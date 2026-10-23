/* cel.c - ver cel.h / PARTE14_CEL.md. Fonte: PhoenixEmuProject.exe (Ghidra 12.1.3), funcoes citadas em cada rotina. */
#include "cel.h"
#include <string.h>

static const int bpp_tab[8] = {1, 1, 2, 4, 6, 8, 16, 16};          /* 0xb030a0 */
int cel_bpp(uint32_t pre0) { return bpp_tab[pre0 & 7]; }

void cel_ccb_reset(cel_ccb_t *c) { memset(c, 0, sizeof *c); }

static uint32_t rd32(const uint32_t *ram, uint32_t a) { return a < CEL_RAM_BYTES ? ram[a >> 2] : 0; }  /* o .exe devolve 0 fora de 3 MB */
static uint32_t rd16(const uint32_t *ram, uint32_t a) {             /* ushort em (a ^ 2), a par */
    return a < CEL_RAM_BYTES ? ((const uint16_t *)ram)[(a ^ 2) >> 1] : 0; }
static uint8_t  rd8 (const uint32_t *ram, uint32_t a) { return a < CEL_RAM_BYTES ? ((const uint8_t *)ram)[a ^ 3] : 0; }

int cel_ccb_read(cel_ccb_t *c, const uint32_t *ram) {
    if (c->next == 0) return -1;
    uint32_t cur = c->next & 0xfffffc;
    if (cur >= CEL_RAM_BYTES) { c->next = 0; return -1; }
    uint32_t pc = cur, v;
    uint32_t fl = rd32(ram, pc); pc += 4; c->pc = pc; c->flags = fl;
    /* next */
    v = (pc < CEL_RAM_BYTES) ? rd32(ram, pc) & ~3u : 0;
    c->next = (fl & CCB_NPABS) ? v : ((v + cur + 8) & 0xffffff);
    pc = cur + 8;
    if (c->next >= CEL_RAM_BYTES) c->next = 0;
    c->pc = pc;
    /* ponteiro dos dados */
    v = (pc < CEL_RAM_BYTES) ? rd32(ram, pc) & ~3u : 0;
    c->src = (fl & CCB_SPABS) ? v : ((v + cur + 0xc) & 0xffffff);
    pc = cur + 0xc; c->pc = pc;
    if (c->src >= CEL_RAM_BYTES) c->src = 0;
    if (fl & CCB_LDPLUT) {
        v = (pc < CEL_RAM_BYTES) ? rd32(ram, pc) & ~3u : 0;
        c->plutp = (fl & CCB_PPABS) ? v : ((v + cur + 0x10) & 0xffffff);
    }
    pc = cur + 0x10; c->pc = pc;
    if ((fl & CCB_YOXY) && !(fl & CCB_SKIP)) {
        c->xpos = (int32_t)rd32(ram, pc); pc += 4;
        c->ypos = (int32_t)rd32(ram, pc); pc += 4;      /* o .exe so avanca; pc final = cur+0x18 */
    }
    pc = cur + 0x18; c->pc = pc;
    /* ATENCAO: o ramo YOXY acima corresponde ao `else` do .exe: ele le xpos/ypos de (cur+0x10, cur+0x14) */
    if (fl & CCB_LAST) c->next = 0;
    if (fl & CCB_LDSIZE) {
        c->hdx = (int32_t)rd32(ram, pc); c->hdy = (int32_t)rd32(ram, pc + 4);
        c->vdx = (int32_t)rd32(ram, pc + 8); c->vdy = (int32_t)rd32(ram, pc + 12); pc += 16;
    }
    if (fl & CCB_LDPRS) { c->hddx = (int32_t)rd32(ram, pc); c->hddy = (int32_t)rd32(ram, pc + 4); pc += 8; }
    if (fl & CCB_LDPPMP) { c->ppmp = rd32(ram, pc); pc += 4; }
    c->pc = pc;
    /* preambulo: no CCB (CCBPRE) ou no inicio dos dados; PRE1 nao existe em CEL empacotado (fica o antigo) */
    if (fl & CCB_CCBPRE) {
        c->pre0 = rd32(ram, pc); pc += 4;
        if (!(fl & CCB_PACKED)) { c->pre1 = rd32(ram, pc); pc += 4; }
        c->pc = pc;
    } else {
        c->pre0 = rd32(ram, c->src); c->src += 4;
        if (!(fl & CCB_PACKED)) { c->pre1 = rd32(ram, c->src); c->src += 4; }
    }
    if (fl & CCB_LDPLUT) {
        int b = c->pre0 & 7, n = b < 3 ? 8 : (b == 3 ? 16 : 32);
        uint32_t h = c->plutp >> 1;
        for (int i = 0; i < n; i++) c->plut32[i] = rd16(ram, (h + i) * 2);
        for (int i = 0; i < 32; i++) c->plut16[i] = (uint16_t)c->plut32[i];
    }
    if (fl & CCB_SKIP) return 0;
    return (fl & (CCB_ACW | CCB_ACCW)) ? 1 : 0;
}

