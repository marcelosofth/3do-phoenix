/* test_hires.c - HiRes CEL Rendering (opcao phoenix_high_resolution), Parte 24.
 * Modelo igual ao do opera-libretro (HIRESMODE): o framebuffer 2x e a VRAM + 3 planos extras; o pixel (x,y) de 2x vive no plano
 * q = ((y&1)<<1)|(x&1) na posicao (x>>1,y>>1). Escritas 1x (CPU, SPORT, DMA, CELs de linha/escala) valem para os 4 planos;
 * o rasterizador arbitrario (3D) grava so a subamostra de cada pixel 2x. O VDLP monta 2 linhas de 2*largura: planos 0,1 / 2,3. */
#include "machine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static ph_machine_t M;
#define WR(a, v) ph_write32(&M, (a), (v))
#define VR 0x200000u
#define STEP 0x500u
#define FB VR                                  /* framebuffer de teste = inicio da VRAM */

static uint16_t rd16(const uint32_t *w, uint32_t off) { uint16_t v; off ^= 2; memcpy(&v, (const uint8_t *)w + off, 2); return v; }
static uint32_t pixoff(int x, int y) { return STEP * (uint32_t)(y >> 1) + (uint32_t)(y & 1) * 2 + (uint32_t)x * 4; }   /* leiaute do XY2OFF com passo 0x500 */
/* pixel 2x (x,y) = plano q na posicao (x>>1,y>>1) */
static uint16_t px2(int x, int y) { int q = ((y & 1) << 1) | (x & 1); uint32_t o = pixoff(x >> 1, y >> 1); return q == 0 ? rd16(&M.ram[VR >> 2], o) : rd16(M.hr[q - 1], o); }

static void ccb(uint32_t a, uint32_t flags, uint32_t next, uint32_t src, int x, int y, uint32_t pre0, uint32_t pre1) {
    uint32_t w[] = { flags, next, src, 0, (uint32_t)x << 16, (uint32_t)y << 16, 0x100000, 0, 0, 0x10000, 0x1F001F00u, pre0, pre1 };
    for (unsigned i = 0; i < sizeof w / 4; i++) WR(a + i * 4, w[i]);
}
static void regs(void) {
    WR(0x03300130, 0x14 | 0x1400); WR(0x03300134, 0x003f003fu); WR(0x03300138, FB); WR(0x0330013c, FB); WR(0x03300110, 0);
}
static void go(uint32_t first) { WR(0x033005a4, first); WR(0x03300100, 0); }
#define F (CCB_ACW | CCB_ACCW | CCB_NPABS | CCB_SPABS | CCB_LDSIZE | CCB_LDPPMP | CCB_CCBPRE | CCB_YOXY | CCB_BGND)

static void fresh(int hires) { CHECK(ph_init(&M) == 0); CHECK(ph_set_hires(&M, hires) == 0); }

/* desenha um CEL 4x2 com rotacao/perspectiva (caminho arbitrario) e devolve quantas subamostras foram gravadas */
static void draw_persp(void) {
    const uint32_t SRC = 0x2000, D = 0x1500;
    for (int ty = 0; ty < 2; ty++) for (int tx = 0; tx < 4; tx++) { uint16_t v = (uint16_t)(0x0100 + ty * 0x40 + tx * 2); uint32_t a = SRC + ty * 32 + tx * 2; uint16_t *p = (uint16_t *)((uint8_t *)M.ram + (a ^ 2)); *p = v; }
    uint32_t w[] = { F | CCB_LDPRS | CCB_LAST, 0, SRC, 0, 20u << 16, 20u << 16, 0x200000, 0, 0, 0x20000, 0x40000, 0, 0x1F001F00u, 6 | 0x10 | (1 << 6), (6u << 16) | 3 };
    for (unsigned i = 0; i < sizeof w / 4; i++) WR(D + i * 4, w[i]);
    regs(); go(D);
}

