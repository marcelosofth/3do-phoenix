/* test_state.c - savestate deterministico: salva no quadro N, roda M quadros (hash A), carrega e roda M quadros de novo (hash B); A == B. */
#include "machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned frames_closed;
static void on_frame(void *ud) { ph_machine_t *m = ud; vdlp_frame_end(&m->vdlp); frames_closed++; }
static uint64_t hash(const ph_machine_t *m) {
    uint64_t h = 1469598103934665603ull;
#define MIX(p, n) for (size_t i = 0; i < (n); i++) { h ^= ((const uint8_t *)(p))[i]; h *= 1099511628211ull; }
    MIX(m->ram, 0xC0000u * 4); MIX(m->clio.R, sizeof m->clio.R); MIX(m->cpu.r, sizeof m->cpu.r); MIX(&m->cpu.cpsr, 4);
    MIX(m->vdlp.rgb, (size_t)m->vdlp.width * m->vdlp.lines * 3); MIX(&m->total_cycles, 4);
    return h;
}
static void frames(ph_machine_t *m, int n) {
    while (n--) { unsigned b = frames_closed; unsigned spent = 0; while (frames_closed == b && spent < 263 * 792 * 3) { uint32_t t0 = m->total_cycles; ph_run(m, 1024); spent += m->total_cycles - t0; } }
}
int main(int argc, char **argv) {
    static ph_machine_t M; static uint8_t bios[0x200000]; int fails = 0;
    FILE *f = fopen(argc > 1 ? argv[1] : "BIOS/fz1.bin", "rb"); if (!f) { puts("sem BIOS: teste ignorado"); return 0; }
    size_t n = fread(bios, 1, sizeof bios, f); fclose(f);
    ph_init(&M); ph_load_bios(&M, bios, n); M.clio.R[0x28 >> 2] = 1; M.cpu.r[15] = 0x03000000u; M.vram_flag = 1; M.clio.vdlp_line = on_frame;
    frames(&M, 400);
    size_t sz = ph_state_size(&M); void *st = malloc(sz);
    if (ph_state_save(&M, st, sz) != 0) { puts("FALHA: save"); return 1; }
    frames(&M, 300); uint64_t a = hash(&M);
    if (ph_state_load(&M, st, sz) != 0) { puts("FALHA: load"); return 1; }
    frames(&M, 300); uint64_t b = hash(&M);
    printf("estado=%zu bytes  hashA=%016llx hashB=%016llx\n", sz, (unsigned long long)a, (unsigned long long)b);
    if (a != b) { puts("FALHA: execucao apos load diverge"); fails++; }
    if (ph_state_load(&M, st, sz - 1) == 0) { puts("FALHA: aceitou estado truncado"); fails++; }
    ph_free(&M); free(st);
    printf("test_state: %d falhas\n", fails); return fails != 0;
}
