/* quemchama.c -- descobre QUEM entrou no laco: para na 1a vez que o PC chega em <alvo> (depois de <ciclo_min>)
 * e imprime o historico dos ultimos saltos (origem -> destino) com SP/LR/R0/R9, + grava a RAM nesse instante.
 *
 * Uso:   quemchama <bios> <imagem> <alvo_hex> <ciclo_min> <ciclo_max> [ram_saida=ram_antes.bin]
 * Ex.:   ./quemchama.exe fz1.bin "AI Shougi (Japan).chd" 284e0 2000000 110000000
 * Build: gcc -O2 -std=c11 -I. -Ilibretro-common/include -o quemchama quemchama.c machine.c arm60.c clio.c \
 *        madam.c sport.c vdlp.c cel.c celexec.c cd.c cd_chd.c dsp.c joybus.c chdobj/*.o -lm
 * Depois:  python armdis.py <endereco> <n> --ram ram_antes.bin   (nos enderecos de ORIGEM mostrados)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"

#define RING 128
typedef struct { uint32_t from, to, lr, sp, r0, r9, cyc; } jmp_t;
static jmp_t ring[RING]; static unsigned rn = 0;
static ph_machine_t m;
static uint32_t (*o_fetch)(void*,uint32_t);
static uint32_t target, cyc_min, prev = 0xffffffffu; static int hit = 0;

static uint32_t w_fetch(void *u, uint32_t a) {
    if (!hit) {
        if (a != prev + 4) {
            jmp_t *j = &ring[rn++ % RING];
            j->from = prev; j->to = a; j->lr = m.cpu.r[14]; j->sp = m.cpu.r[13];
            j->r0 = m.cpu.r[0]; j->r9 = m.cpu.r[9]; j->cyc = m.total_cycles;
        }
        prev = a;
        if (a == target && m.total_cycles >= cyc_min) hit = 1;
    }
    return o_fetch(u, a);
}

int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "uso: %s <bios> <imagem> <alvo_hex> <ciclo_min> <ciclo_max> [ram_saida]\n", argv[0]); return 1; }
    target = strtoul(argv[3], 0, 16); cyc_min = strtoul(argv[4], 0, 0);
    uint32_t cyc_max = strtoul(argv[5], 0, 0);
    const char *ramout = argc > 6 ? argv[6] : "ram_antes.bin";
    if (ph_init(&m)) return 1;
    FILE *f = fopen(argv[1], "rb"); if (!f) { fprintf(stderr, "bios?\n"); return 1; }
    static uint8_t b[0x200000]; size_t n = fread(b, 1, sizeof b, f); fclose(f);
    ph_load_bios(&m, b, n); m.clio.R[0x28 >> 2] = 1;
    if (ph_cd_insert(&m, argv[2])) { fprintf(stderr, "imagem?\n"); return 1; }
    o_fetch = m.cpu.fetch; m.cpu.fetch = w_fetch;
    while (!hit && m.total_cycles < cyc_max) ph_run(&m, 256);
    if (!hit) { printf("alvo %08x nao foi atingido ate cyc=%u\n", target, m.total_cycles); return 2; }
    FILE *d = fopen(ramout, "wb"); fwrite(m.ram, 4, 0xC0000, d); fclose(d);
    printf("PC chegou em %08x  cyc=%u  (RAM gravada em %s)\n", target, m.total_cycles, ramout);
    printf("r0..r3 = %08x %08x %08x %08x   r7=%08x r9=%08x r10=%08x sp=%08x lr=%08x cpsr=%08x\n",
           m.cpu.r[0], m.cpu.r[1], m.cpu.r[2], m.cpu.r[3], m.cpu.r[7], m.cpu.r[9], m.cpu.r[10], m.cpu.r[13], m.cpu.r[14], m.cpu.cpsr);
    printf("\nultimos saltos (mais antigo primeiro): origem(+4 = instrucao seguinte ao salto) -> destino\n");
    unsigned cnt = rn < RING ? rn : RING, start = rn - cnt;
    for (unsigned i = 0; i < cnt; i++) {
        jmp_t *j = &ring[(start + i) % RING];
        printf("  cyc=%-10u %08x -> %08x   lr=%08x sp=%08x r0=%08x r9=%08x\n", j->cyc, j->from, j->to, j->lr, j->sp, j->r0, j->r9);
    }
    puts("\nStack (palavras a partir de SP):");
    for (int k = 0; k < 16; k++) { uint32_t a = m.cpu.r[13] + 4 * k; if (a < 0xC0000 * 4) printf("  [%08x] = %08x\n", a, m.ram[a >> 2]); }
    return 0;
}
