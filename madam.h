/* madam.h - MADAM do Phoenix 3DO (registradores 0x03300000..0x033007ff), reimplementado a partir do
 * PhoenixEmuProject.exe com REA/Ghidra (Parte 13).
 *   FUN_0048b2a0 = Peek   FUN_0048adb0 = Poke   FUN_00489650 = escrita do bloco 0x100..0x1ff (comandos do CEL)
 *   FUN_0047c7c0 = leitura do bloco 0x100..0x1ff   FUN_004894c0 = muda o estado do CEL (laco da CPU / 0x03300100)
 * O motor de CEL em si (FUN_00488700, 3,5 KB) NAO esta aqui: entra pelo gancho cel_exec (NULL = termina na hora).
 * Ver PARTE13_MADAM.md. */
#ifndef PH_MADAM_H
#define PH_MADAM_H
#include <stdint.h>

typedef struct madam madam_t;
struct madam {
    /* --- registradores: base = madam+0x10003a0, indice = (addr & 0x7fc) >> 2 --- */
    uint32_t regs[0x200];
    uint32_t dma_arr[8];            /* madam+0x1000228: espelho de 0x5a0..0x5bc (palavra por palavra, mascara 0xfffffc) */
    /* --- componente CEL (= madam+0x38 = maquina+0x210; o laco da CPU o acha por *(maquina+0x1097548)) --- */
    uint32_t cel_state;             /* CEL+0x10000ec = madam+0x1000124: 0 ocioso, 0x10 ocupado, 0x30 pausado (Peek 0x28 le isto) */
    uint32_t cel_w[64];             /* CEL+0x10000f0: palavras dos comandos 0x100..0x1ff (Peek devolve cel_w[(off-0x100)>>2]) */
    uint32_t cel_w21c, cel_w220;    /* CEL+0x100021c/0x220: derivados da palavra 0x130 (cel_w[12]) */
    uint32_t cel_arg;               /* CEL+0x10000c4: argumento dos ganchos scope_begin/scope_end */
    uint8_t  cel_enabled;           /* CEL+0x10000e0: opcao "motor de CEL ligado" (0 = o Poke de 0x100 nao roda nada) */
    uint8_t  cel_flag40;            /* CEL+0x40: 0 => modo assincrono (cel_exec recebe async=1 no inicio) */
    /* --- motor de matrizes (0x600..0x7fc): vetor resultado vive em GLOBAIS do .exe (0xcf406c, 68, 64, 60) --- */
    int32_t  mat_out[4];            /* [0]=_DAT_00cf406c [1]=_DAT_00cf4068 [2]=_DAT_00cf4064 [3]=_DAT_00cf4060 */
    /* --- ganchos (NULL = neutro) --- */
    void    *ud;
    void     (*reg0_write)(void *ud, int8_t ch);              /* Poke 0x000: vtable(+0xc0) do objeto global, argumento (char)valor */
    void     (*dma580_write)(void *ud, uint32_t v);           /* Poke 0x580: ponteiro-membro em madam+0x1000bb8/bc0/bc8 (nulo => ignora) */
    void     (*mctl_write)(void *ud, uint32_t v);             /* Poke 0x008 com PLAYXEN (0x8000): DMA8 da PBUS (controles) - port do Opera HandleDMA8 */
    uint32_t (*cel_exec)(void *ud, uint8_t async);            /* FUN_00488700 (motor de CEL); retorno = stall (ciclos) */
    void     (*cel_sync)(void *ud);                           /* FUN_00704850: espera a fila assincrona do CEL */
    void     (*scope_begin)(void *ud, uint32_t arg);          /* vtable global +0xd0 */
    void     (*scope_end)(void *ud, uint32_t arg);            /* vtable global +0xd8 */
};

void     madam_reset(madam_t *m);
uint32_t madam_peek(madam_t *m, uint32_t addr);               /* FUN_0048b2a0 (usa addr & 0x7fc) */
uint32_t madam_poke(madam_t *m, uint32_t addr, uint32_t v);   /* FUN_0048adb0; o retorno so importa em 0x03300100 (= stall do ARM) */
void     madam_cel_set(madam_t *m, uint32_t n);               /* FUN_004894c0(cel, n): n=0 termina/sincroniza, 0x30 em FIQ */
int      madam_cel_busy(const madam_t *m);                    /* cel_state == 0x10 (teste do laco FUN_004a0920) */
#endif
