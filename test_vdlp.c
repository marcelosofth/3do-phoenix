/* test_vdlp.c - VDLP: leitor de VDL, captura de linha, conversao RGB e ligacao com CLIO/MADAM.
 * Coerencia com a leitura do .exe (PARTE18), nao comparacao com o .exe rodando. */
#include "machine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int fails;
#define CHK(c,msg) do{ if(!(c)){printf("FALHA: %s\n",msg);fails++;} }while(0)

#define VR 0x200000u                       /* base da VRAM no mapa do ARM */
static uint32_t vr[0x40000];               /* VRAM de teste isolado (1 MB) */
static void vw(uint32_t off, uint32_t v) { vr[off >> 2] = v; }
/* grava o halfword do pixel (bmp, i) onde o leitor o procura: bit1 do endereco = 0 -> metade alta */
static void hw(uint32_t bmp_off, uint32_t i, uint16_t p) {
    uint32_t b = (((bmp_off ^ 2u) & 0xfffffu) + 4u * i) & 0xfffffu;
    uint32_t w = vr[b >> 2];
    vr[b >> 2] = (b & 2) ? ((w & 0xffffu) | ((uint32_t)p << 16)) : ((w & 0xffff0000u) | p);
}
/* posicao da linha y do bitmap: pares de linhas intercalados (+0 / +2 bytes), proximo par em +largura*4 */
/* A contagem de linhas da VDL e o avanco do bitmap comecam na linha 0 do CLIO (PARTE18), nao na 1a linha visivel:
 * a linha visivel y e a linha LEAD+y da VDL/bitmap (LEAD = 0x10 NTSC, 0x11 PAL). */
static int LEAD = 0x10;
static void put(uint32_t bmp_off, uint32_t wsel, int y, int x, uint16_t p) { y += LEAD; hw(bmp_off + (uint32_t)(y >> 1) * wsel * 4u + (uint32_t)(y & 1) * 2u, (uint32_t)x, p); }

static int nblit, npres, nbg; static uint8_t bgc[3];
static void t_bg(void *u, uint32_t id, uint8_t r, uint8_t g, uint8_t b) { (void)u; (void)id; bgc[0]=r; bgc[1]=g; bgc[2]=b; nbg++; }
static void t_blit(void *u, uint32_t id, int y0, int n, const uint8_t *rgb, int w) { (void)u;(void)id;(void)y0;(void)n;(void)rgb;(void)w; nblit++; }
static void t_pres(void *u, uint32_t id) { (void)u; (void)id; npres++; }

static void run_frame(vdlp_t *v) { int last = 0; for (uint32_t ln = 0; ln < 0x120 + 0x20; ln++) { if (vdlp_line(v, ln)) last++; } CHK(last == 1, "exatamente uma linha devolve 1 (ultima visivel)"); }
static const uint8_t *P(vdlp_t *v, int x, int y) { return v->rgb + ((size_t)y * v->width + x) * 3; }
#define RGB(v,x,y,r,g,b) (P(v,x,y)[0]==(r) && P(v,x,y)[1]==(g) && P(v,x,y)[2]==(b))

