/* test_cel_unpacked.c - AUDITORIA: CEL NAO empacotado com bpp < 16 (1/2/4/6/8 bpp coded).
 * O .exe (FUN_00487100 + FUN_0047d760) decodifica a imagem pixel a pixel com bpp bits, passo = WOFFSET*4+8,
 * convertendo cada indice por FUN_0047ca50 (PLUT). Este teste descreve o comportamento esperado do .exe. */
#include "machine.h"
#include <stdio.h>
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static ph_machine_t M;
#define WR(a, v) ph_write32(&M, (a), (v))
#define FB 0x100000u
#define STEP 0x500u
static uint16_t dst(int x, int y) { return cel_fb_read16(M.ram, FB + STEP * (y >> 1) + (y & 1) * 2 + x * 4); }
static void regs(void) { WR(0x03300130, 0x14 | 0x1400); WR(0x03300134, 0x003f003fu); WR(0x03300138, FB); WR(0x0330013c, FB); WR(0x03300110, 0); }
static uint16_t plut(int i) { return (uint16_t)(((i + 1) * 0x0421) & 0x7ffe); }

int main(void) {
    CHECK(ph_init(&M) == 0);
    const uint32_t F = CCB_ACW | CCB_ACCW | CCB_NPABS | CCB_SPABS | CCB_PPABS | CCB_LDSIZE | CCB_LDPPMP | CCB_LDPLUT | CCB_CCBPRE | CCB_YOXY | CCB_BGND | CCB_LAST;
    const uint32_t A = 0x1000, PL = 0x1800, SRC = 0x2000;
    for (int i = 0; i < 32; i++) { WR(PL + (i & ~1) * 2, 0); }
    for (int i = 0; i < 32; i++) cel_fb_write16(M.ram, PL + i * 2, plut(i));
    int bpps[] = { 1, 2, 4, 8 }; int code[] = { 1 /*1bpp*/, 2, 3, 5 };
    for (int k = 0; k < 4; k++) {
        int bpp = bpps[k], W = 8, H = 2; int ok = 1;
        for (unsigned i = 0; i < 0x200; i++) ph_write8(&M, SRC + i, 0);
        /* pixel (x,y) = (x + 3*y) & ((1<<bpp)-1); passo da linha 8 bytes (WOFFSET = 0) ou 12 se bpp==8 */
        uint32_t stride = 8;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            unsigned v = (unsigned)(x + 3 * y) & ((1u << bpp) - 1);
            unsigned bit = (unsigned)x * bpp; uint32_t a = SRC + y * stride + bit / 8;
            uint8_t b = ph_read8(&M, a); b |= (uint8_t)(v << (8 - bpp - (bit % 8))); ph_write8(&M, a, b);
        }
        /* bpp<8: WOFFSET8 em PRE1[31:24]; 8 bpp: WOFFSET10 em PRE1[25:16] */
        uint32_t pre0 = (uint32_t)code[k] | ((H - 1) << 6), pre1 = (bpp < 8 ? 0u : (0u << 16)) | (W - 1);
        uint32_t w[] = { F, 0, SRC, PL, 10u << 16, 4u << 16, 0x100000, 0, 0, 0x10000, 0x1F001F00u, pre0, pre1 };
        for (unsigned i = 0; i < sizeof w / 4; i++) WR(A + i * 4, w[i]);
        for (int yy = 0; yy < 12; yy++) for (int xx = 0; xx < 24; xx++) cel_fb_write16(M.ram, FB + STEP * (yy >> 1) + (yy & 1) * 2 + xx * 4, 0x1234);
        regs(); WR(0x033005a4, A); WR(0x03300100, 0);
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            unsigned v = (unsigned)(x + 3 * y) & ((1u << bpp) - 1);
            if (dst(10 + x, 4 + y) != plut((int)v)) { ok = 0; }
        }
        printf("unpacked %d bpp: %s (dst(10,4)=%04x esperado=%04x)\n", bpp, ok ? "OK" : "ERRADO", dst(10, 4), plut(0));
        CHECK(ok);
    }
    ph_free(&M);
    printf("test_cel_unpacked: %d verificacoes, %d falhas\n", checks, fails);
    return fails != 0;
}