cel_geom_t cel_geometry(const cel_ccb_t *c) {
    cel_geom_t g; int bpp = cel_bpp(c->pre0);
    uint32_t wo = bpp < 8 ? c->pre1 >> 24 : (c->pre1 & 0x3ff0000) >> 16;
    g.bpp = bpp;
    g.stride_bytes = (int)(wo * 4 + 8);
    g.width = (int)(c->pre1 & 0x7ff) + 1;
    g.height = (int)((c->pre0 & 0xffc0) >> 6) + 1;
    if (c->pre1 & 0x800) g.height *= 2;
    return g;
}

uint32_t cel_index_to_color(const cel_ccb_t *c, uint32_t v) {
    uint32_t pre0 = c->pre0, fl = c->flags, r;
    switch (pre0 & 7) {
    default: r = c->plut16[(v & 1) + (fl & 0xf) * 2]; break;
    case 2:  r = c->plut16[(v & 3) + (fl & 0xe) * 2]; break;
    case 3:  r = c->plut16[(v & 0xf) + (fl & 8) * 2]; break;
    case 4:  r = ((v >> 5 & 1) * 0x8000) + (c->plut16[v & 0x1f] & 0x7fff); break;
    case 5:
        if (!(pre0 & 0x10)) { uint32_t t = (v >> 5) & 7; r = ((t * 8 | t << 6 | t) << 16) | c->plut16[v & 0x1f]; }
        else {
            if (!(pre0 & 8))
                v = (v & 0x1c) << 5 | (v & 3) << 3 | (v & 0xe0) << 7 | (v & 1) * 2 | (v & 2) >> 1 | (v & 2) * 2 | (v & 0x18) << 2 | (v & 0xc0) << 4;
            else v = (v & 0x1c) << 5 | (v & 3) << 3 | (v & 0xe0) << 7;
            r = (fl & 0x80) << 8 | v;
        }
        break;
    case 6: case 7:
        r = v;
        if (!(pre0 & 0x10)) r = (v & 0x8000) | ((v & 0x3fe0) << 11) | (c->plut16[v & 0x1f] & 0x7fff);
        break;
    }
    if (!(fl & CCB_BGND) && (r & 0x7fff) == 0) r |= 0x80000000u;
    return r;
}

uint32_t cel_bits_get(cel_bits_t *b, unsigned n) {
    if (n > 32) n = 32; else if (n == 0) n = 1;
    uint32_t r = 0;
    while (n) {
        uint8_t by = rd8(b->ram, b->base + b->byte);
        unsigned avail = 8 - b->bit, take = n < avail ? n : avail;
        r = (r << take) | ((by >> (avail - take)) & ((1u << take) - 1));
        b->bit += take; n -= take;
        if (b->bit == 8) { b->bit = 0; b->byte++; }
    }
    return r;
}

int cel_decode_packed(const cel_ccb_t *c, const uint32_t *ram, uint32_t *out, int *rows_out) {
    int bpp = cel_bpp(c->pre0), rows = (int)((c->pre0 & 0xffc0) >> 6) + 1, maxw = 0;
    uint32_t row = c->src;
    for (int y = 0; y < rows; y++) {
        cel_bits_t b = { ram, row, 0, 0 };
        uint32_t off = cel_bits_get(&b, bpp < 8 ? 8 : 16);
        uint32_t end = 8 + off * 4;                              /* bytes da linha, incl. cabecalho */
        uint32_t *o = out + (size_t)y * 0x800; int x = 0;
        for (;;) {
            uint32_t type = cel_bits_get(&b, 2);
            if (end <= b.byte) { cel_bits_get(&b, 6); break; }   /* .exe: testa o fim DEPOIS de ler o tipo */
            uint32_t cnt = cel_bits_get(&b, 6) + 1;
            if (type == 0) break;
            if (type == 1) { for (uint32_t k = 0; k < cnt && x < 0x800; k++) o[x++] = cel_index_to_color(c, cel_bits_get(&b, bpp)); }
            else if (type == 2) { for (uint32_t k = 0; k < cnt && x < 0x800; k++) o[x++] = 0x80000000u; }
            else { uint32_t p = cel_index_to_color(c, cel_bits_get(&b, bpp)); for (uint32_t k = 0; k < cnt && x < 0x800; k++) o[x++] = p; }
        }
        if (x > maxw) maxw = x;
        for (int k = x; k < 0x800; k++) o[k] = 0x80000000u;
        row += end;
    }
    if (rows_out) *rows_out = rows;
    return maxw;
}

