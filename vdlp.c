/* vdlp.c - ver vdlp.h e PARTE18_VDLP_RODADA1.md */
#include "vdlp.h"
#include <stdlib.h>
#include <string.h>

static const uint32_t WIDTH_TAB[8] = { 0x140, 0x180, 0x200, 0x280, 0x400, 0x140, 0x140, 0x140 };   /* .rdata 0xb02d00 */

/* leitura de palavra na VRAM: fora de 0x200000..0x2fffff devolve 0 (FUN_004740d0) */
static uint32_t vbase(const vdlp_t *v) { return v->vram_base ? v->vram_base : 0x200000u; }
static uint32_t vsize(const vdlp_t *v) { return v->vram_bytes ? v->vram_bytes : 0x100000u; }
static uint32_t vram32(const vdlp_t *v, uint32_t a) {
    uint32_t o = a - vbase(v);
    return o < vsize(v) ? v->vram[o >> 2] : 0;
}
/* halfword de pixel: o .exe le *(short*)(ram + ((bmp ^ 2) & 0xfffff) + 4*i) num host little-endian onde cada
 * palavra guarda o inteiro big-endian; com isso: bit1 do endereco = 0 -> metade ALTA da palavra, = 1 -> BAIXA */
static uint16_t pix16p(const uint32_t *vram, uint32_t mask, uint32_t bmp, uint32_t i) {   /* mask = tamanho da VRAM - 1 */
    uint32_t b = (((bmp ^ 2u) & mask) + 4u * i) & mask;
    uint32_t w = vram[b >> 2];
    return (uint16_t)((b & 2) ? (w >> 16) : (w & 0xffffu));
}
static uint16_t pix16(const vdlp_t *v, uint32_t bmp, uint32_t i) { return pix16p(v->vram, vsize(v) - 1u, bmp, i); }

int vdlp_init_mode(vdlp_t *v, int mode, const uint32_t *vram) {           /* ctor FUN_00478c50 */
    int pal = (mode == VDLP_MODE_PAL2 || mode == VDLP_MODE_PAL1);
    memset(v, 0, sizeof *v);
    v->pal = pal ? 1 : 0;
    v->lines = pal ? 0x120 : 0xf0;  v->width = (mode == VDLP_MODE_PAL2) ? 0x180 : 0x140;  v->first_line = pal ? 0x11 : 0x10;
    v->vram = vram;
    v->rgb = calloc((size_t)v->width * v->lines * 3, 1);
    if (!v->rgb) return -1;
    for (int i = 0; i < 32; i++) { uint8_t g = (uint8_t)((i << 3) | (i >> 2)); v->clut[0][i] = v->clut[1][i] = v->clut[2][i] = g; }
    for (int i = 0; i < VDLP_MAX_LINES; i++) v->line[i].dirty = 1;
    return 0;
}
int vdlp_init(vdlp_t *v, int pal, const uint32_t *vram) { return vdlp_init_mode(v, pal ? VDLP_MODE_PAL2 : VDLP_MODE_NTSC, vram); }
void vdlp_free(vdlp_t *v) { free(v->rgb); v->rgb = NULL; free(v->rgb_hr); v->rgb_hr = NULL; }
void vdlp_set_bypass_clut(vdlp_t *v, int on) {
    on = on ? 1 : 0;
    if (v->bypass_clut == on) return;
    v->bypass_clut = on; v->force_dirty = 1;
    for (int i = 0; i < VDLP_MAX_LINES; i++) v->line[i].dirty = 1;       /* reconverte o quadro inteiro na proxima apresentacao */
}
int vdlp_set_hires(vdlp_t *v, int on, const uint32_t *p1, const uint32_t *p2, const uint32_t *p3) {
    on = (on && p1 && p2 && p3) ? 1 : 0;
    if (on) {
        if (!v->rgb_hr) { v->rgb_hr = calloc((size_t)v->width * 2 * (size_t)v->lines * 2 * 3, 1); if (!v->rgb_hr) return -1; }
        v->hr[0] = p1; v->hr[1] = p2; v->hr[2] = p3;
    } else {
        v->hr[0] = v->hr[1] = v->hr[2] = NULL;
    }
    if (v->hires != on) {
        v->hires = on; v->force_dirty = 1;
        for (int i = 0; i < VDLP_MAX_LINES; i++) v->line[i].dirty = 1;      /* reconverte o quadro inteiro no novo formato */
    }
    return 0;
}
void vdlp_set_head(vdlp_t *v, uint32_t addr) { v->headvdl = addr; }

