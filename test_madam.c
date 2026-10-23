/* Teste do MADAM/SPORT (Parte 13). Os valores esperados vem da leitura da decompilacao (FUN_0048adb0,
 * FUN_0048b2a0, FUN_00489650, FUN_004894c0, FUN_004714d0/e0), recalculados aqui de forma independente. */
#include "madam.h"
#include "sport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHK(c,msg) do{ if(!(c)){printf("FALHA: %s\n",msg);fails++;} }while(0)
#define EQ(a,b,msg) do{ uint32_t _a=(uint32_t)(a),_b=(uint32_t)(b); if(_a!=_b){printf("FALHA: %s (obtido %08x, esperado %08x)\n",msg,_a,_b);fails++;} }while(0)

static int8_t last_ch; static int ch_calls; static uint32_t d580; static int d580_calls;
static int exec_calls, last_async, sync_calls, sb, se; static uint32_t exec_ret;
static void reg0(void *u, int8_t c){ (void)u; last_ch=c; ch_calls++; }
static void dma580(void *u, uint32_t v){ (void)u; d580=v; d580_calls++; }
static uint32_t cexec(void *u, uint8_t a){ (void)u; exec_calls++; last_async=a; return exec_ret; }
static void csync(void *u){ (void)u; sync_calls++; }
static void sbeg(void *u, uint32_t a){ (void)u; (void)a; sb++; }
static void send_(void *u, uint32_t a){ (void)u; (void)a; se++; }

static void setm(madam_t *m, uint32_t off, uint32_t v){ m->regs[off>>2]=(uint32_t)v; }
static int32_t sh(int64_t s){ return (int32_t)((uint64_t)s>>16); }

