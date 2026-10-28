#include "arm60.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static uint32_t mem[1024];
static uint32_t F(void*u,uint32_t a){(void)u;return mem[(a>>2)&1023];}
static uint8_t R8(void*u,uint32_t a){(void)u;return (mem[(a>>2)&1023]>>((a&3)*8))&0xff;}
static void W32(void*u,uint32_t a,uint32_t v){(void)u;mem[(a>>2)&1023]=v;}
static void W8(void*u,uint32_t a,uint8_t v){(void)u;uint32_t m=0xffu<<((a&3)*8);mem[(a>>2)&1023]=(mem[(a>>2)&1023]&~m)|((uint32_t)v<<((a&3)*8));}
static int fails; 
#define CHK(c,msg) do{ if(!(c)){printf("FALHA: %s\n",msg);fails++;} }while(0)
int main(void){
    arm60_t c; memset(&c,0,sizeof c);
    c.fetch=F;c.rd32=F;c.rd8=R8;c.wr32=W32;c.wr8=W8; arm60_reset(&c);
    uint32_t p[]={
     0xe3a00005, /*0 MOV r0,#5 */
     0xe3a01000, /*4 MOV r1,#0 */
     0xe0811000, /*8 ADD r1,r1,r0 (loop) */
     0xe2500001, /*c SUBS r0,r0,#1 */
     0x1afffffc, /*10 BNE 8 */
     0xe3a02b02, /*14 MOV r2,#0x800 */
     0xe5821000, /*18 STR r1,[r2] */
     0xe5923000, /*1c LDR r3,[r2] */
     0xe3a04007, /*20 MOV r4,#7 */
     0xe0050394, /*24 MUL r5,r4,r3 (Rd=r5,Rm=r4,Rs=r3) */
     0xef000042, /*28 SWI 0x42 */
    };
    memcpy(mem,p,sizeof p);
    mem[2]=p[2]; mem[0x18/4]=p[6];
    int cyc=0, n=0;
    while(c.r[15]!=0x28 && n<100){ int k=arm60_step(&c); cyc+=k; n++; if(c.r[15]==0x28&&0) break; }
    CHK(c.r[1]==15,"soma 5+4+3+2+1");
    CHK(mem[0x800/4]==15,"STR");
    CHK(c.r[3]==15,"LDR");
    CHK(c.r[5]==105,"MUL 7*15");
    /* executa o SWI */
    c.r[15]=0x28; int k=arm60_step(&c);
    CHK(k==5,"SWI = 5 ciclos");
    CHK((c.cpsr&0x1f)==M_SVC && c.r[15]==8 && c.r[14]==0x2c && (c.cpsr&0x80),"SWI entra em SVC, LR=pc+4, vetor 8, I=1");
    /* condicao: MOVEQ nao executa com Z=0 */
    c.cpsr&=~0x40000000u; mem[100]=0x03a06001; c.r[15]=400; c.r[6]=0; k=arm60_step(&c);
    CHK(c.r[6]==0 && k==1,"MOVEQ ignorado");
    /* B: 5 ciclos */
    mem[101]=0xea000000; c.r[15]=404; k=arm60_step(&c); CHK(k==5 && c.r[15]==412,"B = 5 ciclos, destino pc+8");
    /* IRQ */
    c.cpsr=M_SVC; c.r[15]=0x100; arm60_set_mode(&c,M_SVC); c.cpsr=0x13; arm60_irq(&c);
    CHK((c.cpsr&0x1f)==M_IRQ && c.r[15]==0x18 && c.r[14]==0x104,"IRQ");

    /* ---- rodada 10: ALU, shifter, PSR, bancos, LDM/STM ---- */
#define RUN1(opw) (mem[0x80]=(opw), c.r[15]=0x200, arm60_step(&c))
    arm60_reset(&c); c.cpsr=M_SVC;
    c.r[0]=0xffffffff; c.r[1]=1; RUN1(0xe0902001);                    /* ADDS r2,r0,r1 */
    CHK(c.r[2]==0 && (c.cpsr>>28)==0x6,"ADDS: Z=1 C=1");
    c.r[0]=0; c.r[1]=1; RUN1(0xe0502001);                              /* SUBS r2,r0,r1 */
    CHK(c.r[2]==0xffffffff && (c.cpsr>>28)==0x8,"SUBS 0-1: N=1 C=0");
    c.r[0]=5; c.r[1]=5; RUN1(0xe0502001);
    CHK(c.r[2]==0 && (c.cpsr>>28)==0x6,"SUBS 5-5: Z=1 C=1");
    c.r[0]=0x7fffffff; c.r[1]=1; RUN1(0xe0902001);
    CHK((c.cpsr>>28)==0x9,"ADDS overflow: N=1 V=1");
    c.r[0]=0x80000000; RUN1(0xe1b02020);                               /* MOVS r2,r0,LSR #32 */
    CHK(c.r[2]==0 && ((c.cpsr>>29)&1),"LSR #32: resultado 0, C=bit31");
    c.cpsr|=0x20000000; c.r[0]=1; RUN1(0xe1b02060);                    /* MOVS r2,r0,RRX */
    CHK(c.r[2]==0x80000000 && ((c.cpsr>>29)&1),"RRX");
    c.r[0]=0x60000013; RUN1(0xe129f000);                               /* MSR CPSR_fc,r0 */
    CHK(c.cpsr==0x60000013,"MSR CPSR completo em SVC");
    RUN1(0xe10f3000);                                                  /* MRS r3,CPSR */
    CHK(c.r[3]==0x60000013,"MRS CPSR");
    c.cpsr=M_USR; c.cur_bank=0; c.r[0]=0xf0000013; RUN1(0xe129f000);
    CHK(c.cpsr==(0xf0000010u),"MSR em USR so altera flags");
    /* ---- 22/10: carry-out do LSL (MOVS Rx,Rx,LSL #n + BCS/BCC: idioma de decoder de FMV) ---- */
    arm60_reset(&c); c.cpsr=M_SVC;
    c.r[0]=0x80000000; c.cpsr&=~0x20000000u; RUN1(0xe1b02080);        /* MOVS r2,r0,LSL #1 */
    CHK(c.r[2]==0 && ((c.cpsr>>29)&1),"LSL #1: C=bit31");
    c.r[0]=0x00000001; c.cpsr|=0x20000000u; RUN1(0xe1b02080);
    CHK(c.r[2]==2 && !((c.cpsr>>29)&1),"LSL #1: C=0 quando bit31=0");
    c.r[0]=0x00010000; c.cpsr&=~0x20000000u; RUN1(0xe1b02800);        /* MOVS r2,r0,LSL #16 */
    CHK(c.r[2]==0 && ((c.cpsr>>29)&1),"LSL #16: C=bit16");
    c.r[0]=0x00008000; c.cpsr|=0x20000000u; RUN1(0xe1b02800);
    CHK(c.r[2]==0x80000000u && !((c.cpsr>>29)&1),"LSL #16: C=0 (bit16 limpo)");
    c.r[0]=1; c.r[1]=32; c.cpsr&=~0x20000000u; RUN1(0xe1b02110);     /* MOVS r2,r0,LSL r1 (32) */
    CHK(c.r[2]==0 && ((c.cpsr>>29)&1),"LSL reg #32: resultado 0, C=bit0");
    c.r[1]=33; c.cpsr|=0x20000000u; RUN1(0xe1b02110);                  /* LSL reg #33 */
    CHK(c.r[2]==0 && !((c.cpsr>>29)&1),"LSL reg >32: resultado 0, C=0");
    c.r[0]=0x40000000; c.r[1]=2; c.cpsr&=~0x20000000u; RUN1(0xe1b02110); /* LSL reg #2: bit30 -> C */
    CHK(c.r[2]==0 && ((c.cpsr>>29)&1),"LSL reg #2: C=bit30");
    /* loop Cinepak-like: testa 32 flags MSB-first com MOVS r4,r4,LSL #1 / ADC r5,r5,#0 */
    c.r[4]=0xa5a5a5a5u; c.r[5]=0; for(int i=0;i<32;i++){ RUN1(0xe1b04084); RUN1(0xe2a55000); }
    CHK(c.r[5]==16,"32 deslocamentos LSL #1 contam os 16 bits setados via carry");
    /* bancos */
    arm60_reset(&c); c.cpsr=M_SVC; c.r[13]=0x1111; c.r[8]=0x88;
    arm60_set_mode(&c,M_IRQ); c.cpsr=M_IRQ; c.r[13]=0x2222;
    arm60_set_mode(&c,M_FIQ); c.cpsr=M_FIQ; CHK(c.r[8]==0,"FIQ tem r8 proprio"); c.r[8]=0xf8; c.r[13]=0x3333;
    arm60_set_mode(&c,M_USR); c.cpsr=M_USR; CHK(c.r[8]==0x88,"USR recupera r8");
    arm60_set_mode(&c,M_FIQ); CHK(c.r[8]==0xf8 && c.r[13]==0x3333,"FIQ recupera r8/r13");
    arm60_set_mode(&c,M_IRQ); CHK(c.r[13]==0x2222 && c.r[8]==0x88,"IRQ r13");
    arm60_set_mode(&c,M_SVC); CHK(c.r[13]==0x1111,"SVC r13");
    /* STM/LDM */
    arm60_reset(&c); c.cpsr=M_SVC; c.r[13]=0x400; c.r[0]=1; c.r[1]=2; c.r[2]=3;
    k=RUN1(0xe8ad0007);                                                /* STMIA r13!,{r0-r2} */
    CHK(mem[0x100]==1&&mem[0x101]==2&&mem[0x102]==3&&c.r[13]==0x40c,"STMIA!"); CHK(k==8,"STM 3 regs = 8 ciclos");
    k=RUN1(0xe93d0070);                                                /* LDMDB r13!,{r4-r6} */
    CHK(c.r[4]==1&&c.r[5]==2&&c.r[6]==3&&c.r[13]==0x400,"LDMDB!"); CHK(k==7,"LDM 3 regs = 7 ciclos");
    c.r[0]=0x500; c.r[1]=7; RUN1(0xe8a00003);                          /* STMIA r0!,{r0,r1}: Rn e o menor */
    CHK(mem[0x140]==0x500,"STM base menor: grava base antiga");
    c.r[1]=0x600; c.r[0]=9; RUN1(0xe8a10003);                          /* STMIA r1!,{r0,r1}: Rn nao e o menor */
    CHK(mem[0x181]==0x608,"STM base nao-menor: grava base nova");
    mem[0x1c0/4*0+0x70]=0x300; mem[0x71]=0x404; c.r[0]=0x1c0; mem[0x1c0/4]=0x55; mem[0x1c0/4+1]=0x600;
    c.r[0]=0x1c0; k=RUN1(0xe8908002);                                  /* LDMIA r0,{r1,pc} */
    CHK(c.r[1]==0x55 && c.r[15]==0x600 && k==10,"LDM com PC = n+8 ciclos");
    /* LDM^ com PC restaura CPSR */
    arm60_reset(&c); c.cpsr=M_SVC; c.spsr[3]=0x60000010; c.r[13]=0x440; mem[0x440/4]=0x700;
    RUN1(0xe8fd8000);                                                  /* LDMIA r13!,{pc}^ */
    CHK(c.r[15]==0x700 && c.cpsr==0x60000010 && c.cur_bank==0,"LDM^ restaura SPSR e troca de banco");
    /* MUL: Rd==Rm */
    arm60_reset(&c); c.cpsr=M_SVC; c.r[1]=6; c.r[2]=7; k=RUN1(0xe0010291); /* MUL r1,r1,r2 */
    CHK(c.r[1]==0 && k==3,"MUL Rd==Rm zera Rd");
    printf("ciclos=%d passos=%d\n",cyc,n);
    puts(fails?"ERRO":"OK"); return fails!=0;
}