int cel_decode_unpacked(const cel_ccb_t *c, const uint32_t *ram, uint32_t *out) {
    const cel_geom_t g = cel_geometry(c);
    if (g.bpp >= 16 || (c->pre1 & 0x800)) return 0;
    const uint32_t mask = (1u << g.bpp) - 1;
    for (int y = 0; y < g.height; y++) {
        const uint32_t base = c->src + (uint32_t)y * (uint32_t)g.stride_bytes;
        for (int x = 0; x < g.width; x++) {
            const uint32_t bitpos = (uint32_t)x * (uint32_t)g.bpp, a = base + (bitpos >> 3);
            const uint32_t b0 = a < CEL_RAM_BYTES ? rd8(ram, a) : 0, b1 = a + 1 < CEL_RAM_BYTES ? rd8(ram, a + 1) : 0;
            const uint32_t v = (((b0 << 8) | b1) >> (16 - (bitpos & 7) - (uint32_t)g.bpp)) & mask;
            out[(size_t)y * g.width + x] = cel_index_to_color(c, v);
        }
    }
    return 1;
}

/* ---------------- Parte 15 ---------------- */
#include <math.h>
double cel_cross(const cel_ccb_t *c) {
    double hdx = c->hdx * (1.0 / 1048576.0), hdy = c->hdy * (1.0 / 1048576.0);   /* 2^-20 (0xb03118) */
    double vdx = c->vdx * (1.0 / 65536.0),   vdy = c->vdy * (1.0 / 65536.0);     /* 2^-16 (0xb030f8) */
    /* forma literal do .exe: ((vdx+hdx)*(hdy-vdy) + vdy*vdx) - hdy*hdx  ( = vdx*hdy - hdx*vdy ) */
    return ((vdx + hdx) * (hdy - vdy) + vdy * vdx) - hdy * hdx;
}
uint32_t cel_winding(const cel_ccb_t *c) { return cel_cross(c) < 0.0 ? CEL_WIND_CCW : CEL_WIND_CW; }
int cel_culled(const cel_ccb_t *c) {
    uint32_t f = c->flags & (CCB_ACW | CCB_ACCW);
    return f != (CCB_ACW | CCB_ACCW) && f == cel_winding(c);
}
static int is_abs(int32_t v, int32_t k) { return v == k || v == -k; }
int cel_is_unit_blit(const cel_ccb_t *c) {
    int h = (is_abs(c->hdx, 0x100000) && c->hdy == 0) || (c->hdx == 0 && is_abs(c->hdy, 0x100000));
    int v = (is_abs(c->vdx, 0x10000) && c->vdy == 0) || (c->vdx == 0 && is_abs(c->vdy, 0x10000));
    return h && v;
}
uint32_t cel_fb_addr(uint32_t base, uint32_t stride, int x, int y) {
    return stride * (uint32_t)(y >> 1) + (uint32_t)(y & 1) * 2 + base + (uint32_t)x * 4;
}
uint16_t cel_fb_read16(const uint32_t *ram, uint32_t a) { return a < CEL_RAM_BYTES ? ((const uint16_t *)ram)[(a ^ 2) >> 1] : 0; }
void cel_fb_write16(uint32_t *ram, uint32_t a, uint16_t v) { if (a < CEL_RAM_BYTES) ((uint16_t *)ram)[(a ^ 2) >> 1] = v; }

/* ---------------- Parte 16: FUN_0047d0d0 ---------------- */
void cel_pix_setup(cel_pix_t *p, const cel_ccb_t *c, uint32_t pixc) {
    int packed = (c->flags & CCB_PACKED) != 0;
    p->flags = c->flags;
    p->ppmp0 = (uint16_t)(c->ppmp & 0xffff); p->ppmp1 = (uint16_t)(c->ppmp >> 16);   /* +0x340 = copia do PPMP (+0x1000110) */
    p->pixc = pixc;                                                                   /* +0x344 */
    p->mode_f8 = packed ? (int32_t)((pixc >> 20) & 3) : (int32_t)((c->pre1 >> 12) & 3);
    p->mode_fc = (c->flags & 0x40) != 0;
    if ((packed || !(c->pre1 & 0x4000)) && (pixc & 0x8000000u)) p->mode_fc |= 2;
    p->xpos = c->xpos; p->ypos = c->ypos;                                             /* +0x2b0 / +0x2b4 */
}

static int32_t sar(int32_t v, unsigned n) { return v < 0 ? ~((~v) >> n) : v >> n; }   /* >> aritmetico sem depender do compilador */

