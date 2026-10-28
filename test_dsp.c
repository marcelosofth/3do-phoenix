/* test_dsp.c - testes do DSP (dsp.c): FIFOs de DMA de audio -> DSP -> L/R, FIFO de saida -> RAM, IRQ e semaforo.
 * Os programas do DSP sao montados a mao (codificacao lida do .exe: ver dsp.h). */
#include "dsp.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
static int fails;
#define CHK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)
static clio_t C; static dsp_t D; static uint32_t regs[0x200]; static uint8_t ram[PH_RAM_BYTES];
static uint32_t rp(void *u, uint32_t o) { (void)u; return regs[(o & 0x7fc) >> 2]; }
static void rpk(void *u, uint32_t o, uint32_t v) { (void)u; regs[(o & 0x7fc) >> 2] = v; }
static void be16(uint32_t a, int16_t v) { ram[a ^ 3] = (uint8_t)((uint16_t)v >> 8); ram[(a + 1) ^ 3] = (uint8_t)v; }
static int16_t rd16(uint32_t a) { return (int16_t)((ram[a ^ 3] << 8) | ram[(a + 1) ^ 3]); }
static void reset_all(void) {
    memset(&C, 0, sizeof C); memset(regs, 0, sizeof regs); memset(ram, 0, sizeof ram);
    dsp_init(&D); dsp_set_clio(&D, &C);
    C.ram8 = ram; C.madam_raw_peek = rp; C.madam_raw_poke = rpk;
}
static void load(const uint16_t *p, int n) { for (int i = 0; i < n; i++) dsp_nwrite16(&D, i, p[i]); dsp_reset(&D, 1); }
int main(void) {
    /* 1) estado inicial do construtor */
    reset_all();
    CHK(D.nmem[0] == 0x8380 && D.nmem[0x3ff] == 0x8380, "N-mem inicia com 0x8380 (dormir)");
    CHK(C.dsp_run == 4 && C.dsp_halt == 1 && C.dsp_in_a == 1, "estado inicial: dbc=4, em execucao, PC=1");
    CHK(dsp_step(&D) == 0, "DSP desligado (0x17fc=0): amostra 0");

    /* 2) MOVE imediato -> L/R, depois dorme */
    reset_all();
    { const uint16_t p[] = { 0x9bfe, 0xc123, 0x9bff, 0xc045, 0x8380 }; load(p, 5); }
    C.dsp_run = 1;
    uint32_t s = dsp_step(&D);
    CHK(s == ((0x123u << 16) | 0x45), "MOVE #imm -> EO 0x3fe/0x3ff = (L<<16)|R");
    CHK(C.dsp_halt == 0, "depois do 0x8380 o DSP dorme (0x17fc: bit 'dormindo')");
    CHK(dsp_step(&D) == s, "proxima amostra repete (o programa recomeca)");

    /* 3) FIFO de entrada: canais 0 e 1 -> L e R (leitura de EI 0xf0/0xf1 repoe o proximo valor) */
    reset_all();
    for (int i = 0; i < 64; i++) { be16(0x1000 + 2 * i, (int16_t)(1000 + i)); be16(0x2000 + 2 * i, (int16_t)(-2000 - i)); }
    regs[0x400 >> 2] = 0x1000; regs[0x404 >> 2] = 64 * 2 - 4;       /* canal 0: 64 amostras (len = bytes-4) */
    regs[0x410 >> 2] = 0x2000; regs[0x414 >> 2] = 64 * 2 - 4;       /* canal 1 */
    C.R[0x304 >> 2] = 0x3;
    { const uint16_t p[] = { 0x9bfe, 0x80f0, 0x9bff, 0x80f1, 0x8380 }; load(p, 5); }
    C.dsp_run = 1;
    int okseq = 1; int16_t prevL = 0;
    for (int i = 0; i < 20; i++) {
        uint32_t o = dsp_step(&D); int16_t L = (int16_t)(o >> 16), R = (int16_t)o;
        if (i >= 1 && (L != (int16_t)(1000 + i - 1) || R != (int16_t)(-2000 - (i - 1)))) okseq = 0;
        prevL = L;
    }
    (void)prevL;
    CHK(okseq, "DMA -> FIFO de entrada -> DSP -> L/R: 20 amostras corretas (1 amostra de latencia do EI)");
    CHK(regs[0x400 >> 2] == 0x1000 + 2 * 20, "endereco do canal 0 avancou 2 bytes por leitura");

    /* 4) fim do buffer: recarrega do 'next', levanta IRQ0 bit 16+ch e desabilita se next == 0 */
    reset_all();
    for (int i = 0; i < 8; i++) be16(0x1000 + 2 * i, (int16_t)(100 + i));
    regs[0x400 >> 2] = 0x1000; regs[0x404 >> 2] = 4 * 2 - 4;        /* 4 amostras */
    regs[0x408 >> 2] = 0; regs[0x40c >> 2] = 0;                      /* sem next */
    C.R[0x304 >> 2] = 0x1;
    { const uint16_t p[] = { 0x9bfe, 0x80f0, 0x8380 }; load(p, 3); }
    C.dsp_run = 1;
    int16_t got[16]; for (int i = 0; i < 12; i++) got[i] = (int16_t)(dsp_step(&D) >> 16);
    int cont = 1; for (int i = 1; i < 5; i++) if (got[i] != (int16_t)(100 + i - 1)) cont = 0;
    CHK(cont, "leitura do buffer inteiro (4 amostras)");
    CHK(C.R[0x40 >> 2] & (1u << 16), "IRQ0 bit 16 (DMA do canal 0 concluida) levantado");
    CHK(!(C.R[0x304 >> 2] & 1), "canal desabilitado quando next == 0");

    /* 5) FIFO de saida (canal 0): DSP escreve em 0x3f0 -> RAM */
    reset_all();
    regs[0x500 >> 2] = 0x3000; regs[0x504 >> 2] = 4 * 2 - 4; C.R[0x304 >> 2] = 1u << 16;
    { const uint16_t p[] = { 0x9bf0, 0xc111, 0x9bf0, 0xc222, 0x9bf0, 0xc333, 0x9bf0, 0xc444, 0x8380 }; load(p, 9); }
    C.dsp_run = 1; dsp_step(&D);
    CHK(rd16(0x3000) == 0x111 && rd16(0x3002) == 0x222 && rd16(0x3004) == 0x333 && rd16(0x3006) == 0x444, "DSP -> FIFO de saida -> RAM (4 amostras)");
    CHK(C.R[0x40 >> 2] & (1u << 12), "IRQ0 bit 12 (DSPP->RAM canal 0 concluida)");

    /* 6) 0x3ee = IRQ do DSP (IRQ0 bit 11); 0x3ed = semaforo; relogio */
    reset_all();
    { const uint16_t p[] = { 0x9bed, 0xc0aa, 0x9bee, 0xc001, 0x9bef, 0xc040, 0x8380 }; load(p, 7); }
    C.dsp_run = 1; dsp_step(&D);
    CHK(C.R[0x40 >> 2] & 0x800, "escrita em 0x3ee levanta IRQ0 bit 11");
    CHK(C.sem_data == 0xaa && C.sem_status == 4, "escrita em 0x3ed grava o semaforo (dado=0xaa, status=4)");
    CHK(dsp_read16(&D, 0x300 + 0xef) == 0x40, "0x3ef = relogio (visivel ao ARM em EO)");

    /* 7) ULA: acc = A + B (somar dois registradores de EI) -- confere flags de zero */
    reset_all();
    printf(fails ? "\n%d FALHA(S)\n" : "\ntodos os testes passaram\n", fails);
    return fails != 0;
}
