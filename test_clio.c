/* Testes de consistencia do clio.c contra o que foi lido no .exe (nao sao testes contra o proprio .exe). */
#include "clio.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
static int irq1_n, irq2_n, audio_n, frames;
static uint32_t mad[0x200];
static uint32_t mpeek(void *x, uint32_t o) { return mad[o >> 2]; }
static void mpoke(void *x, uint32_t o, uint32_t v) { mad[o >> 2] = v; }
static void audio(void *x, uint32_t s) { audio_n++; }
static void frame(void *x) { frames++; }
/* dispositivo de teste: entrega 8 bytes 11 22 33 ... */
static uint8_t src[8] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88}; static int pos;
static uint32_t d_poll(void *x) { return pos < 8 ? 0x20 : 0; }
static uint32_t d_data(void *x) { return pos < 8 ? src[pos++] : 0; }
static clio_t C; static uint8_t ram[PH_RAM_BYTES];

int main(void) {
    clio_t *c = &C; clio_reset(c);
    c->audio_out = audio; c->frame_cb = frame; c->madam_peek = mpeek; c->madam_poke = mpoke; c->ram8 = ram;

    /* 1) registradores */
    assert(clio_peek(c, 0x3400000) == 0x02022000u);
    clio_poke(c, 0x3400040, 0x2); assert(clio_peek(c, 0x3400040) == 0x2);
    clio_poke(c, 0x3400060, 0x1); assert(clio_peek(c, 0x3400040) == 0x80000002u);       /* bit31 sem mascara 0x68 */
    clio_poke(c, 0x3400064, 0x1); assert(clio_peek(c, 0x3400040) == 0x2);
    clio_poke(c, 0x340004c, 0xffffffff); assert(clio_peek(c, 0x3400048) == 0x80000000u);
    clio_poke(c, 0x3400400, 0x800); assert((clio_peek(c, 0x3400400) & 0x80) == 0);       /* 0x800 => sem gate */
    clio_poke(c, 0x3400400, 0x000); assert((clio_peek(c, 0x3400400) & 0x80) != 0);
    assert(clio_peek(c, 0x3400414) == 0x4000);
    assert(clio_peek(c, 0x3400540) == 0x30 || 1);                                       /* sem dispositivo: poll 0x30 */
    assert(clio_peek(c, 0x3400540) == 0x30);
    c->xbus_sel = 0x80; assert(clio_peek(c, 0x3400540) == 0x0); c->xbus_sel = 0;        /* bit7 mascara &0xf */

    /* 2) cadencia: audio 9/2551 por ciclo; campo NTSC/PAL */
    for (int pal = 0; pal < 2; pal++) {
        clio_reset(c); c->pal = pal; irq1_n = audio_n = frames = 0;

        c->R[0x08 >> 2] = 0x20;                                  /* VINT0 na linha 0x20 */
        uint32_t cycles = 12500000u * 4;                         /* 4 s de CPU a 12,5 MHz */
        uint32_t fields = 0, last = 0;
        for (uint32_t t = 0; t < cycles; t += 100) {
            clio_tick(c, 100);
            if (c->R[0x40 >> 2] & 1) { fields++; c->R[0x40 >> 2] &= ~1u; }
            (void)last;
        }
        double sec = 4.0;
        printf("%s: audio=%.1f Hz  campos=%.3f Hz (frames_cb=%d)\n", pal ? "PAL " : "NTSC", audio_n / sec, fields / sec, frames);
    }

    /* 3) timers: timer 1 com recarga, estoura e levanta IRQ0 bit 10 */
    clio_reset(c);
    c->R[0x100 >> 2 | 2] = 0;                                   /* contador 1 = R[0x108] */
    c->R[0x108 >> 2] = 2; c->R[0x10c >> 2] = 5;                 /* valor 2, recarga 5 */
    c->R[0x200 >> 2] = (0x3u << 4);                             /* timer 1: enable + reload */
    for (int i = 0; i < 10; i++) clio_tick(c, 32);              /* 32*2 = 64 = 1 tick (delay 0) */
    printf("timer1: contador=%u IRQ0=%08x (esperado bit10 = 0x400 setado)\n", c->R[0x108 >> 2], c->R[0x40 >> 2]);
    assert(c->R[0x40 >> 2] & 0x400);

    /* 4) DMA XBUS: 8 bytes do dispositivo -> RAM, big-endian por palavra, termina com IRQ bit 29 e gate reaberto */
    clio_reset(c); memset(ram, 0, 64); memset(mad, 0, sizeof mad); pos = 0;
    static xdev_t cd = {0, 0, 0, d_poll, 0, d_data, 0, 0}; c->dev[0] = &cd;
    mad[0x540 >> 2] = 0x100; mad[0x544 >> 2] = 4;               /* 8 bytes: tamanho = bytes-4 */
    c->R[0x400 >> 2] = 0x800 | 0x80; c->R[0x404 >> 2] = 0x200; c->R[0x304 >> 2] = 0x100000;
    clio_tick(c, 1);
    printf("DMA: ram[0x100..]=%02x %02x %02x %02x %02x %02x %02x %02x  R400=%08x R304=%08x IRQ0=%08x madam540=%x\n",
           ram[0x100], ram[0x101], ram[0x102], ram[0x103], ram[0x104], ram[0x105], ram[0x106], ram[0x107],
           c->R[0x400 >> 2], c->R[0x304 >> 2], c->R[0x40 >> 2], mad[0x540 >> 2]);
    assert(ram[0x103] == 0x11 && ram[0x100] == 0x44 && ram[0x107] == 0x55);        /* indice ^3 */
    assert((c->R[0x40 >> 2] & 0x20000000u) && !(c->R[0x304 >> 2] & 0x100000) && (c->R[0x400 >> 2] & 0x80));
    printf("OK\n"); return 0;
}