int main(void){
    static madam_t m; memset(&m,0,sizeof m); madam_reset(&m);
    m.reg0_write=reg0; m.dma580_write=dma580; m.cel_exec=cexec; m.cel_sync=csync; m.scope_begin=sbeg; m.scope_end=send_;
    m.cel_enabled=1;

    /* 1) registradores simples */
    madam_poke(&m,0x3300010,0x12345678); EQ(madam_peek(&m,0x3300010),0x12345678,"reg cru 0x10");
    madam_poke(&m,0x3300004,0xffffffff); EQ(madam_peek(&m,0x3300004),0x29,"poke 0x04 grava 0x29");
    madam_poke(&m,0x3300008,0xabcd0001); EQ(madam_peek(&m,0x3300008),0xabcd0001,"poke 0x08 grava o valor");
    madam_poke(&m,0x3300000,0x1ff); CHK(ch_calls==1 && last_ch==-1,"poke 0x00 chama o gancho com (char)valor");
    EQ(madam_peek(&m,0x3300000),0,"0x00 nao e armazenado");
    madam_poke(&m,0x3300800+0x20,0x77); EQ(madam_peek(&m,0x3300020),0x77,"addr & 0x7fc ignora bits altos");
    madam_poke(&m,0x3300222,1); EQ(madam_peek(&m,0x3300220),1,"addr & 0x7fc ignora bits baixos");

    /* 2) 0x400..0x5ff: mascara 0xfffffc e zera a palavra 8 bytes adiante */
    madam_poke(&m,0x3300548,0x55555555); setm(&m,0x550,0xdead);       /* 0x548 & 0xc = 8 -> nao zera */
    EQ(madam_peek(&m,0x3300548),0x555554,"0x548 mascara 0xfffffc");
    EQ(madam_peek(&m,0x3300550),0xdead&0xfffffc,"0x550 intacto (0x548 nao zera)");
    /* 0x540 ja esta FORA de 0x400..0x53f (cmp 0x13f; ja): so mascara, nao zera */
    setm(&m,0x548,0x99); madam_poke(&m,0x3300540,0x11111111);
    EQ(madam_peek(&m,0x3300540),0x111110,"0x540 mascarado"); EQ(madam_peek(&m,0x3300548),0x98,"0x540 NAO zera 0x548 (fora da faixa)");
    setm(&m,0x508,0x99); madam_poke(&m,0x3300500,0x11111111);          /* 0x500 & 0xc = 0 -> zera 0x508 */
    EQ(madam_peek(&m,0x3300508),0,"poke 0x500 zera 0x508");
    setm(&m,0x50c,0x77); madam_poke(&m,0x3300504,0x22222222);          /* 0x504 & 0xc = 4 -> zera 0x50c */
    EQ(madam_peek(&m,0x330050c),0,"poke 0x504 zera 0x50c");
    setm(&m,0x520,0x77); madam_poke(&m,0x3300518,0x33333333);          /* 0x518 & 0xc = 8 -> nao zera */
    EQ(m.regs[0x520>>2],0x77,"poke 0x518 (&0xc==8) nao zera");
    setm(&m,0x548,0x44); madam_poke(&m,0x330053c,0x1);                 /* ultima da faixa: 0x53c & 0xc = 0xc -> nao zera */
    EQ(m.regs[0x548>>2],0x44,"poke 0x53c nao zera");
    setm(&m,0x548,0x44); madam_poke(&m,0x3300538,0x1);                 /* 0x538 & 0xc = 8 */
    setm(&m,0x404,0x1234); madam_poke(&m,0x3300400,0xffffffff); EQ(madam_peek(&m,0x3300400),0xfffffc,"0x400 mascarado");
    EQ(madam_peek(&m,0x3300408),0,"poke 0x400 zera 0x408");
    madam_poke(&m,0x3300560,0xffffffff); EQ(madam_peek(&m,0x3300560),0xfffffc,"0x560 (>=0x540) so mascara");
    setm(&m,0x568,0x42); madam_poke(&m,0x3300560,0xffffffff); EQ(m.regs[0x568>>2],0x42,"0x560 nao zera nada");
    madam_poke(&m,0x33005c0,0xffffffff); EQ(madam_peek(&m,0x33005c0),0xfffffc,"0x5c0 mascarado");
    setm(&m,0x3a0,0xffffffff); EQ(madam_peek(&m,0x33003a0),0xffffffff,"0x3a0 (fora de 0x400..) cru");

    /* 0x5a0..0x5bc: array separado; 0x580: gancho, nao armazena */
    madam_poke(&m,0x33005a4,0xfedcba98); EQ(madam_peek(&m,0x33005a4),0xdcba98,"0x5a4 array mascarado");
    EQ(m.dma_arr[1],0xdcba98,"array em madam+0x1000228"); EQ(m.regs[0x5a4>>2],0,"0x5a4 nao cai em regs");
    madam_poke(&m,0x33005bc,0x123456); EQ(madam_peek(&m,0x33005bc),0x123454,"0x5bc ultimo do array");
    madam_poke(&m,0x3300580,0x00abcdef); CHK(d580_calls==1 && d580==0xabcdec,"0x580 chama o gancho (mascarado)");
    EQ(madam_peek(&m,0x3300580),0,"0x580 nao e armazenado");
    { void (*sv)(void*,uint32_t)=m.dma580_write; m.dma580_write=NULL; madam_poke(&m,0x3300580,1); m.dma580_write=sv; CHK(d580_calls==1,"0x580 sem gancho = ignora"); }

    /* 3) motor de matrizes */
    {
        madam_t x; memset(&x,0,sizeof x); madam_reset(&x);
        int32_t M[16], V[4]; int i,j;
        for(i=0;i<16;i++){ M[i]=(int32_t)((i*7919+13)*257)-0x30000; x.regs[(0x600>>2)+i]=(uint32_t)M[i]; }
        V[0]=0x12345; V[1]=-0x23456; V[2]=0x3456; V[3]=0x10000; for(i=0;i<4;i++) x.regs[(0x640>>2)+i]=(uint32_t)V[i];
        madam_poke(&x,0x33007fc,1);
        int32_t exp[4]; for(i=0;i<4;i++){ int64_t s=0; for(j=0;j<4;j++) s+=(int64_t)M[i*4+j]*V[j]; exp[i]=sh(s); }
        for(i=0;i<4;i++) CHK(x.mat_out[i]==exp[i],"matriz 4x4: saida");
        EQ(x.regs[0x660>>2],0,"1o resultado: o latch anterior ainda e 0");
        madam_poke(&x,0x33007fc,0);
        for(i=0;i<4;i++) EQ(x.regs[(0x660>>2)+i],(uint32_t)exp[i],"modo 0 copia o resultado para 0x660..0x66c");
        CHK(x.regs[0x7fc>>2]==0,"0x7fc zerado");
        /* modo 2: 3 colunas, out3 inalterado */
        int32_t o3=x.mat_out[3]; madam_poke(&x,0x33007fc,2);
        for(i=0;i<3;i++){ int64_t s=0; for(j=0;j<3;j++) s+=(int64_t)M[i*4+j]*V[j]; CHK(x.mat_out[i]==sh(s),"matriz 3x3 modo 2"); }
        CHK(x.mat_out[3]==o3,"modo 2 mantem out3");
        EQ(x.regs[0x66c>>2],(uint32_t)o3,"modo 2 tambem faz o latch (out3 antigo)");
        /* modo 3: divide (hi:lo)/z e escala x,y */
        x.regs[0x680>>2]=1; x.regs[0x684>>2]=0x80;
        madam_poke(&x,0x33007fc,3);
        { int64_t s2=0,s1=0,s0=0; for(j=0;j<3;j++){ s2+=(int64_t)M[8+j]*V[j]; s1+=(int64_t)M[4+j]*V[j]; s0+=(int64_t)M[j]*V[j]; }
          int32_t z=sh(s2); int64_t q=(int64_t)(((uint64_t)1<<32)|0x80); if(z) q/=z;
          CHK(x.mat_out[2]==z,"modo 3: z");
          CHK(x.mat_out[1]==(int32_t)((uint64_t)((int64_t)sh(s1)*q)>>32),"modo 3: y*(N/z)");
          CHK(x.mat_out[0]==(int32_t)((uint64_t)((int64_t)sh(s0)*q)>>32),"modo 3: x*(N/z)"); }
        /* z == 0: nao divide, usa N inteiro */
        for(j=0;j<3;j++) x.regs[(0x620>>2)+j]=0;
        madam_poke(&x,0x33007fc,3); CHK(x.mat_out[2]==0,"modo 3 com z=0");
        /* modo > 3: so zera 0x7fc, nenhum latch */
        uint32_t keep=x.regs[0x660>>2]; x.regs[0x7fc>>2]=5; int32_t k0=x.mat_out[0];
        madam_poke(&x,0x33007fc,7); CHK(x.regs[0x7fc>>2]==0 && x.regs[0x660>>2]==keep && x.mat_out[0]==k0,"modo 7 so zera 0x7fc");
        /* outros registradores 0x600..0x7f8 sao crus e sem mascara */
        madam_poke(&x,0x3300604,0xffffffff); EQ(madam_peek(&x,0x3300604),0xffffffff,"0x604 cru");
    }

    /* 4) bloco 0x100..0x1ff e estado do CEL */
    madam_poke(&m,0x3300110,0xcafebabe); EQ(madam_peek(&m,0x3300110),0xcafebabe,"0x110 -> cel_w[4]");
    EQ(m.cel_w[4],0xcafebabe,"cel_w[4]");
    madam_poke(&m,0x3300180,0xaabbccdd); EQ(madam_peek(&m,0x3300180),0xaabb,"0x180: metade alta");
    EQ(madam_peek(&m,0x3300184),0xccdd,"0x180: metade baixa (palavra seguinte)");
    madam_poke(&m,0x33001bc,0x11223344); EQ(madam_peek(&m,0x33001f8),0x1122,"0x1bc alta em 0x1f8");
    EQ(madam_peek(&m,0x33001fc),0x3344,"0x1bc baixa em 0x1fc");
    madam_poke(&m,0x33001c0,0xdead); EQ(madam_peek(&m,0x33001c0),m.cel_w[48],"0x1c0+ (>=0xc0) ignorado");
    EQ(m.cel_w[48],0,"0x1c0 nao grava");
    madam_poke(&m,0x3300130,0); EQ(m.cel_w21c,0,"0x130=0: w21c=0"); EQ(m.cel_w220,0,"0x130=0: w220=0");
    madam_poke(&m,0x3300130,0x15); EQ(m.cel_w21c,(0x40+0x20)*4,"0x130=0x15: w21c"); EQ(m.cel_w[12],0x15,"0x130 guardado");
    madam_poke(&m,0x3300130,0x1); EQ(m.cel_w21c,(0x00+0x20)*4,"0x130=1");
    madam_poke(&m,0x3300130,0x8); EQ(m.cel_w21c,(0x8<<7)*4,"0x130=8: a=(v&8)<<7, b=0");
    madam_poke(&m,0x3300130,0x80);   EQ(m.cel_w21c,((0x80*8)&0x400)*4,"0x130=0x80: b=(v*8)&0x400 (b=0x80 vira 0 aqui)");
    madam_poke(&m,0x3300130,0x100);  EQ(m.cel_w220,(0x20+0)*4,"0x130=0x100: a=0x20, b=(v2*8)&0x400=0");
    madam_poke(&m,0x3300130,0x1100); EQ(m.cel_w220,(0x20+0x40)*4,"0x130=0x1100: a=0x20 (v2&1), b=0x40 (v&0x1000)");
    EQ(madam_peek(&m,0x3300130),0x1100,"0x130 le de volta");

    exec_ret=7; exec_calls=sync_calls=sb=se=0;
    EQ(madam_poke(&m,0x3300100,1),7,"0x100 devolve o stall do motor");
    CHK(exec_calls==1 && last_async==1 && sb==1 && se==1,"0x100: exec async (flag40=0) entre scope_begin/end");
    EQ(madam_peek(&m,0x3300028),0x10,"Peek 0x28 = ocupado"); CHK(madam_cel_busy(&m),"busy");
    EQ(madam_poke(&m,0x3300100,1),0,"0x100 com CEL ocupado nao reinicia"); CHK(exec_calls==1,"sem novo exec");
    madam_poke(&m,0x330010c,0); EQ(madam_peek(&m,0x3300028),0x30,"0x10c pausa"); CHK(!madam_cel_busy(&m),"pausado nao e busy");
    madam_poke(&m,0x330010c,0); EQ(madam_peek(&m,0x3300028),0x30,"0x10c em pausa nao muda");
    madam_poke(&m,0x3300108,0); EQ(madam_peek(&m,0x3300028),0x10,"0x108 continua");
    madam_poke(&m,0x3300108,0); EQ(madam_peek(&m,0x3300028),0x10,"0x108 ocupado nao muda");
    exec_calls=sync_calls=0; madam_poke(&m,0x3300104,0);
    CHK(sync_calls==1 && exec_calls==1 && last_async==0,"0x104: sync e termina com exec(async=0)");
    EQ(madam_peek(&m,0x3300028),0,"0x104 -> ocioso");
    exec_calls=sync_calls=0; madam_poke(&m,0x3300104,0); CHK(sync_calls==1 && exec_calls==0,"0x104 ocioso: so sync");
    /* flag40=1: sincrono, async=0 no inicio e nada ao terminar */
    m.cel_flag40=1; exec_calls=0; madam_poke(&m,0x3300100,1); CHK(exec_calls==1 && last_async==0,"flag40=1: exec async=0");
    exec_calls=0; madam_cel_set(&m,0); CHK(exec_calls==0 && m.cel_state==0,"flag40=1: madam_cel_set(0) so zera");
    m.cel_flag40=0;
    /* CEL desligado: start fica ocupado mas nao chama o motor */
    m.cel_enabled=0; exec_calls=0; madam_poke(&m,0x3300100,1);
    CHK(exec_calls==1 && last_async==0 && m.cel_state==0x10,"cel_enabled=0: o motor roda sincrono (async=0), como em FUN_00489650"); exec_calls=0; madam_cel_set(&m,0); CHK(exec_calls==0 && m.cel_state==0,"cel_enabled=0: terminar nao roda de novo");
    m.cel_enabled=1;
    /* madam_cel_set(0x30) so grava */
    exec_calls=0; m.cel_state=0x10; madam_cel_set(&m,0x30); CHK(m.cel_state==0x30 && exec_calls==0,"cel_set(0x30)");
    /* sem gancho de exec: 0x100 devolve 0 */
    m.cel_state=0; m.cel_exec=NULL; EQ(madam_poke(&m,0x3300100,1),0,"sem cel_exec devolve 0"); m.cel_exec=cexec;

    /* 5) SPORT */
    {
        static uint32_t vram[SPORT_VRAM_WORDS]; sport_t s; memset(&s,0,sizeof s); s.vram=vram;
        for(uint32_t i=0;i<SPORT_VRAM_WORDS;i++) vram[i]=i*2654435761u;
        EQ(sport_peek(&s,0x0),0,"1o peek devolve 0 (src inicial)");
        EQ(sport_peek(&s,0x10),0,"2o peek devolve o anterior (0)");
        EQ(s.src,0x10u<<7,"src = off << 7");
        uint32_t src_before[0x200]; for(int i=0;i<0x200;i++) src_before[i]=vram[(0x10u<<7)+i];
        sport_poke(&s,0x40,0xffffffff);                       /* copia pagina src -> dst = (0x40&0x7fc)<<7 */
        for(int i=0;i<0x200;i++) if(vram[(0x40u<<7)+i]!=src_before[i]){ printf("FALHA: copia total (palavra %d)\n",i); fails++; break; }
        EQ(s.dst,0x40u<<7,"dst");
        /* copia com mascara */
        uint32_t d0=vram[(0x80u<<7)+3]; sport_poke(&s,0x80,0x0000ffff);
        EQ(vram[(0x80u<<7)+3],((d0^src_before[3])&0xffff)^src_before[3],"copia com mascara");
        /* cor + flash write */
        sport_poke(&s,0x2000,0x55aa55aa); EQ(s.fill,0x55aa55aa,"0x2000 grava a cor");
        sport_poke(&s,0x4000+0x100,0xffffffff);
        EQ(vram[0x100*0x80+0],0x55aa55aa,"flash write total (1a palavra)"); EQ(vram[0x100*0x80+0x1ff],0x55aa55aa,"flash write (ultima palavra)");
        uint32_t nxt=vram[0x100*0x80+0x200]; EQ(vram[0x100*0x80+0x200],nxt,"flash nao passa de 0x200 palavras");
        vram[0x104*0x80+5]=0x12345678; sport_poke(&s,0x4000+0x104,0xff00ff00);
        EQ(vram[0x104*0x80+5],((0x12345678u^0x55aa55aau)&0xff00ff00u)^0x55aa55aau,"flash write com mascara");
        sport_poke(&s,0x6000,0x1234); EQ(s.fill,0x55aa55aa,"0x6000.. ignorado");
        /* origem fora da VRAM le zeros em vez de lixo do host */
        sport_peek(&s,0x1ffc); sport_poke(&s,0x0,0xffffffff);
        EQ(vram[0],0,"src fora da VRAM copia zeros");
    }
    puts(fails?"ERRO":"OK"); return fails!=0;
}