uint32_t cel_pixel(const cel_pix_t *p, uint32_t s, uint32_t d) {
    const uint32_t fl = p->flags, pixc = p->pixc;
    /* 1) escolhe a metade do PPMP: POVER (0x100) forca a metade (0x80 = alta); senao vale o bit 15 do pixel de origem */
    const uint32_t half = ((fl & 0x100) ? (fl & 0x80) != 0 : (s & 0x8000) != 0) ? p->ppmp1 : p->ppmp0;
    const uint32_t ms = half & 0x8000, ss = (half >> 6) & 3;        /* ms: multiplica o destino; ss: 2o operando */
    /* 2) bit 15 do resultado (b15) e bit 0 */
    uint32_t b15, top = pixc >> 30;
    if (top < 2) b15 = top << 15;
    else if ((pixc & 0x1000000u) && (ms || ss == 2)) b15 = d & 0x8000;
    else switch (p->mode_fc) {
        case 1:  b15 = s & 0x8000; break;
        case 2:  b15 = (uint32_t)p->xpos & 0x8000; break;          /* neste ramo 2 -> xpos e 0 -> ypos; no ramo do bit 0 (mode 3) e o inverso, como no .exe */
        case 0:  b15 = (uint32_t)p->ypos & 0x8000; break;
        default: b15 = (s & 1) << 15; break;
    }
    const uint32_t mode = (pixc >> 28) & 3;
    if (mode == 1) b15 |= 1;
    else if (mode == 3) {
        if (!(pixc & 0x1000000u) || (ms == 0 && ss != 2)) {
            switch (p->mode_fc) {
            case 1:  b15 |= s & 1; break;
            case 2:  b15 |= ((uint32_t)(p->ypos >> 15)) & 1; break;
            case 0:  b15 |= ((uint32_t)(p->xpos >> 15)) & 1; break;
            default: b15 |= (s >> 15) & 1; break;
            }
        } else b15 |= d & 1;
    }
    /* 3) bit 0 do destino (pixc>>22) e da origem (mode_f8) */
    switch ((pixc >> 22) & 3) {
    case 2: d = ((d >> 4) & 1) | (d & ~1u); break;
    case 3: d = ((d >> 5) & 1) | (d & ~1u); break;
    case 0: d &= ~1u; break;
    default: break;
    }
    uint32_t a;                                                      /* uVar10: origem com o bit 0 ajustado */
    switch (p->mode_f8) {
    case 2:  a = ((s >> 4) & 1) | (s & ~1u); break;
    case 3:  a = ((s >> 5) & 1) | (s & ~1u); break;
    case 0:  a = s & ~1u; break;
    default: a = s; break;
    }
    /* 4) multiplicador (3 bits +1) e divisor (1..4 bits) por canal; canal0 = bits 0-4, canal1 = 5-9, canal2 = 10-14 */
    const uint32_t col = ms ? d : a;
    const uint32_t dv = (((half >> 8) - 1) & 3) + 1;
    uint32_t k0, k1, k2, d0 = dv, d1 = dv, d2 = dv;
    switch ((half >> 13) & 3) {
    case 1:  k0 = ((a >> 16) & 7) + 1; k1 = ((a >> 19) & 7) + 1; k2 = ((a >> 22) & 7) + 1; break;
    case 2:  k0 = ((a >> 2) & 7) + 1;  k1 = ((a >> 7) & 7) + 1;  k2 = ((a >> 12) & 7) + 1;
             d0 = ((a - 1) & 3) + 1; d1 = (((a >> 5) - 1) & 3) + 1; d2 = (((a >> 10) - 1) & 3) + 1; break;
    case 0:  k0 = k1 = k2 = ((half >> 10) & 7) + 1; break;
    default: k0 = ((a >> 2) & 7) + 1;  k1 = ((a >> 7) & 7) + 1;  k2 = ((a >> 12) & 7) + 1; break;
    }
    int32_t c0 = (int32_t)(((col & 0x1f) * k0) >> d0);
    int32_t c1 = (int32_t)((((col >> 5) & 0x1f) * k1) >> d1);
    int32_t c2 = (int32_t)((((col >> 10) & 0x1f) * k2) >> d2);
    /* 5) 2o operando (ss) e combinacao */
    int wrap = 0;                                                    /* 1 = mascara 5 bits sem saturar (FUN_0047d0d0 rotulo 0047d43c) */
    if (ss == 0) {
        if (half & 1) { c0 = sar(c0, 1); c1 = sar(c1, 1); c2 = sar(c2, 1); }
        if ((fl & 0x400) && (half & 8)) wrap = 1;
    } else {
        int32_t o0, o1, o2;                                          /* operando: o0 casa com c0, o1 com c1, o2 com c2 */
        if (ss == 1)      o0 = o1 = o2 = (int32_t)((half >> 1) & 0x1f);
        else if (ss == 2) { o0 = (int32_t)(d & 0x1f); o1 = (int32_t)((d >> 5) & 0x1f); o2 = (int32_t)((d >> 10) & 0x1f); }
        else              { o0 = (int32_t)(a & 0x1f); o1 = (int32_t)((a >> 5) & 0x1f); o2 = (int32_t)((a >> 10) & 0x1f); }
        if (!(fl & 0x400)) {                                         /* sem USEAV: soma ou XOR */
            if (!(fl & 0x800)) { c0 += o0; c1 += o1; c2 += o2; } else { c0 ^= o0; c1 ^= o1; c2 ^= o2; }
            if (half & 1) { c0 = sar(c0, 1); c1 = sar(c1, 1); c2 = sar(c2, 1); }
        } else {                                                     /* USEAV: operando deslocado, opcionalmente com sinal */
            uint32_t m = (half >> 4) & 3; unsigned s0, s1, s2;
            if (m == 3) { s0 = a & 3; s1 = (a >> 5) & 3; s2 = (a >> 10) & 3; } else s0 = s1 = s2 = m;
            o0 = sar(o0, s0); o1 = sar(o1, s1); o2 = sar(o2, s2);
            if (half & 4) {                                          /* extensao de sinal de 5 bits */
                o0 = sar((int32_t)((uint32_t)o0 << 27), 27); o1 = sar((int32_t)((uint32_t)o1 << 27), 27); o2 = sar((int32_t)((uint32_t)o2 << 27), 27);
            }
            if (!(fl & 0x800)) {
                if (!(half & 2)) { c0 += o0; c1 += o1; c2 += o2; } else { c0 -= o0; c1 -= o1; c2 -= o2; }
            } else { c0 ^= o0; c1 ^= o1; c2 ^= o2; }
            if (half & 1) { c0 = sar(c0, 1); c1 = sar(c1, 1); c2 = sar(c2, 1); }
            if (half & 8) wrap = 1;
            else if (half & 6) { if (c2 < 0) c2 = 0; if (c1 < 0) c1 = 0; if (c0 < 0) c0 = 0; }
        }
    }
    uint32_t out;
    if (wrap) out = (((uint32_t)c1 & 0x1f) << 5) | (((uint32_t)c2 & 0x1f) << 10) | ((uint32_t)c0 & 0x1f);
    else {
        if (c1 > 0x1f) c1 = 0x1f;
        if (c2 > 0x1f) c2 = 0x1f;
        if (c0 > 0x1f) c0 = 0x1f;
        out = ((uint32_t)c1 << 5) | ((uint32_t)c2 << 10) | (uint32_t)c0;
    }
    uint32_t r = out | b15;
    if (mode == 0) r &= ~1u;
    if (!(fl & 0x10) && (r & 0x7fff) == 0) r |= 0x400;
    return r;
}

