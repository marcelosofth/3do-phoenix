/* trace_dsp.c — confere se o semaforo DSP/ARM (CLIO 0x17d0) e tocado perto da trava, antes de
 * mexer em qualquer coisa. Encapa clio_peek/clio_poke via os ponteiros de callback do madam/clio
 * (mais simples: encapa m.cpu.rd32/wr32 e filtra o endereco de barramento 0x034017d0, que e
 * exatamente off=0x17d0 dentro do bloco do CLIO).
 *
 * Uso: trace_dsp <bios> <imagem> <cyc_min> <cyc_max>
 */
#include <stdio.h>
#include <stdlib.h>
#include "machine.h"

static ph_machine_t m;
static uint32_t (*o_rd32)(void*,uint32_t);
static void     (*o_wr32)(void*,uint32_t,uint32_t);
static uint32_t cyc_min, cyc_max;
static int n_hits = 0;

static uint32_t w_rd32(void *u, uint32_t a) {
    uint32_t v = o_rd32(u, a);
    if ((a & 0xfffffffc) == 0x034017d0 && m.total_cycles >= cyc_min && m.total_cycles <= cyc_max && n_hits < 200) {
        printf("cyc=%-10u READ  a=%08x v=%08x pc=%08x\n", m.total_cycles, a, v, m.cpu.r[15]);
        n_hits++;
    }
    return v;
}
static void w_wr32(void *u, uint32_t a, uint32_t v) {
    if ((a & 0xfffffffc) == 0x034017d0 && m.total_cycles >= cyc_min && m.total_cycles <= cyc_max && n_hits < 200) {
        printf("cyc=%-10u WRITE a=%08x v=%08x pc=%08x\n", m.total_cycles, a, v, m.cpu.r[15]);
        n_hits++;
    }
    o_wr32(u, a, v);
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "uso: %s <bios> <imagem> <cyc_min> <cyc_max>\n", argv[0]); return 1; }
    cyc_min = strtoul(argv[3], 0, 0); cyc_max = strtoul(argv[4], 0, 0);
    if (ph_init(&m)) return 1;
    FILE *f = fopen(argv[1], "rb"); if (!f) { fprintf(stderr, "bios?\n"); return 1; }
    static uint8_t b[0x200000]; size_t n = fread(b, 1, sizeof b, f); fclose(f);
    ph_load_bios(&m, b, n); m.clio.R[0x28 >> 2] = 1;
    if (ph_cd_insert(&m, argv[2])) { fprintf(stderr, "imagem?\n"); return 1; }
    o_rd32 = m.cpu.rd32; o_wr32 = m.cpu.wr32;
    m.cpu.rd32 = w_rd32; m.cpu.wr32 = w_wr32;
    while (m.total_cycles < cyc_max) ph_run(&m, 4096);
    printf("--- fim: %d acessos a 0x034017d0 entre cyc %u e %u ---\n", n_hits, cyc_min, cyc_max);
    return 0;
}
