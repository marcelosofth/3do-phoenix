/* sport.h - SPORT do Phoenix 3DO (0x03200000..0x03201fff + 0x4000 flash), Parte 13.
 *   FUN_004714d0 = Peek (escolhe a pagina de ORIGEM)   FUN_004714e0 = Poke (copia / flash write / cor)
 * Objeto em maquina+0x10974f0; +0x50 = ponteiro da VRAM = maquina+0x149755c (= RAM + 2 MB, conferido no ctor 0x4716a0). */
#ifndef PH_SPORT_H
#define PH_SPORT_H
#include <stdint.h>

#define SPORT_VRAM_WORDS 0x40000u        /* 1 MB */

typedef struct sport sport_t;
struct sport {
    uint32_t vram_words;                 /* tamanho da VRAM em palavras; 0 = SPORT_VRAM_WORDS (1 MB) */
    uint32_t *vram;                      /* obj+0x50: VRAM como palavras (valor big-endian do 3DO, igual a RAM) */
    uint32_t *hr[3];                     /* HiRes CEL Rendering (Phoenix): planos extras da VRAM; copia/flash valem para eles tambem. NULL = desligado */
    uint32_t src;                        /* obj+0x48: palavra-origem (= (offset lido & ~3) << 7); Peek devolve o valor ANTERIOR */
    uint32_t fill;                       /* obj+0x44: cor do flash write (poke 0x2000..0x3fff) */
    uint32_t dst;                        /* obj+0x4c: ultima palavra-destino da copia */
    void    *ud;
    void     (*event)(void *ud, uint8_t kind, uint32_t a, uint32_t b);   /* vtable +0x78, evento 0x10101: {kind, reg<<9, b} (kind 0 = copia, b = src*4; kind 1 = flash, b = cor) */
    void     (*written)(void *ud, uint32_t arg);                         /* ponteiro-membro obj+0x28..0x38 com offset>>2 (aviso do renderizador) */
};

uint32_t sport_peek(sport_t *s, uint32_t off);               /* off = addr & 0x1ffc */
void     sport_poke(sport_t *s, uint32_t off, uint32_t v);   /* off = addr & 0xfffc */
#endif
