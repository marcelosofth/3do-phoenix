/* arm60.h - CPU ARM60 (ARMv3, modos 32 bits) reimplementada a partir de
 * FUN_00423870 (passo), FUN_00422800 (troca de banco) e FUN_004a0920 (laco)
 * do PhoenixEmuProject.exe. Ver PARTE9_ARM60.md. */
#ifndef ARM60_H
#define ARM60_H
#include <stdint.h>

enum { M_USR=0x10, M_FIQ=0x11, M_IRQ=0x12, M_SVC=0x13, M_ABT=0x17, M_UND=0x1b };

typedef struct arm60 arm60_t;
struct arm60 {
    uint32_t r[16];          /* Phoenix: this+0xd0 ; r[15]=this+0x10c = endereco da instrucao atual */
    uint32_t cpsr;           /* this+0x180 */
    uint32_t spsr[6];        /* indice de banco 1..5 = FIQ,IRQ,SVC,ABT,UND (this+0x168+4*i; [0] sem uso) */
    uint32_t usrhi[5];       /* copias USR de r8..r12 (this+0x110..0x120), validas fora de USR com FIQ ativo */
    uint32_t fiq[7];         /* FIQ r8..r14 (this+0x13c..0x154) quando fora de FIQ */
    uint32_t b13[6], b14[6]; /* r13/r14 por banco: [0]=USR(0x124/0x128) [2]=IRQ(0x158/0x15c) [3]=SVC(0x12c/0x130)
                                [4]=ABT(0x134/0x138) [5]=UND(0x160/0x164); valem quando o banco NAO e o atual */
    int      cur_bank;       /* 0=USR 1=FIQ 2=IRQ 3=SVC 4=ABT 5=UND */
    uint32_t shc;            /* this+0x188: carry-out do shifter (bit 0) */
    int      cyc;            /* this+0x184 (negativo): ciclos da instrucao em curso */
    void *ud;
    uint32_t (*rd32)(void *ud, uint32_t a);   /* leitura de dado (alinhada, sem rotacao) */
    uint32_t (*fetch)(void *ud, uint32_t a);  /* busca de opcode (this+0x90) */
    uint8_t  (*rd8)(void *ud, uint32_t a);
    void     (*wr32)(void *ud, uint32_t a, uint32_t v);
    void     (*wr8)(void *ud, uint32_t a, uint8_t v);
    int      (*swi_hook)(void *ud, uint32_t imm24);  /* this+0x40: !=0 => tratado em HLE */
    void     (*mode_hook)(void *ud);                 /* this+0x28 */
    void     (*debug_log)(void *ud, const char *fmt, ...);  /* debug logging via RetroArch */
    int      trace;          /* 0 (padrao) = nenhum log no caminho quente; 1 = loga excecoes e SWIs */
    uint32_t stat_undef, undef_pc, undef_op;   /* AUDITORIA: instrucoes indefinidas (1a ocorrencia) */
    uint32_t stat_fiq, stat_irq;               /* AUDITORIA: excecoes de interrupcao atendidas */
};

void arm60_reset(arm60_t *c);
int  arm60_step(arm60_t *c);                 /* devolve ciclos (>=1) */
void arm60_irq(arm60_t *c);                  /* IRQ se CPSR.I == 0 */
void arm60_fiq(arm60_t *c);                  /* FIQ se CPSR.F == 0 */
void arm60_set_mode(arm60_t *c, uint32_t m); /* FUN_00422800: troca bancos de registradores */
#endif