/* ---------------- Parte 17: FUN_00480290, caminho 1 ---------------- */
void cel_raster_setup(cel_raster_t *r, const cel_ccb_t *c, uint32_t reg124, uint32_t reg128, uint32_t reg12c, uint32_t reg21c, uint32_t reg220) {
    cel_geom_t g = cel_geometry(c);
    r->width = g.width; r->height = g.height;
    r->clip_x = (int)(reg124 & 0x3ff); r->clip_y = (int)((reg124 >> 16) & 0x3ff);
    r->wr_base = reg12c; r->wr_step = reg220; r->rd_base = reg128; r->rd_step = reg21c;
    r->src = c->src; r->tex_x = r->tex_y = 0; r->tex = 0;
}

static int sgn(int a, int b) { return a < b ? 1 : (a > b ? -1 : 0); }   /* sinal de (b - a) */
typedef struct { int x, y, tx, ty; } cpt_t;
static void cswap(cpt_t *a, cpt_t *b) { cpt_t t = *a; *a = *b; *b = t; }

int cel_blit11(const cel_ccb_t *c, const cel_pix_t *px, const cel_raster_t *r, uint32_t *ram) {
    const int W = r->width, H = r->height, sc = (int)((c->pre0 >> 24) & 0xf);
    if (sc >= W) return 1;                                         /* nada a desenhar (a funcao inteira sai) */
    if (c->hddx != 0 || c->hddy != 0 || !cel_is_unit_blit(c)) return 0;
    if (cel_culled(c)) return 1;                                   /* descarte por enrolamento (Parte 15, secao 3) */
    const int n = W - sc;
    /* parte inteira dos vetores e da posicao (shorts nas metades altas dos campos derivados) */
    const int hdx_i = (int16_t)((c->hdx >> 4) >> 16), hdy_i = (int16_t)((c->hdy >> 4) >> 16);
    const int vdx_i = c->vdx >> 16, vdy_i = c->vdy >> 16;
    const int xp = (int16_t)(c->xpos >> 16), yp = (int16_t)(c->ypos >> 16);
    cpt_t p[4];
    p[0] = (cpt_t){ xp, yp, sc, 0 };                                              /* A */
    p[1] = (cpt_t){ xp + hdx_i * n, yp + hdy_i * n, W - 1, 0 };                   /* B: fim da linha */
    p[3] = (cpt_t){ xp + vdx_i * H, yp + vdy_i * H, sc, H - 1 };                  /* C: fim da coluna */
    p[2] = (cpt_t){ p[3].x + hdx_i * n, p[3].y + hdy_i * n, W - 1, H - 1 };       /* D */
    if (p[2].y < p[0].y) cswap(&p[0], &p[2]);
    if (p[3].y < p[1].y) cswap(&p[1], &p[3]);
    if (p[1].x < p[0].x) { cswap(&p[0], &p[1]); cswap(&p[2], &p[3]); }
    /* recorte ao retangulo inclusivo [0..clip] */
    int xs = 0, xe = 0;
    if (p[0].x >= 0) { xs = r->clip_x + 1; if (p[0].x <= r->clip_x) xs = p[0].x; }
    if (p[1].x >= 0) { xe = r->clip_x + 1; if (p[1].x <= r->clip_x) xe = p[1].x; }
    if (xs == xe) return 1;
    int ys = p[0].y, skip = 0, ye = 0;
    if (p[0].y < 0) { ys = 0; skip = -p[0].y; }
    else if (r->clip_y < p[0].y) { ys = r->clip_y + 1; skip = ys - p[0].y; }
    if (p[3].y >= 0) { ye = p[3].y; if (r->clip_y < p[3].y) ye = r->clip_y + 1; }
    const int sx = sgn(p[0].tx, p[1].tx), sy = sgn(p[0].ty, p[1].ty);   /* passo do texel por pixel (x do destino) */
    const int cx = sgn(p[0].tx, p[3].tx), cy = sgn(p[0].ty, p[3].ty);   /* passo do texel por linha (y do destino) */
    if (ye <= ys) return 1;
    int tx_row = p[0].tx + cx * skip + (xs - p[0].x) * sx;
    int ty_row = p[0].ty + cy * skip + (xs - p[0].x) * sy;
    const uint32_t stride = (((c->pre1 >> 16) & 0x3ff) * 4 + 8);
    const cel_tex_t *t = r->tex;
    for (int y = ys; y != ye; y++) {
        int tx = tx_row, ty = ty_row;
        for (int x = xs; x < xe; x++, tx += sx, ty += sy) {
            uint32_t col;
            if (!t) {                                              /* fonte crua de 16 bits na RAM + PLUT */
                /* fonte CRUA = linear; com PRE1 bit 11 (0x800, que dobra a altura) cada linha de 1280 bytes guarda DUAS linhas entrelacadas
                 * (par no meio-palavra alto, impar no baixo), o mesmo formato do framebuffer: copia de tela do jogo (The Horde) */
                uint32_t a = (c->pre1 & 0x800) ? (uint32_t)(ty >> 1) * stride + r->src + (uint32_t)tx * 4 + (uint32_t)(ty & 1) * 2
                                               : (uint32_t)ty * stride + r->src + (uint32_t)tx * 2;
                col = cel_index_to_color(c, cel_fb_read16(ram, a));
            } else {
                long i = (long)(r->tex_y + ty) * t->width + r->tex_x + tx;
                if (i < 0 || i >= (long)t->width * t->height) continue;   /* o .exe leria memoria arbitraria; aqui vira "transparente" */
                col = t->data[i];
            }
            if (col & 0x80000000u) continue;
            uint32_t wa = r->wr_step * (uint32_t)(y >> 1) + (uint32_t)(y & 1) * 2 + r->wr_base + (uint32_t)x * 4;
            if (t && t->converted) { cel_fb_write16(ram, wa, (uint16_t)col); continue; }   /* ja passou pelo PPMP no cache: sem leitura do destino */
            uint32_t ra = r->rd_step * (uint32_t)(y >> 1) + (uint32_t)(y & 1) * 2 + r->rd_base + (uint32_t)x * 4;
            cel_fb_write16(ram, wa, (uint16_t)cel_pixel(px, col, cel_fb_read16(ram, ra)));
        }
        tx_row += cx; ty_row += cy;
    }
    return 1;
}