/* FUN_004740d0: le uma entrada da VDL (cabecalho de 4 palavras + palavras de cor/controle) */
static void vdl_read(vdlp_t *v) {
    uint32_t cur = v->curvdl;
    if ((cur - vbase(v)) >= vsize(v)) { v->stat_empty_vdl++; v->linedelay = 0x1ff; return; }
    uint32_t ctl = v->vram[(cur - vbase(v)) >> 2];
    if (ctl == 0) { v->stat_empty_vdl++; v->linedelay = 0x1ff; return; }
    v->stat_ctl_seen |= ctl; v->stat_width_seen |= 1u << ((ctl >> 23) & 7);
    v->clutdma = ctl;
    if (ctl & 0x10000u) v->curbmp  = vram32(v, cur + 4);
    if (ctl & 0x8000u)  v->prevbmp = vram32(v, cur + 8);
    uint32_t next = vram32(v, cur + 12);
    if (ctl & 0x40000u) next += cur + 0x10;                               /* proximo VDL relativo */
    cur += 0x10;
    int n = (int)((ctl >> 9) & 0x3f), stop = 0;
    for (int k = 0; k < n; k++) {
        uint32_t w = 0; int inram = ((cur - vbase(v)) < vsize(v));
        if (inram) w = v->vram[(cur - vbase(v)) >> 2];
        cur += 4;
        if (inram && (int32_t)w < 0) {
            if ((w >> 29) == 7u && w != 0xffffffffu) {      /* cor de fundo: como no Opera (case 0x7 de vdlp_process_optional_cmds), toda palavra 111xxxxx vale e a parada (bit 1 do controle) NAO a bloqueia; 0xffffffff segue sendo o reset da CLUT */
                v->bg = (w & 0xff) << 16 | (w >> 16 & 0xff) | (w & 0xff00);
            } else if ((w & 0xe0000000u) == 0xc0000000u) {
                if (!stop) { v->outctl = w; v->stat_ctl_bits |= w; stop = (int)(w & 2); }
            } else if (w == 0xffffffffu && !stop) {
                for (int i = 0; i < 32; i++) { uint8_t g = (uint8_t)((i << 3) | (i >> 2)); v->clut[0][i] = v->clut[1][i] = v->clut[2][i] = g; }
            }
        } else {                                                          /* palavra de cor (fora da VRAM: w=0 -> entrada 0 = preto) */
            uint32_t i = (w & 0x1f000000u) >> 24, sel = w & 0x60000000u;
            if (!inram) { i = 0; sel = 0; w = 0; }
            if (sel == 0)               { v->clut[0][i] = (uint8_t)w; v->clut[1][i] = (uint8_t)(w >> 8); v->clut[2][i] = (uint8_t)(w >> 16); }
            else if (sel == 0x60000000u)  v->clut[2][i] = (uint8_t)(w >> 16);
            else if (sel == 0x40000000u)  v->clut[1][i] = (uint8_t)(w >> 8);
            else if (sel == 0x20000000u)  v->clut[0][i] = (uint8_t)w;
        }
    }
    v->curvdl = next;
    v->width_sel = WIDTH_TAB[(ctl >> 23) & 7];
    v->linedelay = (int32_t)(ctl & 0x1ff);
}

