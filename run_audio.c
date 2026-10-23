/* run_audio.c - roda o nucleo sem frontend (BIOS fz1) e grava o audio do DSP em WAV. */
#include "machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int16_t *buf; static size_t n, cap; static uint32_t nz;
static void aout(void *ud, uint32_t s) { (void)ud; if (n + 1 > cap) { cap = cap ? cap * 2 : 1 << 20; buf = realloc(buf, cap * 4); }
    buf[2 * n] = (int16_t)(s >> 16); buf[2 * n + 1] = (int16_t)s; n++; if (s) nz++; }
extern uint32_t dsp_stats[16];
int main(int argc, char **argv) {
    const char *bios = argc > 1 ? argv[1] : "fz1.bin"; double secs = argc > 2 ? atof(argv[2]) : 10; const char *out = argc > 3 ? argv[3] : "out.wav";
    static ph_machine_t m; if (ph_init(&m)) return 2;
    FILE *f = fopen(bios, "rb"); if (!f) { perror(bios); return 3; }
    static uint8_t img[PH_BIOS_BYTES]; size_t k = fread(img, 1, sizeof img, f); fclose(f); ph_load_bios(&m, img, k);
    m.clio.audio_out = aout;
    m.clio.R[0x28 >> 2] = 0x01; if (m.cpu.r[15] == 0) { m.cpu.r[15] = 0x03000000u; m.vram_flag = 1; }
    uint64_t total = (uint64_t)(secs * 12500000.0), done = 0;
    while (done < total) { uint32_t t0 = m.total_cycles; ph_run(&m, 0x4000); done += (uint32_t)(m.total_cycles - t0) ? (uint32_t)(m.total_cycles - t0) : 0x4000; }
    int peak = 0; for (size_t i = 0; i < 2 * n; i++) { int v = abs(buf[i]); if (v > peak) peak = v; }
    printf("samples=%zu nonzero=%u peak=%d PC=%08x dsp_run=%d halt=%d pc_dsp=%u\n", n, nz, peak, m.cpu.r[15], m.clio.dsp_run, m.clio.dsp_halt, m.clio.dsp_in_a);
    printf("DMA en=%08x irq0=%08x\n", m.clio.R[0x304 >> 2], m.clio.R[0x40 >> 2]);
    { int nzn = 0; for (int i = 0; i < 0x400; i++) if (m.dsp.nmem[i]) nzn++;
      printf("nmem nonzero=%d first: ", nzn); for (int i = 0; i < 12; i++) printf("%04x ", m.dsp.nmem[i]); printf("\n");
      printf("EO L=%04x R=%04x d6a=%04x clock=%04x sem_st=%04x sem_dt=%04x\n", m.dsp.eo[0xfe], m.dsp.eo[0xff], m.dsp.eo[0xeb], m.dsp.eo[0xef], m.clio.sem_status, m.clio.sem_data);
      for (int ch = 0; ch < 13; ch++) { uint32_t b = (0x400 + ch * 16) >> 2; printf("in%-2d addr=%06x len=%06x next=%06x nlen=%06x\n", ch, m.madam.regs[b], m.madam.regs[b+1], m.madam.regs[b+2], m.madam.regs[b+3]); }
      for (int ch = 0; ch < 4; ch++) { uint32_t b = (0x500 + ch * 16) >> 2; printf("out%d addr=%06x len=%06x next=%06x nlen=%06x\n", ch, m.madam.regs[b], m.madam.regs[b+1], m.madam.regs[b+2], m.madam.regs[b+3]); }
      printf("IRQ0=%08x EN0=%08x IRQ1=%08x\n", m.clio.R[0x40>>2], m.clio.R[0x48>>2], m.clio.R[0x60>>2]); }
    printf("stats: 3ec=%u 3ed=%u 3ee(irq)=%u outpush=%u wrL=%u wrR=%u nonzero_LR=%u in_reload=%u in_pop=%u\n", dsp_stats[0], dsp_stats[1], dsp_stats[2], dsp_stats[3], dsp_stats[4], dsp_stats[5], dsp_stats[6], dsp_stats[7], dsp_stats[8]);
    FILE *w = fopen(out, "wb"); uint32_t sr = 44100, dl = (uint32_t)(n * 4), v;
    fwrite("RIFF", 1, 4, w); v = 36 + dl; fwrite(&v, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w); v = 16; fwrite(&v, 4, 1, w);
    uint16_t a = 1, c = 2, ba = 4, bps = 16; fwrite(&a, 2, 1, w); fwrite(&c, 2, 1, w); fwrite(&sr, 4, 1, w); v = sr * 4; fwrite(&v, 4, 1, w); fwrite(&ba, 2, 1, w); fwrite(&bps, 2, 1, w);
    fwrite("data", 1, 4, w); fwrite(&dl, 4, 1, w); fwrite(buf, 4, n, w); fclose(w); return 0; }