/* ---------------- Parte 21: FUN_00480290, caminho 3 (malha de triangulos) ----------------
 * Substitui os antigos cel_blit22/cel_blit33 (aproximacoes que NAO vinham do .exe: o blit33 usava a posicao da TELA como
 * coordenada de textura e lia lixo da RAM -> quadro cheio de listras diagonais em jogos com CELs escalados/rotacionados,
 * como Road Rash). Aqui o mapeamento e o do .exe (dec_00480290.c, ramo final; dec_0047e3c0.c = FUN_0047e3c0):
 *   cantos P0 = pos, P1 = P0 + hd*n, P3 = P0 + vd*H, P2 = P3 + (hd + hdd*H)*n   (n = largura - start_col; doubles 12.20/16.16)
 *   malha (N+1)x(N+1), N = min(7, (int)raiz4(|dif. das arestas|)/2 + 1) (regra de FUN_00480290 para largura/altura pequenas);
 *   vertice (r,c): linha r comeca em P0 + r*(P3-P0)/N; passo da linha = passo_topo + r*(passo_base-passo_topo)/N;
 *   textura u = start_col + c*n/N, v = r*H/N.  Cada celula = 2 triangulos (v00,v01,v11) e (v00,v11,v10), com o mesmo
 *   descarte por enrolamento de FUN_0047e3c0 (flags&0x60000 contra o sinal da area).
 * Diferencas assumidas (FUN_0047d9e0, o preenchedor de linhas, nao foi decompilado): amostragem pelo vizinho mais proximo no
 * centro do pixel, borda superior/esquerda inclusiva. O .exe soma -0.5 as posicoes e usa o centro do pixel em inteiros;
 * aqui o centro do pixel fica em +0.5 e os cantos em pos (mesma cobertura). O resultado com escala 1 coincide com o caminho 1. */
