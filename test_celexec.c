/* test_celexec.c - motor de CEL ligado ao MADAM: pokes de registradores + cadeia de CCBs -> pixels na RAM. */
#include "machine.h"
#include <stdio.h>
#include <string.h>
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static ph_machine_t M;
#define WR(a, v) ph_write32(&M, (a), (v))
#define FB 0x100000u
#define STEP 0x500u            /* 320 pixels x 4 bytes */
static uint16_t dst(int x, int y) { return cel_fb_read16(M.ram, FB + STEP * (y >> 1) + (y & 1) * 2 + x * 4); }

/* CCB (24 palavras max): flags, next, src, plut, xpos, ypos, hdx, hdy, vdx, vdy, ppmp, pre0, pre1 (CCBPRE) */
static void ccb(uint32_t a, uint32_t flags, uint32_t next, uint32_t src, int x, int y, uint32_t pre0, uint32_t pre1) {
    uint32_t w[] = { flags, next, src, 0, (uint32_t)x << 16, (uint32_t)y << 16, 0x100000, 0, 0, 0x10000, 0x1F001F00u, pre0, pre1 };
    for (unsigned i = 0; i < sizeof w / 4; i++) WR(a + i * 4, w[i]);
}
static void regs(void) {
    WR(0x03300130, 0x14 | 0x1400);          /* passos 0x500 (leitura e escrita) */
    WR(0x03300134, 0x003f003fu);            /* recorte 63 x 63 */
    WR(0x03300138, FB); WR(0x0330013c, FB); /* base de leitura e de escrita */
    WR(0x03300110, 0);                      /* pixc */
}
static void go(uint32_t first) { WR(0x033005a4, first); WR(0x03300100, 0); }