int main(void) {
    /* 1) CPU: write32 / write8 na VRAM replicam nos 3 planos; na DRAM nao */
    fresh(1);
    CHECK(M.hires == 1 && M.vdlp.hires == 1 && M.sport.hr[0] == M.hr[0] && M.vdlp.rgb_hr != NULL);
    WR(VR + 0x100, 0x12345678u);
    CHECK(M.ram[(VR + 0x100) >> 2] == 0x12345678u && M.hr[0][0x40] == 0x12345678u && M.hr[1][0x40] == 0x12345678u && M.hr[2][0x40] == 0x12345678u);
    ph_write8(&M, VR + 0x203, 0xab);
    { uint32_t o = ((VR + 0x203) ^ 3u) & 0xfffffu; CHECK(((uint8_t *)M.hr[0])[o] == 0xab && ((uint8_t *)M.hr[1])[o] == 0xab && ((uint8_t *)M.hr[2])[o] == 0xab); }
    WR(0x1000, 0xdeadbeefu); CHECK(M.hr[0][0x1000 >> 2] != 0xdeadbeefu);      /* DRAM nao toca os planos */

    /* 2) DMA (XBUS/DSP): ram_written replica o trecho que cai na VRAM */
    for (int i = 0; i < 4; i++) M.ram[((VR + 0x400) >> 2) + i] = 0xa0a00000u + (uint32_t)i;
    CHECK(M.clio.ram_written != NULL);
    M.clio.ram_written(&M, VR + 0x400, 16);
    CHECK(M.hr[0][0x100] == 0xa0a00000u && M.hr[1][0x101] == 0xa0a00001u && M.hr[2][0x103] == 0xa0a00003u);

    /* 3) SPORT: flash write com cor e copia de pagina chegam aos planos; a copia preserva o conteudo 2x */
    WR(0x03200000u + 0x2000, 0x7c1f7c1fu);                                    /* cor */
    WR(0x03200000u + 0x4000 + 0x10, 0xffffffffu);                             /* flash: pagina 0x10 => palavra 0x800 */
    CHECK(M.ram[(VR >> 2) + 0x800] == 0x7c1f7c1fu && M.hr[0][0x800] == 0x7c1f7c1fu && M.hr[1][0x800] == 0x7c1f7c1fu && M.hr[2][0x9ff] == 0x7c1f7c1fu);
    M.hr[1][0x800] = 0x11111111u;                                             /* conteudo so do plano 2: precisa sobreviver a uma copia de pagina */
    (void)ph_read32(&M, 0x03200000u + 0x10);                                  /* peek: origem = pagina 0x10 (palavra 0x800) */
    WR(0x03200000u + 0x20, 0xffffffffu);                                      /* copia -> pagina 0x20 (palavra 0x1000) */
    CHECK(M.ram[(VR >> 2) + 0x1000] == 0x7c1f7c1fu && M.hr[1][0x1000] == 0x11111111u && M.hr[0][0x1000] == 0x7c1f7c1fu);

    /* 4) CEL 1x (blit 1:1, caminho linha/escala): as 4 subamostras recebem o pixel */
    {   const uint32_t SRC = 0x2000, A = 0x1000;
        for (int tx = 0; tx < 4; tx++) { uint16_t v = (uint16_t)(0x0100 + tx * 2); *(uint16_t *)((uint8_t *)M.ram + ((SRC + tx * 2) ^ 2)) = v; }
        ccb(A, F | CCB_LAST, 0, SRC, 10, 4, 6 | 0x10 | (1 << 6), (6u << 16) | 3);
        regs(); go(A);
        CHECK(M.cel.drawn == 1);
        int ok = 1;
        for (int tx = 0; tx < 4; tx++) for (int k = 0; k < 4; k++) if (px2((10 + tx) * 2 + (k & 1), 4 * 2 + (k >> 1)) != (uint16_t)((0x0100 + tx * 2) & 0x7ffe)) ok = 0;
        CHECK(ok);
        CHECK(px2(9 * 2, 8) == 0 && px2(14 * 2, 8) == 0);
    }
    ph_free(&M);

    /* 5) CEL arbitrario (3D): em 2x cobre ~4x mais subamostras que em 1x, e cada plano recebe a sua parte */
    {   fresh(0); draw_persp();
        unsigned n1 = 0; for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) if (rd16(&M.ram[VR >> 2], pixoff(x, y))) n1++;
        ph_free(&M);
        fresh(1); draw_persp();
        unsigned n2[4] = {0, 0, 0, 0}, tot = 0;
        for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) if (px2(x, y)) { n2[((y & 1) << 1) | (x & 1)]++; tot++; }
        printf("  arbitrario: 1x=%u pixels; 2x=%u subamostras (planos %u/%u/%u/%u)\n", n1, tot, n2[0], n2[1], n2[2], n2[3]);
        CHECK(n1 > 20);
        CHECK(tot > n1 * 3 && tot < n1 * 5);                                          /* ~4x */
        int bal = 1; for (int q = 0; q < 4; q++) if (n2[q] * 5 < tot) bal = 0;        /* nenhum plano ficou de fora */
        CHECK(bal);
        /* plano 0 continua sendo a imagem 1x que a CPU/VDLP enxergam: cobertura parecida com a do render 1x */
        CHECK(n2[0] * 4 > n1 * 3 && n2[0] * 3 < n1 * 4 + n1);
        ph_free(&M);
    }

    /* 6) destino fora da VRAM (DRAM): HIRESMODE nao liga, nada vaza para os planos nem alem da RAM */
    {   fresh(1);
        const uint32_t DR = 0x100000u, SRC = 0x2000, D = 0x1500;
        for (int tx = 0; tx < 4; tx++) *(uint16_t *)((uint8_t *)M.ram + ((SRC + tx * 2) ^ 2)) = (uint16_t)(0x0100 + tx * 2);
        uint32_t w[] = { F | CCB_LDPRS | CCB_LAST, 0, SRC, 0, 20u << 16, 20u << 16, 0x200000, 0, 0, 0x20000, 0x40000, 0, 0x1F001F00u, 6 | 0x10 | (1 << 6), (6u << 16) | 3 };
        for (unsigned i = 0; i < sizeof w / 4; i++) WR(D + i * 4, w[i]);
        WR(0x03300130, 0x14 | 0x1400); WR(0x03300134, 0x003f003fu); WR(0x03300138, DR); WR(0x0330013c, DR); WR(0x03300110, 0); go(D);
        unsigned n = 0; for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) if (rd16(&M.ram[DR >> 2], pixoff(x, y))) n++;
        unsigned leak = 0; for (unsigned i = 0; i < 0x40000; i++) leak += M.hr[0][i] | M.hr[1][i] | M.hr[2][i];
        CHECK(n > 20 && leak == 0);
        ph_free(&M);
    }

    /* 7) desligar: ganchos soltos, escritas da CPU nao tocam mais os planos, VDLP volta a 1x */
    {   fresh(1); CHECK(ph_set_hires(&M, 0) == 0);
        CHECK(M.hires == 0 && M.vdlp.hires == 0 && M.sport.hr[0] == NULL && M.vdlp.hr[0] == NULL);
        M.hr[0][0x40] = 0; WR(VR + 0x100, 0x55555555u); CHECK(M.hr[0][0x40] == 0);
        CHECK(ph_set_hires(&M, 1) == 0 && M.hr[0][0x40] == 0x55555555u);           /* religar: recomeca da VRAM atual */
        ph_free(&M);
    }

    /* 8) savestate: o estado nao guarda os planos; ao carregar eles recomecam da VRAM (replicacao) e o modo continua ligado */
    {   fresh(1); WR(VR + 0x300, 0x01020304u);
        size_t n = ph_state_size(&M); void *buf = malloc(n);
        CHECK(buf && ph_state_save(&M, buf, n) == 0);
        M.hr[1][0xc0] = 0; M.ram[(VR >> 2) + 0xc0] = 0;
        CHECK(ph_state_load(&M, buf, n) == 0);
        CHECK(M.hires == 1 && M.vdlp.hires == 1 && M.vdlp.rgb_hr && M.sport.hr[1] == M.hr[1] && M.hr[1][0xc0] == 0x01020304u && M.hr[2][0xc0] == M.ram[(VR >> 2) + 0xc0]);
        free(buf); ph_free(&M);
    }

    /* 9) troca de regiao mantem o modo 2x com o rgb_hr do tamanho novo (PAL2 = 384x288 => 768x576) */
    {   fresh(1); CHECK(ph_set_region(&M, VDLP_MODE_PAL2) == 0);
        CHECK(M.vdlp.hires && M.vdlp.width == 384 && M.vdlp.lines == 288 && M.vdlp.rgb_hr != NULL && M.vdlp.hr[0] == M.hr[0]);
        M.vdlp.rgb_hr[(size_t)768 * 576 * 3 - 1] = 1;                                   /* ultimo byte: precisa caber (ASan pega se nao) */
        ph_free(&M);
    }

    /* 10) VDLP: linha nativa vira 2 linhas de 2*largura (planos 0,1 / 2,3), com CLUT/fundo/direto iguais ao 1x */
    {   static uint32_t vr[0x40000], h0[0x40000], h1[0x40000], h2[0x40000];
        vdlp_t v; CHECK(vdlp_init(&v, 0, vr) == 0 && vdlp_set_hires(&v, 1, h0, h1, h2) == 0);
        static int nb, bw, by0, bn; static uint8_t last[3];
        (void)last;
        const uint32_t VDL = 0x100, BMP = 0x2000;
        vr[VDL / 4 + 0] = (240u + 16) | (2u << 9) | 0x10000u | 0x200000u;
        vr[VDL / 4 + 1] = VR + BMP; vr[VDL / 4 + 2] = 0; vr[VDL / 4 + 3] = 0;
        vr[VDL / 4 + 4] = 0xe0112233u;                                                  /* fundo R=11 G=22 B=33 */
        vr[VDL / 4 + 5] = 0xc2000000u;                                                  /* pixel direto pelo bit 15 */
        /* linha visivel 0 = linha 0x10 do bitmap: par de linhas 8, metade ALTA (bit1 = 0) */
        uint32_t base = (uint32_t)(0x10 >> 1) * 320u * 4u;                              /* offset do par */
        uint32_t b0 = (((BMP + base) ^ 2u) & 0xfffffu);                                 /* pixel 0 */
        uint16_t pv[4] = { 0xfc00, 0x83e0, 0x801f, 0xffff };                            /* R, G, B, branco (todos com bit15 = direto) */
        uint32_t *pl[4] = { vr, h0, h1, h2 };
        for (int q = 0; q < 4; q++) { uint32_t w = pl[q][b0 >> 2]; pl[q][b0 >> 2] = (b0 & 2) ? ((w & 0xffffu) | ((uint32_t)pv[q] << 16)) : ((w & 0xffff0000u) | pv[q]); }
        vdlp_set_head(&v, VR + VDL);
        for (uint32_t ln = 0; ln < 0x120 + 0x20; ln++) vdlp_line(&v, ln);
        vdlp_frame_end(&v);
        const uint8_t *r0 = v.rgb_hr, *r1 = v.rgb_hr + (size_t)640 * 3;                /* linhas 2x 0 e 1 da linha nativa 0 */
        CHECK(r0[0] == 0xf8 && r0[1] == 0 && r0[2] == 0);                               /* plano 0 -> (0,0) */
        CHECK(r0[3] == 0 && r0[4] == 0xf8 && r0[5] == 0);                               /* plano 1 -> (1,0) */
        CHECK(r1[0] == 0 && r1[1] == 0 && r1[2] == 0xf8);                               /* plano 2 -> (0,1) */
        CHECK(r1[3] == 0xf8 && r1[4] == 0xf8 && r1[5] == 0xf8);                         /* plano 3 -> (1,1) */
        (void)nb; (void)bw; (void)by0; (void)bn;
        vdlp_free(&v);
    }

    printf("test_hires: %d verificacoes, %d falhas\n", checks, fails);
    return fails ? 1 : 0;
}