#include <math.h>
typedef struct { double x, y, u, v; } cvx_t;
cel_dbg_t g_cel_dbg[2]; int g_cel_dbg_cls;

static void poly_plot(const cel_ccb_t *c, const cel_pix_t *px, const cel_raster_t *r, uint32_t *ram, int x, int y, int tx, int ty) {
    const cel_tex_t *t = r->tex;
    uint32_t col;
    if (!t) {
        const uint32_t stride = (((c->pre1 >> 16) & 0x3ff) * 4 + 8);
        /* fonte CRUA = linear; com PRE1 bit 11 (0x800, que dobra a altura) cada linha de 1280 bytes guarda DUAS linhas entrelacadas
                 * (par no meio-palavra alto, impar no baixo), o mesmo formato do framebuffer: copia de tela do jogo (The Horde) */
                uint32_t a = (c->pre1 & 0x800) ? (uint32_t)(ty >> 1) * stride + r->src + (uint32_t)tx * 4 + (uint32_t)(ty & 1) * 2
                                               : (uint32_t)ty * stride + r->src + (uint32_t)tx * 2;
        col = cel_index_to_color(c, cel_fb_read16(ram, a));
    } else {
        long i = (long)(r->tex_y + ty) * t->width + r->tex_x + tx;
        if (i < 0 || i >= (long)t->width * t->height) { g_cel_dbg[g_cel_dbg_cls].px_oob++; return; }
        col = t->data[i];
    }
    if (col & 0x80000000u) { g_cel_dbg[g_cel_dbg_cls].px_transp++; return; }
    g_cel_dbg[g_cel_dbg_cls].px++;
    uint32_t wa = r->wr_step * (uint32_t)(y >> 1) + (uint32_t)(y & 1) * 2 + r->wr_base + (uint32_t)x * 4;
    if (t && t->converted) { cel_fb_write16(ram, wa, (uint16_t)col); return; }
    uint32_t ra = r->rd_step * (uint32_t)(y >> 1) + (uint32_t)(y & 1) * 2 + r->rd_base + (uint32_t)x * 4;
    cel_fb_write16(ram, wa, (uint16_t)cel_pixel(px, col, cel_fb_read16(ram, ra)));
}