int main(void) {
    vdlp_t v;
    /* 1) tamanhos e valores iniciais (ctor FUN_00478c50) */
    CHK(vdlp_init(&v, 0, vr) == 0 && v.lines == 240 && v.width == 320 && v.first_line == 0x10, "NTSC 320x240, 1a linha 0x10");
    CHK(v.clut[0][1] == 8 && v.clut[1][31] == 255 && v.clut[2][4] == 33, "CLUT inicial = rampa de cinza");
    vdlp_free(&v);
    CHK(vdlp_init(&v, 1, vr) == 0 && v.lines == 288 && v.width == 384 && v.first_line == 0x11, "PAL 384x288, 1a linha 0x11");
    vdlp_free(&v);

    /* 2) um frame NTSC: fundo, CLUT, pixel por CLUT, pixel direto, intercalacao par/impar */
    memset(vr, 0, sizeof vr);
    vdlp_init(&v, 0, vr); v.set_background = t_bg; v.blit = t_blit; v.present = t_pres;
    const uint32_t VDL = 0x100, BMP = 0x2000;
    /* ctl: 240 linhas | 3 palavras<<9 | bit16 (le CURRENTBMP) | EnDMA 0x200000 | largura 0 (320) | proximo absoluto */
    vw(VDL + 0, (240u + 16) | (3u << 9) | 0x10000u | 0x200000u);
    vw(VDL + 4, VR + BMP); vw(VDL + 8, 0); vw(VDL + 12, 0);
    vw(VDL + 16, 0xe0112233u);          /* BACKGROUND: R=0x11 G=0x22 B=0x33 */
    vw(VDL + 20, 0x01ff8040u);          /* CLUT[1]: R=ff G=80 B=40 */
    vw(VDL + 24, 0xc2000000u);          /* OUTCONTROLL com bit 0x2000000 (pixel direto por bit 15) */
    put(BMP, 320, 0, 0, 0x0421);        /* CLUT[1] nos 3 canais */
    put(BMP, 320, 0, 1, 0x0000);        /* zero = fundo */
    put(BMP, 320, 0, 2, 0xfc00);        /* direto: R=f8 G=0 B=0 */
    put(BMP, 320, 1, 0, 0x0421);        /* linha impar (metade baixa) */
    put(BMP, 320, 2, 5, 0x0421);        /* 2o par de linhas (+largura*4) */
    vdlp_set_head(&v, VR + VDL);
    run_frame(&v);
    CHK(v.width_sel == 320 && v.curbmp != 0, "largura 320 lida do CLUTDMA, CURRENTBMP carregado");
    vdlp_frame_end(&v);
    CHK(RGB(&v, 0, 0, 0xff, 0x80, 0x40), "pixel por CLUT (linha 0)");
    CHK(RGB(&v, 1, 0, 0x11, 0x22, 0x33), "pixel 0 = BACKGROUND");
    CHK(RGB(&v, 2, 0, 0xf8, 0x00, 0x00), "pixel direto (bit 15) quando OUTCONTROLL bit 25");
    CHK(RGB(&v, 0, 1, 0xff, 0x80, 0x40), "linha impar usa a outra metade da palavra");
    CHK(RGB(&v, 5, 2, 0xff, 0x80, 0x40), "avanco do bitmap: 3a linha = proximo par (+largura*4)");
    CHK(RGB(&v, 6, 2, 0x11, 0x22, 0x33), "demais pixels = fundo");
    CHK(nbg == 1 && bgc[0] == 0x11 && bgc[1] == 0x22 && bgc[2] == 0x33, "set_background recebe R,G,B");
    CHK(nblit >= 1 && npres == 1, "blit + present no fim do frame");
    int b0 = nblit; run_frame(&v); vdlp_frame_end(&v);
    CHK(nblit == b0 && npres == 1, "frame sem mudanca: nada de blit/present (so linhas sujas)");
    vdlp_free(&v);

    /* 3) sem EnDMA: linha inteira = fundo; VDL em duas entradas (proximo absoluto) */
    memset(vr, 0, sizeof vr); nblit = npres = 0;
    vdlp_init(&v, 0, vr);
    vw(0x100, (100u + 16) | (1u << 9));        vw(0x104, 0); vw(0x108, 0); vw(0x10c, VR + 0x200);  vw(0x110, 0xe0010203u);
    vw(0x200, (140u + 16) | (1u << 9));        vw(0x204, 0); vw(0x208, 0); vw(0x20c, 0);           vw(0x210, 0xe0040506u);
    vdlp_set_head(&v, VR + 0x100); run_frame(&v); vdlp_frame_end(&v);
    CHK(RGB(&v, 7, 99, 1, 2, 3), "linha 99 = fundo da 1a entrada");
    CHK(RGB(&v, 7, 100, 4, 5, 6), "linha 100 = fundo da 2a entrada (proximo VDL absoluto)");
    CHK(v.linedelay < 0x200, "contador de linhas continua valido");
    vdlp_free(&v);

    /* 4) proximo VDL relativo (bit 18): next = valor + cur + 0x10 */
    memset(vr, 0, sizeof vr);
    vdlp_init(&v, 0, vr);
    vw(0x100, (50u + 16) | 0x40000u);          vw(0x104, 0); vw(0x108, 0); vw(0x10c, 0x30);        /* 0x30 + 0x100 + 0x10 = 0x140 */
    vw(0x140, (190u + 16) | (1u << 9));        vw(0x144, 0); vw(0x148, 0); vw(0x14c, 0);           vw(0x150, 0xe0aabbccu);
    vdlp_set_head(&v, VR + 0x100); run_frame(&v); vdlp_frame_end(&v);
    CHK(RGB(&v, 0, 49, 0, 0, 0) && RGB(&v, 0, 50, 0xaa, 0xbb, 0xcc), "VDL relativo: troca na linha 50");
    vdlp_free(&v);

    /* 5) CLUT parcial (so um canal) e rampa 0xffffffff */
    memset(vr, 0, sizeof vr);
    vdlp_init(&v, 0, vr);
    vw(0x100, (240u + 16) | (3u << 9) | 0x10000u | 0x200000u); vw(0x104, VR + BMP);
    vw(0x110, 0x01010203u);             /* CLUT[1] = (1,2,3) */
    vw(0x114, 0x61aa0000u);             /* so R[1] = 0xaa  (sel 0x60000000) */
    vw(0x118, 0x41000500u);             /* so G[1] = 0x05  (sel 0x40000000) */
    put(BMP, 320, 0, 0, 0x0421);
    vdlp_set_head(&v, VR + 0x100); run_frame(&v); vdlp_frame_end(&v);
    CHK(RGB(&v, 0, 0, 0xaa, 0x05, 0x03), "palavras de cor por canal (R/G/B separados)");
    vdlp_free(&v);
    memset(vr, 0, sizeof vr);
    vdlp_init(&v, 0, vr);
    vw(0x100, (240u + 16) | (2u << 9) | 0x10000u | 0x200000u); vw(0x104, VR + BMP);
    vw(0x110, 0x01010101u); vw(0x114, 0xffffffffu);     /* depois da rampa, CLUT[1] volta a (8,8,8) */
    put(BMP, 320, 0, 0, 0x0421);
    vdlp_set_head(&v, VR + 0x100); run_frame(&v); vdlp_frame_end(&v);
    CHK(RGB(&v, 0, 0, 8, 8, 8), "0xffffffff restaura a rampa de cinza");
    vdlp_free(&v);

    /* 6) largura 384 num NTSC 320: corta no centro; largura 320 num PAL 384: bordas de fundo */
    memset(vr, 0, sizeof vr);
    vdlp_init(&v, 0, vr);
    vw(0x100, (240u + 16) | (2u << 9) | 0x10000u | 0x200000u | (1u << 23)); vw(0x104, VR + BMP);     /* tabela[1] = 384 */
    vw(0x110, 0xe0000000u); vw(0x114, 0x01ff0000u);
    put(BMP, 384, 0, 32, 0x0421);       /* origem x=32 aparece em x=0 */
    put(BMP, 384, 0, 31, 0x0421);       /* fora do recorte */
    vdlp_set_head(&v, VR + 0x100); run_frame(&v); vdlp_frame_end(&v);
    CHK(v.width_sel == 384, "tabela de largura: indice 1 = 384");
    CHK(RGB(&v, 0, 0, 0xff, 0, 0), "recorte central: pixel de origem x=32 vai para x=0");
    vdlp_free(&v);
    memset(vr, 0, sizeof vr);
    vdlp_init(&v, 1, vr); LEAD = 0x11;
    vw(0x100, (288u + 17) | (2u << 9) | 0x10000u | 0x200000u); vw(0x104, VR + BMP);                   /* largura 320 */
    vw(0x110, 0xe0102030u); vw(0x114, 0x01ff0000u);
    put(BMP, 320, 0, 0, 0x0421);
    vdlp_set_head(&v, VR + 0x100); run_frame(&v); vdlp_frame_end(&v);
    CHK(RGB(&v, 0, 0, 0x10, 0x20, 0x30) && RGB(&v, 31, 0, 0x10, 0x20, 0x30), "borda esquerda (32 px) = fundo");
    CHK(RGB(&v, 32, 0, 0xff, 0, 0), "imagem centrada em x=32");
    LEAD = 0x10;
    CHK(RGB(&v, 383, 0, 0x10, 0x20, 0x30), "borda direita = fundo");
    vdlp_free(&v);

    /* 7) VDL invalida (fora da VRAM / ctl=0): sem travar, frame de fundo preto */
    memset(vr, 0, sizeof vr);
    vdlp_init(&v, 0, vr);
    vdlp_set_head(&v, 0x1000); run_frame(&v); vdlp_frame_end(&v);
    CHK(RGB(&v, 10, 10, 0, 0, 0) && v.linedelay > 0, "VDL fora da VRAM: linedelay=0x1ff, imagem preta");
    vdlp_free(&v);

    /* 8) maquina completa: MADAM 0x580 -> HEADVDL, tick do CLIO -> linhas -> frame; NTSC e PAL */
    ph_machine_t m; if (ph_init(&m)) return 2;
    nblit = npres = 0;
    m.vdlp.blit = t_blit; m.vdlp.present = t_pres;
    ph_write32(&m, VR + VDL, (240u + 16) | (2u << 9) | 0x10000u | 0x200000u); ph_write32(&m, VR + VDL + 4, VR + BMP);
    ph_write32(&m, VR + VDL + 16, 0xe0204060u); ph_write32(&m, VR + VDL + 20, 0x01ff8040u);
    { uint32_t b = BMP + (uint32_t)(LEAD >> 1) * 320u * 4u; ph_write32(&m, VR + b, 0x04210000u); }   /* linha visivel 0 = linha 0x10 da VDL (par 8): pixel 0 = metade alta */
    ph_write32(&m, 0x3300580, VR + VDL);
    CHK(m.vdlp.headvdl == VR + VDL, "MADAM 0x580 grava HEADVDL");
    for (int i = 0; i < 40 && npres == 0; i++) clio_tick(&m.clio, 100000);
    CHK(npres >= 1, "tick do CLIO produz um frame apresentado");
    CHK(RGB(&m.vdlp, 0, 0, 0xff, 0x80, 0x40), "pixel da maquina chegou ao frame RGB");
    CHK(RGB(&m.vdlp, 1, 0, 0x20, 0x40, 0x60), "fundo da maquina");
    CHK(ph_set_pal(&m, 1) == 0 && m.vdlp.width == 384 && m.vdlp.lines == 288 && m.clio.pal == 1, "ph_set_pal(1): 384x288 e CLIO em PAL");
    CHK(m.vdlp.headvdl == VR + VDL && m.vdlp.present == t_pres, "ph_set_pal preserva HEADVDL e ganchos");
    npres = 0; for (int i = 0; i < 40 && npres == 0; i++) clio_tick(&m.clio, 100000);
    CHK(npres >= 1, "PAL: tambem apresenta frames");
    ph_free(&m);

    puts(fails ? "ERRO" : "OK"); return fails != 0;
}