int main(int argc, char **argv) {
    CHECK(ph_init(&M) == 0);
    M.cel.legacy = (argc > 1 && !strcmp(argv[1], "legacy"));   /* sem argumento: motor do Opera (padrao); "legacy": motor antigo (cel.c) */
    printf("test_celexec: motor %s\n", M.cel.legacy ? "LEGADO (cel.c)" : "OPERA (cel_opera.c)");
    regs(); CHECK(M.madam.cel_w21c == 0x500 && M.madam.cel_w220 == 0x500);
    const uint32_t F = CCB_ACW | CCB_ACCW | CCB_NPABS | CCB_SPABS | CCB_LDSIZE | CCB_LDPPMP | CCB_CCBPRE | CCB_YOXY | CCB_BGND;

    /* 1) CEL 16 bpp linear 4x2 (nao empacotado), um CCB: x=10, y=4 */
    const uint32_t SRC = 0x2000, A = 0x1000;
    for (int ty = 0; ty < 2; ty++) for (int tx = 0; tx < 4; tx++) cel_fb_write16(M.ram, SRC + ty * 32 + tx * 2, (uint16_t)(0x0100 + ty * 0x40 + tx * 2));
    ccb(A, F | CCB_LAST, 0, SRC, 10, 4, 6 | 0x10 | (1 << 6), (6u << 16) | 3);
    regs(); go(A);
    CHECK(M.madam.cel_state == 0);                       /* terminou: poke 0x100 com stall 0 desenha na hora */
    CHECK(M.cel.drawn == 1 && M.cel.unsupported == 0);
    CHECK(dst(10, 4) == (0x0100 & 0x7ffe) && dst(13, 4) == (0x0106 & 0x7ffe) && dst(10, 5) == (0x0140 & 0x7ffe) && dst(13, 5) == (0x0146 & 0x7ffe));
    CHECK(dst(9, 4) == 0 && dst(14, 4) == 0 && dst(10, 6) == 0);
    CHECK(M.madam.dma_arr[1] == 0);                      /* NEXTCCB: fim da cadeia */

    /* 2) dois CCBs: o 2o sem YOXY cai logo abaixo (ypos += vdy * altura = 2) */
    const uint32_t B = A + 0x80, SRC2 = 0x3000;
    for (int ty = 0; ty < 2; ty++) for (int tx = 0; tx < 4; tx++) cel_fb_write16(M.ram, SRC2 + ty * 32 + tx * 2, (uint16_t)(0x0200 + ty * 0x40 + tx * 2));
    ccb(A, F, B, SRC, 20, 8, 6 | 0x10 | (1 << 6), (6u << 16) | 3);
    ccb(B, (F & ~CCB_YOXY) | CCB_LAST, 0, SRC2, 0, 0, 6 | 0x10 | (1 << 6), (6u << 16) | 3);
    { uint32_t w[] = { (F & ~CCB_YOXY) | CCB_LAST, 0, SRC2, 0, 0, 0, 0x100000, 0, 0, 0x10000, 0x1F001F00u, 6 | 0x10 | (1 << 6), (6u << 16) | 3 };
      /* sem YOXY o leiaute encolhe: xpos/ypos nao existem no CCB, a leitura segue em +0x18 */
      uint32_t o[] = { w[0], w[1], w[2], w[3], 0, 0, w[6], w[7], w[8], w[9], w[10], w[11], w[12] };
      for (unsigned i = 0; i < sizeof o / 4; i++) WR(B + i * 4, o[i]); }
    regs(); go(A);
    CHECK(M.cel.drawn == 3);
    CHECK(dst(20, 8) == (0x0100 & 0x7ffe) && dst(20, 10) == (0x0200 & 0x7ffe) && dst(23, 11) == (0x0246 & 0x7ffe));

    /* 3) CEL empacotado 16 bpp, 4x2: linha 0 = literal de 4 pixels; linha 1 = 1 pixel repetido 4 vezes */
    { const uint32_t P = 0x4000, C = 0x1200;
      static const uint8_t bytes[] = { 0x00,0x01, 0x43, 0x01,0x00, 0x01,0x02, 0x01,0x04, 0x01,0x06, 0x00, 0,0,0,0,      /* linha 0: 12 bytes + folga */
                                       0x00,0x00, 0xC3, 0x03,0x00, 0x00, 0,0 };                                           /* linha 1 */
      for (unsigned i = 0; i < sizeof bytes; i++) ph_write8(&M, P + i, bytes[i]);
      /* linha 0 tem 12 bytes (off=1): a linha 1 comeca em P+12 */
      for (unsigned i = 0; i < 8; i++) ph_write8(&M, P + 12 + i, bytes[16 + i]);
      ccb(C, F | CCB_PACKED | CCB_LAST, 0, P, 30, 20, 6 | 0x10 | (1 << 6), 0);
      regs(); go(C);
      CHECK(M.cel.drawn == 4);
      CHECK(dst(30, 20) == (0x0100 & 0x7ffe) && dst(33, 20) == (0x0106 & 0x7ffe));
      CHECK(dst(30, 21) == (0x0300 & 0x7ffe) && dst(33, 21) == (0x0300 & 0x7ffe) && dst(34, 21) == 0); }

    /* 4) escala 2x (PARTE21: malha de triangulos de FUN_00480290, antes cel_blit22/33 liam lixo): cada texel vira 2x2 pixels */
    { const uint32_t D = 0x1400;
      ccb(D, F | CCB_LAST, 0, SRC, 20, 30, 6 | 0x10 | (1 << 6), (6u << 16) | 3);
      WR(D + 0x18, 0x200000); WR(D + 0x24, 0x20000);          /* hdx = 2.0 (12.20), vdy = 2.0 (16.16) */
      regs(); unsigned p0 = M.cel.legacy ? M.cel.poly : M.cel.drawn; go(D);
      CHECK((M.cel.legacy ? M.cel.poly : M.cel.drawn) == p0 + 1);
      int ok = 1;
      for (int ty = 0; ty < 2; ty++) for (int tx = 0; tx < 4; tx++) for (int k = 0; k < 4; k++)
          if (dst(20 + 2 * tx + (k & 1), 30 + 2 * ty + (k >> 1)) != ((0x0100 + ty * 0x40 + tx * 2) & 0x7ffe)) ok = 0;
      CHECK(ok);
      CHECK(dst(19, 30) == 0 && dst(28, 30) == 0 && dst(20, 34) == 0 && dst(20, 29) == 0); }

    /* 5) perspectiva (hddx != 0) e rotacao: desenha so dentro do recorte e dentro da caixa dos cantos */
    { const uint32_t D = 0x1500;
      uint32_t w[] = { F | CCB_LDPRS | CCB_LAST, 0, SRC, 0, 40u << 16, 40u << 16, 0x200000, 0, 0, 0x20000, 0x40000, 0, 0x1F001F00u, 6 | 0x10 | (1 << 6), (6u << 16) | 3 };
      for (unsigned i = 0; i < sizeof w / 4; i++) WR(D + i * 4, w[i]);
      regs(); go(D);
      int any = 0, out = 0;
      for (int y = 34; y < 64; y++) for (int x = 34; x < 64; x++) { if (dst(x, y)) { any++; if (x < 40 || y < 40) out++; } }   /* so a area livre: os CELs dos testes anteriores ficam em x < 34 */
      CHECK(any > 8 && out == 0); }
    /* 6) CEL NAO empacotado de 4 bpp com PLUT (antes lido como 16 bpp cru -> listras): 4x2, pixels 1,2,3,4 / 5,6,7,8 */
    { const uint32_t E = 0x1600, PL = 0x1700, S4 = 0x5000;
      for (int i = 0; i < 16; i++) cel_fb_write16(M.ram, PL + i * 2, (uint16_t)((i + 1) * 0x42));
      static const uint8_t px4[] = { 0x12, 0x34, 0, 0, 0, 0, 0, 0, 0x56, 0x78, 0, 0, 0, 0, 0, 0 };   /* passo 8 bytes (WOFFSET 0) */
      for (unsigned i = 0; i < sizeof px4; i++) ph_write8(&M, S4 + i, px4[i]);
      /* LDPRS com hddx=hddy=0: os registradores do CEL persistem entre CCBs, e o teste 5 deixou hddx != 0 */
      { uint32_t w[] = { F | CCB_LDPLUT | CCB_PPABS | CCB_LDPRS | CCB_LAST, 0, S4, PL, 50u << 16, 4u << 16, 0x100000, 0, 0, 0x10000, 0, 0, 0x1F001F00u, 3 | (1 << 6), 3 };
        for (unsigned i = 0; i < sizeof w / 4; i++) WR(E + i * 4, w[i]); }
      regs(); unsigned d0 = M.cel.drawn; go(E);
      CHECK(M.cel.drawn == d0 + 1);
      int ok4 = 1;
      for (int ty = 0; ty < 2; ty++) for (int tx = 0; tx < 4; tx++)
          if (dst(50 + tx, 4 + ty) != (uint16_t)(((ty * 4 + tx + 1 + 1) * 0x42) & 0x7ffe)) ok4 = 0;
      CHECK(ok4);
      CHECK(dst(54, 4) == 0 && dst(49, 4) == 0); }
    ph_free(&M);
    printf("test_celexec: %d verificacoes, %d falhas\n", checks, fails);
    return fails != 0;
}
