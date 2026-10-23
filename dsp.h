/* dsp.h - DSP do Phoenix 3DO (classe dsp3DO), reimplementado a partir do PhoenixEmuProject.exe (REA com Ghidra).
 * Fonte: FUN_0046d570 (passo por amostra), FUN_00427f00 (decodifica flags), FUN_0042de40 (busca operandos),
 * FUN_0045e910 (ULA), FUN_0045f270/0045f000/00428080 (controle/desvios/MOVE), FUN_00428160 (endereco por registrador),
 * FUN_004281f0 (leitura), FUN_0042e3a0 (escrita), FUN_00490360/004910c0/0048f020/0048f0b0 (FIFOs DMA do audio).
 * So o caminho interpretado e portado: FUN_0046a3d0 e as entradas +0x890 do .exe sao um otimizador (mesmo efeito). */
#ifndef PH_DSP_H
#define PH_DSP_H
#include <stdint.h>
#include "clio.h"

#define DSP_NMEM_WORDS 0x400          /* memoria de instrucoes (N): .exe +0x10894 */

struct dsp {
    uint8_t  flags;                   /* +0x10890: bit0 baixo(Z16) bit1 zero bit2 overflow bit3 negativo bit4 carry */
    uint16_t nmem[DSP_NMEM_WORDS];    /* +0x10894 (escrita pelo ARM em 0x1800..0x1fff e 0x2000..0x27ff) */
    uint16_t ei[0x80];                /* +0x11894 registradores EI (ARM escreve em 0x3000..0x37ff); FIFOs de entrada = 0x70..0x7c */
    uint16_t imem[0x100];             /* +0x11994 memoria de dados I (DSP 0x100..0x2ff) */
    uint16_t eo[0x100];               /* +0x11b94 registradores EO (DSP 0x300..0x3ff): 0xeb=d6a 0xee=d70 0xef=d72(relogio) 0xfe=L 0xff=R */
    uint16_t m6, m8, ma, mc;          /* +0x11da6/a8/aa/ac: operandos do multiplicador/ULA */
    uint32_t acc;                     /* +0x11db0: acumulador (20 bits em 31..12); db2 = acc>>16 */
    uint32_t retaddr;                 /* +0x11db4 */
    uint32_t extra;                   /* +0x11dc0: ciclos extras de pipeline */
    uint16_t rbase, rmode;            /* +0x11d96 / +0x11d98 */
    int16_t  remain, start;           /* +0x11da0 / +0x11dd0: ciclos restantes / no inicio da amostra */
    uint8_t  delay;                   /* +0x11da2: desvio pendente (slot de atraso) */
    uint16_t mask;                    /* +0x11da4 */
    uint8_t  dc6, dc7, dc8, dc9, dca, dcb;   /* flags de decodificacao */
    uint16_t dcc, dce;                /* destino de escrita / controle do deslocador */
    clio_t  *clio;                    /* PC=clio->dsp_in_a, instrucao=dsp_in_b, semaforos, dsp_run/dsp_halt, rand */
    void    *ctx;                     /* nao usado */
};
typedef struct dsp dsp_t;

void dsp_init(dsp_t *d);
void dsp_free(dsp_t *d);
void dsp_set_clio(dsp_t *d, clio_t *c);        /* liga ao CLIO e poe o estado inicial do construtor do .exe */
void dsp_reset(dsp_t *d, int flag);            /* FUN_00427d90: 0x17e4 -> 1, 0x17e8 -> 0 */
uint16_t dsp_read16(dsp_t *d, uint32_t idx);   /* FUN_0045f8c0: EO lido pelo ARM (idx 0x300+) */
void     dsp_write16(dsp_t *d, uint32_t idx, uint16_t v);   /* FUN_0045f8a0: EI (idx < 0x100) */
void     dsp_nwrite16(dsp_t *d, uint32_t idx, uint16_t v);  /* N-mem (instrucoes) escrita pelo ARM */
uint32_t dsp_step(dsp_t *d);                   /* 1 amostra: (L<<16)|R */
#endif