static void poly_tri(const cel_ccb_t *c, const cel_pix_t *px, const cel_raster_t *r, uint32_t *ram, const cvx_t *a, const cvx_t *b, const cvx_t *d) {
    const uint32_t fl = c->flags & 0x60000u;
    g_cel_dbg[g_cel_dbg_cls].tri++;
    if (fl != 0x60000u) {                                          /* FUN_0047e3c0: so desenha se o enrolamento bate com as flags */
        double s = (a->x + b->x) * (a->y - b->y) + (b->x + d->x) * (b->y - d->y) + (a->x + d->x) * (d->y - a->y);
        if (fl != (s < 0.0 ? 0x40000u : 0x20000u)) { g_cel_dbg[g_cel_dbg_cls].tri_wind++; return; }
    }
    const cvx_t *t = a, *m = b, *bt = d, *tmp;                     /* ordena por y: t (topo) <= m <= bt (base) */
    if (m->y < t->y)  { tmp = t; t = m; m = tmp; }
    if (bt->y < m->y) { tmp = m; m = bt; bt = tmp; }
    if (m->y < t->y)  { tmp = t; t = m; m = tmp; }
    double area = (m->x - t->x) * (bt->y - t->y) - (bt->x - t->x) * (m->y - t->y);
    if (area == 0.0) { g_cel_dbg[g_cel_dbg_cls].tri_area0++; return; }
    const double dudx = ((m->u - t->u) * (bt->y - t->y) - (bt->u - t->u) * (m->y - t->y)) / area;
    const double dudy = ((bt->u - t->u) * (m->x - t->x) - (m->u - t->u) * (bt->x - t->x)) / area;
    const double dvdx = ((m->v - t->v) * (bt->y - t->y) - (bt->v - t->v) * (m->y - t->y)) / area;
    const double dvdy = ((bt->v - t->v) * (m->x - t->x) - (m->v - t->v) * (bt->x - t->x)) / area;
    if (!(fabs(t->x) < 1e7 && fabs(t->y) < 1e7 && fabs(m->x) < 1e7 && fabs(m->y) < 1e7 && fabs(bt->x) < 1e7 && fabs(bt->y) < 1e7)) { g_cel_dbg[g_cel_dbg_cls].tri_far++; return; }
    int y0 = (int)ceil(t->y - 0.5), y1 = (int)ceil(bt->y - 0.5);
    if (y0 < 0) y0 = 0;
    if (y1 > r->clip_y + 1) y1 = r->clip_y + 1;
    const int W = r->width, H = r->height, sc = (int)((c->pre0 >> 24) & 0xf);
    for (int y = y0; y < y1; y++) {
        const double yc = y + 0.5;
        double xl = t->x + (yc - t->y) * (bt->x - t->x) / (bt->y - t->y), xs;
        if (yc < m->y) xs = t->x + (yc - t->y) * (m->x - t->x) / (m->y - t->y);
        else           xs = m->x + (yc - m->y) * (bt->x - m->x) / (bt->y - m->y);
        double xa = xl < xs ? xl : xs, xb = xl < xs ? xs : xl;
        int x0 = (int)ceil(xa - 0.5), x1 = (int)ceil(xb - 0.5);
        if (x0 < 0) x0 = 0;
        if (x1 > r->clip_x + 1) x1 = r->clip_x + 1;
        for (int x = x0; x < x1; x++) {
            const double xc = x + 0.5;
            int tx = (int)floor(t->u + dudx * (xc - t->x) + dudy * (yc - t->y));
            int ty = (int)floor(t->v + dvdx * (xc - t->x) + dvdy * (yc - t->y));
            if (tx < sc) tx = sc; else if (tx > W - 1) tx = W - 1;
            if (ty < 0) ty = 0;   else if (ty > H - 1) ty = H - 1;
            poly_plot(c, px, r, ram, x, y, tx, ty);
        }
    }
}

int cel_blit_poly(const cel_ccb_t *c, const cel_pix_t *px, const cel_raster_t *r, uint32_t *ram) {
    const int W = r->width, H = r->height, sc = (int)((c->pre0 >> 24) & 0xf);
    if (sc >= W || H <= 0) { g_cel_dbg[g_cel_dbg_cls].early++; return 1; }
    const int n = W - sc;
    const double hdx = c->hdx * (1.0 / 1048576.0), hdy = c->hdy * (1.0 / 1048576.0);       /* 12.20 */
    const double hddx = c->hddx * (1.0 / 1048576.0), hddy = c->hddy * (1.0 / 1048576.0);
    const double vdx = c->vdx * (1.0 / 65536.0), vdy = c->vdy * (1.0 / 65536.0);           /* 16.16 */
    const double x0 = c->xpos * (1.0 / 65536.0), y0 = c->ypos * (1.0 / 65536.0);
    const double P0x = x0, P0y = y0;
    const double P1x = hdx * n + x0, P1y = hdy * n + y0;
    const double P3x = vdx * H + x0, P3y = vdy * H + y0;
    const double P2x = (hddx * H + hdx) * n + P3x, P2y = (hddy * H + hdy) * n + P3y;
    const double ex = (P2x - P3x) - (P1x - P0x), ey = (P2y - P3y) - (P1y - P0y);
    int N = (int)sqrt(sqrt(ex * ex + ey * ey)) / 2 + 1;
    if (N > 7) N = 7;
    if (H < N && W < N) { N = W; if (W < H) N = H; }
    if (N < 1) N = 1;
    cvx_t g[8][8];
    double sx = P0x, sy = P0y;                                   /* inicio da linha */
    double stx = (P1x - P0x) / N, sty = (P1y - P0y) / N;         /* passo da linha (topo) */
    const double dstx = (P2x - P3x) / N - stx, dsty = (P2y - P3y) / N - sty;
    const double vx = P3x - P0x, vy = P3y - P0y;
    for (int i = 0; i <= N; i++) {
        double x = sx, y = sy;
        for (int j = 0; j <= N; j++) {
            g[i][j].x = x; g[i][j].y = y;
            g[i][j].u = (double)j * n / N + sc;
            g[i][j].v = (double)i * H / N;
            x += stx; y += sty;
        }
        sx += vx / N; sy += vy / N; stx += dstx / N; sty += dsty / N;
    }
    for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) {
        poly_tri(c, px, r, ram, &g[i][j], &g[i][j + 1], &g[i + 1][j + 1]);
        poly_tri(c, px, r, ram, &g[i][j], &g[i + 1][j + 1], &g[i + 1][j]);
    }
    return 1;
}