/* FUN_00474410 */
int vdlp_line(vdlp_t *v, uint32_t line_reg) {
    uint32_t ln = line_reg & 0x7ff;
    if (ln == 0) { v->linedelay = 0; v->curvdl = v->headvdl; vdl_read(v); }
    if (v->linedelay == 0) vdl_read(v);
    int idx = (int)ln - v->first_line;
    uint32_t flags = (v->clutdma >> 16) & 0xff;
    if (idx >= 0 && idx < v->lines) {
        uint32_t sel_w = v->width_sel;
        int n = (int)(sel_w < (uint32_t)v->width ? sel_w : (uint32_t)v->width);
        if (sel_w >= 0x280) v->stat_hires_lines++;
        vdlp_line_t *L = &v->line[idx];
        L->dirty = v->force_dirty;
        if (flags & 0x20) {                                              /* EnDMA */
            uint32_t crop = ((uint32_t)v->width < sel_w) ? (sel_w - (uint32_t)v->width) / 2 : 0;
            for (int i = 0; i < n; i++) {
                uint16_t p = pix16(v, v->curbmp, crop + (uint32_t)i);
                if (L->pix[i] != p) { L->pix[i] = p; L->dirty = 1; }
            }
            if (v->hires) for (int k = 0; k < 3; k++) for (int i = 0; i < n; i++) {         /* planos 1..3: subamostras do HiRes CEL Rendering */
                uint16_t p = pix16p(v->hr[k], vsize(v) - 1u, v->curbmp, crop + (uint32_t)i);
                if (L->pixh[k][i] != p) { L->pixh[k][i] = p; L->dirty = 1; }
            }
            if (memcmp(L->clut, v->clut, sizeof L->clut)) { memcpy(L->clut, v->clut, sizeof L->clut); L->dirty = 1; }
        }
        if (L->outctl != v->outctl || L->clutdma != v->clutdma || L->bg != v->bg) {
            L->outctl = v->outctl; L->clutdma = v->clutdma; L->bg = v->bg; L->dirty = 1;
        }
    }
    /* avanco dos ponteiros de bitmap (a cada chamada, visivel ou nao) */
    if (!(flags & 2)) v->prevbmp = v->curbmp;
    else v->prevbmp = (v->prevbmp & 2) ? v->prevbmp - 2 + v->width_sel * 4 : v->prevbmp + 2;
    v->curbmp = (v->curbmp & 2) ? v->curbmp - 2 + v->width_sel * 4 : v->curbmp + 2;
    v->linedelay--;
    return idx == v->lines;
}

static void px(const vdlp_line_t *L, uint16_t p, uint8_t *o, int bypass) {
    /* "VDLP Bypass CLUT" (opcao do nucleo; no Opera troca o renderizador de linha inteiro, vdlp_render_line_*_bypass_clut): TODO pixel vira cor direta de
     * 15 bits (bit 15 ignorado), sem CLUT e sem cor de fundo - o pixel 0 sai preto. Cores possivelmente erradas, renderizacao mais rapida. */
    if (bypass) { o[0] = (p >> 7) & 0xf8; o[1] = (p & 0x3e0) >> 2; o[2] = (uint8_t)((p & 0x1f) << 3); return; }
    if ((L->outctl & 0x2000000u) && (p & 0x8000)) { o[0] = (p >> 7) & 0xf8; o[1] = (p & 0x3e0) >> 2; o[2] = (uint8_t)((p & 0x1f) << 3); return; }
    if ((p & 0x7fff) == 0) { o[0] = L->bg & 0xff; o[1] = (L->bg >> 8) & 0xff; o[2] = (L->bg >> 16) & 0xff; return; }
    o[0] = L->clut[2][(p >> 10) & 31]; o[1] = L->clut[1][(p >> 5) & 31]; o[2] = L->clut[0][p & 31];
}
/* CLUT da linha toda zero = o jogo "apagou" a tela pela CLUT (fade a partir do preto) e a VRAM ainda guarda lixo/conteudo antigo. O bypass ignoraria a CLUT
 * e mostraria esse lixo (pontos e faixas coloridas, ex.: Crash 'n Burn e Stellar 7 ao iniciar); com CLUT zerada o resultado certo e preto. */
