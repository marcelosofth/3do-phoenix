#include "cel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
static uint32_t *ram;
static void w32(uint32_t a, uint32_t v) { ram[a >> 2] = v; }
static void w16(uint32_t a, uint16_t v) { ((uint16_t *)ram)[(a ^ 2) >> 1] = v; }
static void w8(uint32_t a, uint8_t v) { ((uint8_t *)ram)[a ^ 3] = v; }
/* monta um fluxo de bits MSB-first */
static unsigned bits_[256]; static int nbits_;
static void B(uint32_t v, int k) { for (int i = k - 1; i >= 0; i--) bits_[nbits_++] = (v >> i) & 1; }
static void flush(uint32_t addr) {
    int nbytes = (nbits_ + 7) / 8;
    for (int i = 0; i < nbytes; i++) w8(addr + i, 0);
    for (int i = 0; i < nbits_; i++) w8(addr + i / 8, ((uint8_t *)ram)[(addr + i / 8) ^ 3] | (bits_[i] << (7 - i % 8)));
    nbits_ = 0;
}

int main(void) {
    ram = calloc(CEL_RAM_BYTES / 4, 4);
    /* --- CCB 1 @0x1000: ponteiros relativos, LDSIZE|LDPRS|LDPPMP|LDPLUT|YOXY|CCBPRE|ACW|PACKED, 4 bpp --- */
    uint32_t f1 = CCB_LDSIZE | CCB_LDPRS | CCB_LDPPMP | CCB_LDPLUT | CCB_YOXY | CCB_CCBPRE | CCB_ACW | CCB_PACKED;
    w32(0x1000, f1);
    w32(0x1004, 0x3000 - 0x1008);          /* next = 0x3000 */
    w32(0x1008, 0x4000 - 0x100c);          /* dados = 0x4000 */
    w32(0x100c, 0x2000 - 0x1010);          /* PLUT = 0x2000 */
    w32(0x1010, 0x00100000); w32(0x1014, 0xfff00000);   /* xpos, ypos */
    w32(0x1018, 0x00010000); w32(0x101c, 0x00020000); w32(0x1020, 0x00030000); w32(0x1024, 0x00040000);
    w32(0x1028, 0x11); w32(0x102c, 0x22);
    w32(0x1030, 0xdeadbeef);               /* PPMP */
    w32(0x1034, (1u << 6) | 3);            /* PRE0: 4 bpp, 2 linhas; PACKED => sem PRE1 */
    for (int i = 0; i < 16; i++) w16(0x2000 + 2 * i, 0x1000 + i * 0x21);
    cel_ccb_t c; cel_ccb_reset(&c); c.next = 0x1000;
    int r = cel_ccb_read(&c, ram);
    CHECK(r == 1);
    CHECK(c.next == 0x3000 && c.src == 0x4000 && c.plutp == 0x2000);
    CHECK(c.xpos == 0x00100000 && c.ypos == (int32_t)0xfff00000);
    CHECK(c.hdx == 0x10000 && c.hdy == 0x20000 && c.vdx == 0x30000 && c.vdy == 0x40000);
    CHECK(c.hddx == 0x11 && c.hddy == 0x22 && c.ppmp == 0xdeadbeef);
    CHECK(c.pre0 == ((1u << 6) | 3) && c.pc == 0x1038);
    CHECK(c.plut32[0] == 0x1000 && c.plut32[15] == 0x1000 + 15 * 0x21 && c.plut16[3] == 0x1000 + 3 * 0x21);

    /* linha 0 (12 bytes): off=1 | literal 4 px (1,2,3,4) | repeticao 3x idx 7 | EOL */
    B(1, 8); B(1, 2); B(3, 6); B(0x1234, 16); B(3, 2); B(2, 6); B(7, 4); B(0, 2); B(0, 6);
    flush(0x4000);
    /* linha 1 (8 bytes): off=0 | transparente 2 | EOL */
    B(0, 8); B(2, 2); B(1, 6); B(0, 2); B(0, 6);
    flush(0x400c);

    static uint32_t out[2 * 0x800];
    int rows; int mw = cel_decode_packed(&c, ram, out, &rows);
    CHECK(rows == 2 && mw == 7);
    CHECK(out[0] == cel_index_to_color(&c, 1) && out[3] == cel_index_to_color(&c, 4));
    CHECK(out[4] == cel_index_to_color(&c, 7) && out[6] == cel_index_to_color(&c, 7));
    CHECK(out[7] == 0x80000000u);
    CHECK(out[0x800] == 0x80000000u && out[0x801] == 0x80000000u && out[0x802] == 0x80000000u);
    CHECK(out[0] == c.plut16[1]);

    /* --- CCB 2 @0x3000: absoluto, LAST, SKIP => le, nao desenha, next=0; PRE0/PRE1 nos dados --- */
    w32(0x3000, CCB_SKIP | CCB_LAST | CCB_NPABS | CCB_SPABS | CCB_ACW);
    w32(0x3004, 0x5000); w32(0x3008, 0x6000);
    w32(0x6000, 3); w32(0x6004, 0x0);
    r = cel_ccb_read(&c, ram);
    CHECK(r == 0 && c.next == 0 && c.src == 0x6008 && c.pre0 == 3 && c.pre1 == 0);
    r = cel_ccb_read(&c, ram);
    CHECK(r == -1);

    /* geometria */
    cel_ccb_t g; cel_ccb_reset(&g); g.pre0 = (9u << 6) | 3; g.pre1 = (5u << 24) | 0x1f;
    cel_geom_t gg = cel_geometry(&g);
    CHECK(gg.width == 32 && gg.height == 10 && gg.stride_bytes == 28 && gg.bpp == 4);
    g.pre1 |= 0x800; CHECK(cel_geometry(&g).height == 20);
    g.pre0 = 6; g.pre1 = (7u << 16) | 3; gg = cel_geometry(&g); CHECK(gg.bpp == 16 && gg.stride_bytes == 36 && gg.width == 4);

    /* conversao de cor */
    cel_ccb_t k; cel_ccb_reset(&k); k.pre0 = 6;
    for (int i = 0; i < 32; i++) k.plut16[i] = (uint16_t)(i * 3);
    CHECK(cel_index_to_color(&k, 0x8005) == (0x8000 | 15));
    CHECK(cel_index_to_color(&k, 0) == 0x80000000u);
    k.flags = CCB_BGND; CHECK(cel_index_to_color(&k, 0) == 0);
    k.pre0 = 6 | 0x10; k.flags = 0; CHECK(cel_index_to_color(&k, 0x1234) == 0x1234);
    k.pre0 = 4; CHECK(cel_index_to_color(&k, 0x21) == (0x8000 + 3));

    /* ---- Parte 15 ---- */
    cel_ccb_t q; cel_ccb_reset(&q);
    q.hdx = 0x100000; q.hdy = 0; q.vdx = 0; q.vdy = 0x10000;            /* h=(1,0) v=(0,1): cross = vdx*hdy-hdx*vdy = -1 */
    CHECK(cel_cross(&q) == -1.0 && cel_winding(&q) == CEL_WIND_CCW && cel_is_unit_blit(&q));
    q.flags = CCB_ACCW; CHECK(cel_culled(&q) == 1);                       /* bit == enrolamento => descartado */
    q.flags = CCB_ACW;  CHECK(cel_culled(&q) == 0);
    q.flags = CCB_ACW | CCB_ACCW; CHECK(cel_culled(&q) == 0);
    q.vdy = -0x10000;   CHECK(cel_cross(&q) == 1.0 && cel_winding(&q) == CEL_WIND_CW);
    q.flags = CCB_ACW;  CHECK(cel_culled(&q) == 1);
    q.hdx = 0x100001;   CHECK(!cel_is_unit_blit(&q));
    q.hdx = 0; q.hdy = -0x100000; q.vdx = -0x10000; q.vdy = 0; CHECK(cel_is_unit_blit(&q));   /* rotacao 90 */
    CHECK(cel_fb_addr(0x100000, 0x500, 3, 0) == 0x10000c && cel_fb_addr(0x100000, 0x500, 3, 1) == 0x10000e);
    CHECK(cel_fb_addr(0x100000, 0x500, 0, 5) == 0x100000 + 2 * 0x500 + 2);
    cel_fb_write16(ram, 0x10000e, 0xbeef); CHECK(cel_fb_read16(ram, 0x10000e) == 0xbeef);
    cel_fb_write16(ram, 0x400000, 1); CHECK(cel_fb_read16(ram, 0x400000) == 0);

    /* ---- Parte 16: cel_pixel (FUN_0047d0d0) -- valores calculados a mao a partir da decompilacao ---- */
    /* PPMP 0x1F00 = multiplicador 8 (bits 12:10 = 7) com divisor 2^3 (bits 9:8 = 3) => identidade; 0x0000 => multiplicador 1 >> 4 = 0 (preto) */
    {
        cel_pix_t p = { 0, 0x1F00, 0x0000, 0, 1, 0, 0, 0 };
        CHECK(cel_pixel(&p, 0x2B5B, 0x1111) == 0x2B5A);                 /* identidade; mode 0 zera o bit 0 */
        p.pixc = 0x10000000u; CHECK(cel_pixel(&p, 0x2B5B, 0) == 0x2B5B); /* mode 1 forca o bit 0 */
        p.pixc = 0x40000000u; CHECK(cel_pixel(&p, 0x2B5B, 0) == 0xAB5A); /* bits 31:30 = 1 forca o bit 15 */
        p.pixc = 0;
        /* escolha da metade do PPMP */
        p.ppmp0 = 0; p.ppmp1 = 0x1F00; p.flags = 0x100 | 0x80; CHECK(cel_pixel(&p, 0x2B5B, 0) == 0x2B5A);
        p.flags = 0x100;        CHECK(cel_pixel(&p, 0x2B5B, 0) == 0x20);    /* metade 0: k=1, divisor 2^4: (27,26,10)>>4 = (1,1,0) */
        p.flags = 0x100;        CHECK(cel_pixel(&p, 0x3DEF, 0) == 0x400);   /* todos os canais 15 -> 0 => vira 0x400 (sem NOBLK) */
        p.flags = 0x100 | 0x10; CHECK(cel_pixel(&p, 0x3DEF, 0) == 0);       /* NOBLK desliga a troca */
        p.flags = 0;            CHECK(cel_pixel(&p, 0xAB5B, 0) == 0x2B5A);  /* bit 15 da origem escolhe a metade alta */
        p.flags = 0;            CHECK(cel_pixel(&p, 0x3DEF, 0) == 0x400);   /* ... e bit 15 = 0 escolhe a baixa (preto) */
        /* halve + multiplicador constante */
        p.ppmp0 = 0x1F01; p.ppmp1 = 0x1F01; CHECK(cel_pixel(&p, 0x7A8A, 0) == 0x3D44);   /* (10,20,30)/2 = (5,10,15) */
        /* soma de constante (ss=1, constante = bits 5:1) com saturacao */
        p.ppmp0 = 0x1F46; p.ppmp1 = 0x1F46; CHECK(cel_pixel(&p, 0x7A8A, 0) == 0x7EEC);   /* +3: (13,23,31) */
        p.ppmp0 = 0x1F7E; p.ppmp1 = 0x1F7E; CHECK(cel_pixel(&p, 0x7A8A, 0) == 0x7FFE);   /* +31: tudo satura */
        p.ppmp0 = 0x1F46; p.ppmp1 = 0x1F46; p.flags = 0x800; CHECK(cel_pixel(&p, 0x7A8A, 0) == 0x76E8);  /* XOR 3: (9,23,29) */
        /* USEAV (0x400) com ss=3 (2o operando = a propria origem) */
        p.flags = 0x400;
        p.ppmp0 = p.ppmp1 = 0x1FC0; CHECK(cel_pixel(&p, 0x7A8A, 0) == 0x7FF4);          /* soma: (20,31,31) */
        p.ppmp0 = p.ppmp1 = 0x1FC2; CHECK(cel_pixel(&p, 0x7A8A, 0) == 0x400);           /* subtrai: 0 => 0x400 */
        p.ppmp0 = p.ppmp1 = 0x1FC8; CHECK(cel_pixel(&p, 0x7A8A, 0) == 0x7114);          /* bit 3: mascara 5 bits sem saturar (20,8,28) */
        p.ppmp0 = p.ppmp1 = 0x1FC4; CHECK(cel_pixel(&p, 0x7A8A, 0) == 0x7114);          /* bit 2: operando com sinal (10,-12,-2) */
        p.ppmp0 = p.ppmp1 = 0x1FC5; CHECK(cel_pixel(&p, 0x7A8A, 0) == 0x388A);          /* + halve: (10,4,14) */
        /* ss=2 (operando = destino) subtraindo, com piso em 0 */
        p.ppmp0 = p.ppmp1 = 0x1F82; CHECK(cel_pixel(&p, 0x7A8A, 0x172C) == 0x6400);     /* (10-12, 20-25, 30-5) -> (0,0,25) */
        p.flags = 0;
        /* multiplicador vindo dos bits 16.. da origem (mf=1) */
        p.ppmp0 = p.ppmp1 = 0x2300; CHECK(cel_pixel(&p, 0x003F7A8Au, 0) == 0x0E8A);    /* k=(8,8,1) dv 3 -> (10,20,3) */
        /* multiplicador dos bits 2/7/12 da origem: mf=3 (divisor comum) e mf=2 (divisor por canal) */
        p.ppmp0 = p.ppmp1 = 0x6300; CHECK(cel_pixel(&p, 0x7A8A, 0) == 0x79E2);
        p.ppmp0 = p.ppmp1 = 0x4000; CHECK(cel_pixel(&p, 0x7A8A, 0) == 0x7CE6);
        /* bit 15 da cor multiplicada = destino (ms) */
        p.ppmp0 = p.ppmp1 = 0x9F00; CHECK(cel_pixel(&p, 0x7A8A, 0x172C) == 0x172C);
        /* bit 0 da origem (mode_f8) e do destino (pixc>>22); mode 2 deixa o bit 0 como saiu */
        p.ppmp0 = p.ppmp1 = 0x1F00; p.pixc = 0x20000000u;
        p.mode_f8 = 0; CHECK(cel_pixel(&p, 0x2B7F, 0) == 0x2B7E);
        p.mode_f8 = 1; CHECK(cel_pixel(&p, 0x2B7F, 0) == 0x2B7F);
        p.mode_f8 = 2; CHECK(cel_pixel(&p, 0x2B6E, 0) == 0x2B6E && cel_pixel(&p, 0x2B7E, 0) == 0x2B7F);   /* bit 4 */
        p.mode_f8 = 3; CHECK(cel_pixel(&p, 0x2B6E, 0) == 0x2B6F && cel_pixel(&p, 0x2B5E, 0) == 0x2B5E);   /* bit 5 */
        p.ppmp0 = p.ppmp1 = 0x9F00; p.mode_f8 = 1;
        p.pixc = 0x20C00000u; CHECK(cel_pixel(&p, 0, 0x172C) == 0x172D);   /* dsel 3: bit 5 do destino */
        p.pixc = 0x20800000u; CHECK(cel_pixel(&p, 0, 0x172C) == 0x172C);   /* dsel 2: bit 4 (=0) */
        p.pixc = 0x20400000u; CHECK(cel_pixel(&p, 0, 0x172D) == 0x172D);   /* dsel 1: inalterado */
        p.pixc = 0x20000000u; CHECK(cel_pixel(&p, 0, 0x172D) == 0x172C);   /* dsel 0: bit 0 = 0 */
        /* bit 15 do resultado (bits 31:30 >= 2) conforme mode_fc; note a troca xpos/ypos entre os dois ramos */
        p.ppmp0 = p.ppmp1 = 0x1F00; p.pixc = 0x80000000u; p.xpos = 0x8000; p.ypos = 0;
        p.mode_fc = 1; CHECK(cel_pixel(&p, 0xAB5B, 0) == 0xAB5A);
        p.mode_fc = 3; CHECK(cel_pixel(&p, 0x2B5B, 0) == 0xAB5A && cel_pixel(&p, 0x2B5A, 0) == 0x2B5A);
        p.mode_fc = 2; CHECK(cel_pixel(&p, 0x2B5B, 0) == 0xAB5A);          /* 2 -> xpos */
        p.mode_fc = 0; CHECK(cel_pixel(&p, 0x2B5B, 0) == 0x2B5A);          /* 0 -> ypos */
        p.pixc = 0x81000000u; p.ppmp0 = p.ppmp1 = 0x9F00; p.mode_fc = 1;    /* bit 24 + ms: bit 15 vem do destino */
        CHECK(cel_pixel(&p, 0x2B5B, 0x8172) == 0x8172);
        /* mode 3 (pixc 31..28 = x011): bit 0 do resultado */
        p.ppmp0 = p.ppmp1 = 0x1F00; p.pixc = 0x30000000u; p.mode_f8 = 0; p.xpos = 0; p.ypos = 0;
        p.mode_fc = 1; CHECK(cel_pixel(&p, 0x2B5B, 0) == 0x2B5B && cel_pixel(&p, 0x2B5A, 0) == 0x2B5A);
        p.mode_fc = 3; CHECK(cel_pixel(&p, 0xAB5B, 0) == 0x2B5B && cel_pixel(&p, 0x2B5B, 0) == 0x2B5A);
        p.mode_fc = 2; p.ypos = 0x8000; CHECK(cel_pixel(&p, 0x2B5B, 0) == 0x2B5B);   /* 2 -> ypos */
        p.mode_fc = 0; p.xpos = 0x8000; p.ypos = 0; CHECK(cel_pixel(&p, 0x2B5B, 0) == 0x2B5B);   /* 0 -> xpos */
        p.xpos = 0;                     CHECK(cel_pixel(&p, 0x2B5B, 0) == 0x2B5A);
        /* cel_pix_setup (FUN_00488700): mode_f8 / mode_fc */
        cel_ccb_t cc; cel_ccb_reset(&cc);
        cc.flags = CCB_PACKED | 0x40; cc.ppmp = 0x1F001234u; cc.xpos = 7; cc.ypos = -9; cel_pix_setup(&p, &cc, 0x08000000u);
        CHECK(p.ppmp0 == 0x1234 && p.ppmp1 == 0x1F00 && p.pixc == 0x08000000u && p.mode_f8 == 0 && p.mode_fc == 3 && p.xpos == 7 && p.ypos == -9);
        cc.flags = CCB_PACKED; cel_pix_setup(&p, &cc, 0x00300000u); CHECK(p.mode_f8 == 3 && p.mode_fc == 0);        /* empacotado: f8 = pixc>>20&3 */
        cc.flags = 0x40; cc.pre1 = 0x3000; cel_pix_setup(&p, &cc, 0x08000000u); CHECK(p.mode_f8 == 3 && p.mode_fc == 3);
        cc.pre1 = 0x5000;               cel_pix_setup(&p, &cc, 0x08000000u); CHECK(p.mode_f8 == 1 && p.mode_fc == 1);  /* PRE1 bit 14 bloqueia o bit 1 */
    }

    /* ---- Parte 17: cel_blit11 (caminho 1 de FUN_00480290) ---- */
    {
        memset(ram, 0, CEL_RAM_BYTES);
        const uint32_t FB = 0x100000, STEP = 0x200, SRC = 0x2000;
        const int TW = 4, TH = 2;
        /* textura 16 bpp linear, 4x2: v(tx,ty) = 0x0100 + ty*0x40 + tx*2 ; passo (6*4+8) = 32 bytes por par de linhas */
        #define TV(tx, ty) ((uint16_t)(0x0100 + (ty) * 0x40 + (tx) * 2))
        for (int ty = 0; ty < TH; ty++) for (int tx = 0; tx < TW; tx++) cel_fb_write16(ram, SRC + ty * 32 + tx * 2, TV(tx, ty));   /* fonte CRUA = linear (so PRE1 bit 11 entrelaca) */
        cel_ccb_t c; cel_ccb_reset(&c);
        c.flags = CCB_ACW | CCB_ACCW | CCB_BGND; c.pre0 = 6 | 0x10 | ((TH - 1) << 6); c.pre1 = (6u << 16) | (TW - 1); c.src = SRC;
        c.hdx = 0x100000; c.hdy = 0; c.vdx = 0; c.vdy = 0x10000; c.xpos = 10 << 16; c.ypos = 4 << 16; c.ppmp = 0x1F001F00u;
        cel_raster_t r; cel_raster_setup(&r, &c, 0x003f003fu, FB, FB, STEP, STEP);
        CHECK(r.width == TW && r.height == TH && r.clip_x == 63 && r.clip_y == 63 && r.src == SRC);
        cel_pix_t px; cel_pix_setup(&px, &c, 0);
        #define DST(x, y) cel_fb_read16(ram, FB + STEP * ((y) >> 1) + ((y) & 1) * 2 + (x) * 4)
        #define EXP(tx, ty) ((uint16_t)(TV(tx, ty) & 0x7ffe))          /* pipeline identidade: &0x7ffe */
        #define CLEARFB() do { for (int yy = 0; yy < 12; yy++) for (int xx = 0; xx < 20; xx++) cel_fb_write16(ram, FB + STEP * (yy >> 1) + (yy & 1) * 2 + xx * 4, 0x1234); } while (0)
        /* 1) normal */
        CLEARFB(); CHECK(cel_blit11(&c, &px, &r, ram) == 1);
        { int ok = 1; for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) ok &= DST(10 + x, 4 + y) == EXP(x, y);
          CHECK(ok); CHECK(DST(9, 4) == 0x1234 && DST(14, 4) == 0x1234 && DST(10, 3) == 0x1234 && DST(10, 6) == 0x1234); }
        /* 2) espelho horizontal: hdx = -1, texel tx cai em x = xpos - 1 - tx */
        c.hdx = -0x100000; CLEARFB(); CHECK(cel_blit11(&c, &px, &r, ram) == 1);
        { int ok = 1; for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) ok &= DST(10 - 1 - x, 4 + y) == EXP(x, y);
          CHECK(ok); CHECK(DST(10, 4) == 0x1234 && DST(5, 4) == 0x1234); }
        c.hdx = 0x100000;
        /* 3) espelho vertical: vdy = -1, ypos = 5 -> texel ty cai em y = 5 - 1 - ty */
        c.vdy = -0x10000; c.ypos = 5 << 16; CLEARFB(); CHECK(cel_blit11(&c, &px, &r, ram) == 1);
        { int ok = 1; for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) ok &= DST(10 + x, 5 - 1 - y) == EXP(x, y);
          CHECK(ok); CHECK(DST(10, 5) == 0x1234 && DST(10, 2) == 0x1234); }
        c.vdy = 0x10000; c.ypos = 4 << 16;
        /* 4) rotacao de 90 graus: hdx=0, hdy=+1, vdx=-1, vdy=0 -> dest (x,y) = (xpos - 1 - ty, ypos + tx) */
        c.hdx = 0; c.hdy = 0x100000; c.vdx = -0x10000; c.vdy = 0; c.xpos = 10 << 16; CLEARFB(); CHECK(cel_blit11(&c, &px, &r, ram) == 1);
        { int ok = 1; for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) ok &= DST(10 - 1 - y, 4 + x) == EXP(x, y);
          CHECK(ok); CHECK(DST(10, 4) == 0x1234 && DST(7, 4) == 0x1234 && DST(9, 8) == 0x1234); }
        c.hdx = 0x100000; c.hdy = 0; c.vdx = 0; c.vdy = 0x10000;
        /* 5) recorte inclusivo (x <= 11, y <= 4): so (10,4) e (11,4) */
        r.clip_x = 11; r.clip_y = 4; CLEARFB(); CHECK(cel_blit11(&c, &px, &r, ram) == 1);
        CHECK(DST(10, 4) == EXP(0, 0) && DST(11, 4) == EXP(1, 0) && DST(12, 4) == 0x1234 && DST(10, 5) == 0x1234 && DST(11, 5) == 0x1234);
        r.clip_x = 63; r.clip_y = 63;
        /* 5b) fora da tela a esquerda/acima: xpos = -2 -> x in [-2, 2) so 0 e 1 aparecem, texel 2 e 3 */
        c.xpos = -2 * 65536; c.ypos = -1 * 65536; CLEARFB(); CHECK(cel_blit11(&c, &px, &r, ram) == 1);
        CHECK(DST(0, 0) == EXP(2, 1) && DST(1, 0) == EXP(3, 1) && DST(2, 0) == 0x1234 && DST(0, 1) == 0x1234);
        c.xpos = 10 << 16; c.ypos = 4 << 16;
        /* 6) texel transparente (valor 0 sem BGND) e CEL fora de caminho */
        cel_fb_write16(ram, SRC + 2, 0); c.flags = CCB_ACW | CCB_ACCW;                 /* tx=1,ty=0 */
        CLEARFB(); cel_pix_setup(&px, &c, 0); CHECK(cel_blit11(&c, &px, &r, ram) == 1);
        CHECK(DST(11, 4) == 0x1234 && DST(10, 4) == EXP(0, 0) && DST(12, 4) == EXP(2, 0));
        cel_fb_write16(ram, SRC + 2, TV(1, 0)); c.flags |= CCB_BGND; cel_pix_setup(&px, &c, 0);
        c.hdx = 0x100001; CHECK(cel_blit11(&c, &px, &r, ram) == 0); c.hdx = 0x100000;  /* escala != 1 */
        c.hddx = 1; CHECK(cel_blit11(&c, &px, &r, ram) == 0); c.hddx = 0;
        CLEARFB(); c.flags = CCB_ACCW | CCB_BGND; cel_pix_setup(&px, &c, 0);          /* cross<0 e ACCW: descartado */
        CHECK(cel_blit11(&c, &px, &r, ram) == 1 && DST(10, 4) == 0x1234);
        c.flags = CCB_ACW | CCB_ACCW | CCB_BGND; cel_pix_setup(&px, &c, 0);
        c.pre0 |= (uint32_t)4 << 24; CLEARFB();                                        /* start_col = 4 >= largura: nada */
        CHECK(cel_blit11(&c, &px, &r, ram) == 1 && DST(10, 4) == 0x1234); c.pre0 &= ~(0xfu << 24);
        /* 7) o pipeline le o destino: PPMP 0x1F80 (ss=2, soma) com destino (4,6,7)+(1,2,3) -> 0x2904 */
        c.ppmp = 0x1F801F80u; cel_pix_setup(&px, &c, 0);
        cel_fb_write16(ram, SRC, 0x0C41); CLEARFB(); cel_fb_write16(ram, FB + STEP * 2 + 10 * 4, 0x1CC4);   /* dest (10,4) */
        CHECK(cel_blit11(&c, &px, &r, ram) == 1 && DST(10, 4) == 0x2904);
        c.ppmp = 0x1F001F00u; cel_pix_setup(&px, &c, 0);
        cel_fb_write16(ram, SRC, TV(0, 0));
        /* 8) textura decodificada compacta (largura 4): convertida -> grava direto; nao convertida -> pipeline */
        uint32_t td[8]; for (int ty = 0; ty < TH; ty++) for (int tx = 0; tx < TW; tx++) td[ty * TW + tx] = 0x4000 + ty * 0x10 + tx;
        td[1 * TW + 2] = 0x80000000u;                                                  /* transparente */
        cel_tex_t tx_ = { TW, TH, 1, td }; r.tex = &tx_;
        CLEARFB(); CHECK(cel_blit11(&c, &px, &r, ram) == 1);
        CHECK(DST(10, 4) == 0x4000 && DST(13, 4) == 0x4003 && DST(11, 5) == 0x4011 && DST(12, 5) == 0x1234);
        tx_.converted = 0; CLEARFB(); CHECK(cel_blit11(&c, &px, &r, ram) == 1);
        CHECK(DST(10, 4) == (0x4000 & 0x7ffe) && DST(13, 4) == (0x4003 & 0x7ffe) && DST(12, 5) == 0x1234);
        r.tex_x = 1; r.tex_y = 0; CLEARFB();                                           /* origem dentro da textura: fora do vetor => ignora */
        CHECK(cel_blit11(&c, &px, &r, ram) == 1 && DST(10, 4) == (0x4001 & 0x7ffe));
        #undef TV
        #undef DST
        #undef EXP
        #undef CLEARFB
    }
    printf("test_cel: %d verificacoes, %d falhas\n", checks, fails);
    free(ram);
    return fails != 0;
}
