/* sport.c - SPORT (FUN_004714d0 / FUN_004714e0). */
#include "sport.h"

/* Peek: devolve o valor anterior de +0x48 e grava o novo */
uint32_t sport_peek(sport_t *s, uint32_t off) {
    uint32_t old = s->src;
    s->src = (off & 0xfffffffcu) << 7;
    return old;
}

/* O .exe indexa a VRAM sem checar limite (src pode chegar a 0xfffff palavras = 4 MB, VRAM tem 0x40000):
 * le lixo do host. Aqui fora da VRAM le 0 (unico desvio, so atinge programas que ja estao errados). */
static uint32_t vget(const sport_t *s, uint32_t i) { return i < SPORT_VRAM_WORDS ? s->vram[i] : 0; }

void sport_poke(sport_t *s, uint32_t off, uint32_t v) {
    uint32_t region = off & 0xffffe000u, cbarg, i;
    if (region == 0x4000) {                              /* flash write: 0x200 palavras com a cor de +0x44 */
        uint32_t reg = off & 0x7fc, base = reg * 0x80;
        if (s->event) s->event(s->ud, 1, reg << 9, s->fill);
        for (i = 0; i < 0x200; i++) {
            uint32_t *p = &s->vram[base + i];
            if (v == 0xffffffffu) *p = s->fill;
            else *p = ((*p ^ s->fill) & v) ^ s->fill;
        }
        cbarg = reg >> 2;
    } else if (region == 0) {                            /* copia pagina origem (+0x48) -> destino */
        uint32_t reg = off & 0x7fc;
        s->dst = reg << 7;
        if (s->event) s->event(s->ud, 0, reg << 9, s->src << 2);
        for (i = 0; i < 0x200; i++) {
            uint32_t a = vget(s, s->src + i);
            uint32_t *p = &s->vram[s->dst + i];
            if (v == 0xffffffffu) *p = a;
            else *p = ((*p ^ a) & v) ^ a;
        }
        cbarg = (off & 0x7ff) >> 2;
    } else {
        if (region == 0x2000) s->fill = v;               /* 0x2000..0x3fff: cor; qualquer outro: ignora */
        return;
    }
    if (s->written) s->written(s->ud, cbarg);
}