static int clut_black(const vdlp_line_t *L) {
    for (int c = 0; c < 3; c++) for (int i = 0; i < 32; i++) if (L->clut[c][i]) return 0;
    return 1;
}
static void fill_bg(const vdlp_line_t *L, uint8_t *o, int n, int black) {
    if (black) { memset(o, 0, (size_t)n * 3); return; }                 /* bypass: linha sem DMA de bitmap = preta (como no Opera) */
    for (int i = 0; i < n; i++, o += 3) { o[0] = L->bg & 0xff; o[1] = (L->bg >> 8) & 0xff; o[2] = (L->bg >> 16) & 0xff; }
}

/* FUN_004788b0 (+ FUN_00474e90, caminho simples) */
void vdlp_frame_end(vdlp_t *v) {
    if (v->set_background) v->set_background(v->ud, v->frame_id, v->bg & 0xff, (v->bg >> 8) & 0xff, (v->bg >> 16) & 0xff);
    int run0 = -1, blitted = 0;
    const int hr = v->hires && v->rgb_hr;                                       /* HiRes: duas linhas 2*width por linha nativa */
    const size_t w2 = (size_t)v->width * 2;
    for (int y = 0; y <= v->lines; y++) {
        int dirty = (y < v->lines) && v->line[y].dirty;
        if (dirty) {
            vdlp_line_t *L = &v->line[y];
            const int byp = v->bypass_clut && !clut_black(L);
            if (hr) {
                uint8_t *r0 = v->rgb_hr + (size_t)y * 2 * w2 * 3, *r1 = r0 + w2 * 3;
                if (!((L->clutdma >> 16) & 0x20)) { fill_bg(L, r0, (int)w2, byp); fill_bg(L, r1, (int)w2, byp); }
                else {
                    int n = (int)(v->width_sel < (uint32_t)v->width ? v->width_sel : (uint32_t)v->width);
                    int x0 = (v->width - n) / 2;
                    fill_bg(L, r0, 2 * x0, byp); fill_bg(L, r1, 2 * x0, byp);
                    for (int i = 0; i < n; i++) {                              /* plano q = ((y&1)<<1)|(x&1): linha par = planos 0,1; linha impar = planos 2,3 */
                        size_t o = (size_t)(2 * (x0 + i)) * 3;
                        px(L, L->pix[i],     r0 + o,     byp);
                        px(L, L->pixh[0][i], r0 + o + 3, byp);
                        px(L, L->pixh[1][i], r1 + o,     byp);
                        px(L, L->pixh[2][i], r1 + o + 3, byp);
                    }
                    fill_bg(L, r0 + (size_t)(2 * (x0 + n)) * 3, 2 * (v->width - x0 - n), byp);
                    fill_bg(L, r1 + (size_t)(2 * (x0 + n)) * 3, 2 * (v->width - x0 - n), byp);
                }
            } else {
                uint8_t *row = v->rgb + (size_t)y * v->width * 3;
                if (!((L->clutdma >> 16) & 0x20)) fill_bg(L, row, v->width, byp);
                else {
                    int n = (int)(v->width_sel < (uint32_t)v->width ? v->width_sel : (uint32_t)v->width);
                    int x0 = (v->width - n) / 2;
                    fill_bg(L, row, x0, byp);
                    for (int i = 0; i < n; i++) px(L, L->pix[i], row + (size_t)(x0 + i) * 3, byp);
                    fill_bg(L, row + (size_t)(x0 + n) * 3, v->width - x0 - n, byp);
                }
            }
            L->dirty = 0;
            if (run0 < 0) run0 = y;
        } else if (run0 >= 0) {
            if (v->blit) {
                if (hr) v->blit(v->ud, v->frame_id, run0 * 2, (y - run0) * 2, v->rgb_hr + (size_t)run0 * 2 * w2 * 3, (int)w2);
                else    v->blit(v->ud, v->frame_id, run0, y - run0, v->rgb + (size_t)run0 * v->width * 3, v->width);
            }
            blitted = 1; run0 = -1;
        }
    }
    v->force_dirty = 0;
    if (blitted && v->present) v->present(v->ud, v->frame_id);
}
