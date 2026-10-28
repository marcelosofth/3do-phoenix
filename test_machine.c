/* Teste de integracao ARM60 <-> barramento <-> CLIO (coerencia com a leitura do .exe, nao com o .exe). */
#include "machine.h"
#include <stdio.h>
#include <string.h>
static int fails;
#define CHK(c,msg) do{ if(!(c)){printf("FALHA: %s\n",msg);fails++;} }while(0)
static int busy; static int cel_runs; static int vtouch;
static void vram_touch(void *u){ (void)u; vtouch++; }
static uint32_t t_madam_poke(void *u,uint32_t a,uint32_t v){ (void)u;(void)v; return a==0x3300100 ? 1000 : 0; }
static uint32_t t_madam_peek(void *u,uint32_t a){ (void)u; return 0x4d000000u|(a&0xffff); }
static int  cel_busy(void *u){ (void)u; return busy; }
static void cel_run(void *u,uint32_t n){ (void)u; if(n==0) busy=0; cel_runs++; }
static void be(uint8_t *p,uint32_t w){ p[0]=w>>24;p[1]=w>>16;p[2]=w>>8;p[3]=w; }

int main(void){
    ph_machine_t m; if(ph_init(&m)) return 2;
    m.madam_poke=t_madam_poke; m.madam_peek=t_madam_peek; m.cel_busy=cel_busy; m.cel_run=cel_run;

    /* 1) mapa de memoria */
    ph_write32(&m,0x1000,0x11223344u);
    CHK(ph_read32(&m,0x1000)==0x11223344u,"RAM read32");
    CHK(ph_read8(&m,0x1000)==0x11 && ph_read8(&m,0x1003)==0x44,"RAM read8 big-endian");
    ph_write8(&m,0x1001,0xaa); CHK(ph_read32(&m,0x1000)==0x11aa3344u,"RAM write8");
    ph_write32(&m,0x200000,0x55); CHK(ph_read32(&m,0x200000)==0x55,"VRAM = RAM continua");
    static uint8_t img[PH_BIOS_BYTES]; memset(img,0,sizeof img);
    be(img,0xdeadbeef); be(img+0x100000,0xcafef00d);
    ph_load_bios(&m,img,sizeof img);
    CHK(ph_read32(&m,0x3000000)==0xdeadbeefu,"BIOS banco 0");
    m.clio.R[0x84>>2]=4; CHK(ph_read32(&m,0x3000000)==0xcafef00du,"BIOS banco 1 (CLIO 0x84 bit2)");
    CHK(ph_read32(&m,0x6000000)==0xdeadbeefu,"alias 0x06000000 = banco 0");
    ph_write32(&m,0x3000000,1); m.clio.R[0x84>>2]=0; CHK(ph_read32(&m,0x3000000)==0xdeadbeefu,"escrita em ROM ignorada");
    ph_write32(&m,0x3140000,0x7a); CHK(ph_read32(&m,0x3140000)==0x7a,"NVRAM (so 0x40000..)");
    CHK(ph_read32(&m,0x3100000)==PH_BADACCE5,"NVRAM fora da janela = 0xbadacce5");
    CHK(ph_read32(&m,0x3500000)==PH_BADACCE5,"regiao vazia = 0xbadacce5");
    CHK(ph_read8(&m,0x3500000)==0,"read8 vazio = 0");
    CHK(ph_fetch(&m,0x3400000)==PH_BADACCE5,"fetch do CLIO = 0xbadacce5");
    CHK(ph_read32(&m,0x3400000)==0x02022000u,"CLIO: ID");
    CHK(ph_read32(&m,0x3300004)==0x4d000004u,"MADAM peek");

    /* 2) programa: timer do CLIO -> FIQ no vetor 0x1c */
    uint32_t bios[] = { 0xe3a00634,   /* MOV r0,#0x03400000 */
                        0xe3a01b01,   /* MOV r1,#0x400      */
                        0xe5801048,   /* STR r1,[r0,#0x48]  habilita IRQ0 bit10 */
                        0xe321f013,   /* MSR CPSR_c,#0x13   libera F */
                        0xeafffffe }; /* B .  */
    for(unsigned i=0;i<5;i++) m.bios[i]=bios[i];
    m.ram[0x1c>>2]=0xe2888001;         /* ADD r8,r8,#1 */
    m.ram[0x20>>2]=0xe5801044;         /* STR r1,[r0,#0x44]  limpa pendencia */
    m.ram[0x24>>2]=0xe25ef004;         /* SUBS pc,r14,#4 */
    m.clio.R[0x108>>2]=2; m.clio.R[0x10c>>2]=5; m.clio.R[0x200>>2]=0x3u<<4;   /* timer 1 com recarga */
    m.cpu.r[15]=0x3000000;
    ph_run(&m,0x4000);
    CHK(m.cpu.fiq[0]>=5,"FIQ do timer foi atendido varias vezes");
    printf("FIQs atendidos: %u em %u ciclos\n",m.cpu.fiq[0],m.total_cycles);
    CHK((m.cpu.cpsr&0x1f)==0x13 || (m.cpu.cpsr&0x1f)==0x11,"modo final SVC ou FIQ");
    CHK(m.cpu.r[0]==0x3400000 && m.cpu.r[1]==0x400,"r0/r1 compartilhados entre bancos");
    CHK(m.cpu.r[15]==0x3000010 || m.cpu.r[15]<0x30,"PC no laco ou no vetor");

    /* 3) CEL: escrita em 0x03300100 para o ARM; so FIQ roda */
    ph_free(&m); ph_init(&m); m.madam_poke=t_madam_poke; m.cel_busy=cel_busy; m.cel_run=cel_run;
    m.ram[0]=0xeafffffe; m.cpu.r[15]=0; 
    ph_write32(&m,0x3300100,1); busy=1; cel_runs=0;
    CHK(m.stall==1000,"MADAM 0x100 => stall");
    uint32_t t0=m.total_cycles; ph_run(&m,512);
    CHK(m.cpu.r[15]==0 && m.total_cycles-t0>=512,"ARM parado durante o CEL");
    ph_run(&m,1024);
    CHK(busy==0 && m.stall==0,"stall esgota e o CEL termina");
    /* 3b) FUN_0058e9f0: so leitura de 32 bits na regiao 2 com *(maq+0x3c)==0; escrita de 32 bits nao chama */
    ph_free(&m); ph_init(&m); m.vram_touch=vram_touch; vtouch=0;
    m.vram_flag=0;  /* ph_init agora poe vram_flag=1 (reset real, REA em FUN_004a7f60); aqui o teste quer
                        exercitar o caminho flag==0 de proposito, entao forca antes de medir */
    ph_read32(&m,0x200000);             CHK(vtouch==1,"read32 regiao 2 com flag 0 chama vram_touch");
    m.vram_flag=1; ph_read32(&m,0x200000); CHK(vtouch==1,"read32 regiao 2 com flag 1 nao chama");
    m.vram_flag=0; ph_write32(&m,0x200000,7); CHK(vtouch==1,"write32 regiao 2 nao chama vram_touch");
    ph_read32(&m,0x100000);             CHK(vtouch==1,"read32 regiao 1 nao chama vram_touch");

    /* 4) 8 bits e blocos por instrucao ARM (STRB/LDRB/STM/LDM) contra o mapa de enderecos:
     *    RAM aceita tudo; ROM ignora escrita; NVRAM aceita byte; regiao do CLIO le 0 por byte. */
    ph_free(&m); ph_init(&m); m.madam_poke=t_madam_poke; m.madam_peek=t_madam_peek; m.cel_busy=cel_busy; m.cel_run=cel_run;
    busy=0;
    m.bios[0]=0xdeadbeef; m.bios[1]=0x01020304; m.bios[2]=0x05060708;
    {
        static const uint32_t prog[] = {
            0xe3a00a02,   /* MOV  r0,#0x2000                 */
            0xe3a010aa,   /* MOV  r1,#0xaa                   */
            0xe5c01001,   /* STRB r1,[r0,#1]       RAM       */
            0xe5d02001,   /* LDRB r2,[r0,#1]       r2=0xaa   */
            0xe3a03001,   /* MOV  r3,#1                      */
            0xe3a04002,   /* MOV  r4,#2                      */
            0xe3a05003,   /* MOV  r5,#3                      */
            0xe3a06a03,   /* MOV  r6,#0x3000                 */
            0xe8860038,   /* STMIA r6,{r3,r4,r5}   RAM       */
            0xe3a03000,   /* MOV  r3,#0                      */
            0xe3a04000,   /* MOV  r4,#0                      */
            0xe3a05000,   /* MOV  r5,#0                      */
            0xe8960038,   /* LDMIA r6,{r3,r4,r5}   volta 1,2,3 */
            0xe3a07403,   /* MOV  r7,#0x03000000   ROM       */
            0xe8870038,   /* STMIA r7,{r3,r4,r5}   ignorada  */
            0xe5c71000,   /* STRB r1,[r7]          ignorada  */
            0xe3a087c5,   /* MOV  r8,#0x03140000   NVRAM     */
            0xe5c81000,   /* STRB r1,[r8]          aceita    */
            0xe3a09634,   /* MOV  r9,#0x03400000   CLIO      */
            0xe5d9a000,   /* LDRB r10,[r9]         le 0      */
            0xeafffffe }; /* B .                             */
        for (unsigned i = 0; i < sizeof prog / sizeof *prog; i++) m.ram[(0x1000 >> 2) + i] = prog[i];
    }
    m.cpu.r[15]=0x1000;
    ph_run(&m,512);
    CHK(m.cpu.r[2]==0xaa,"LDRB le o byte escrito por STRB");
    CHK(ph_read32(&m,0x2000)==0x00aa0000u,"STRB em RAM: byte 1 do inteiro big-endian");
    CHK(ph_read32(&m,0x3000)==1 && ph_read32(&m,0x3004)==2 && ph_read32(&m,0x3008)==3,"STM em RAM grava palavra a palavra");
    CHK(m.cpu.r[3]==1 && m.cpu.r[4]==2 && m.cpu.r[5]==3,"LDM le de volta o bloco");
    CHK(m.bios[0]==0xdeadbeef && m.bios[1]==0x01020304 && m.bios[2]==0x05060708,"STM/STRB em ROM ignorados");
    CHK(ph_read8(&m,0x3140000)==0xaa,"STRB em NVRAM aceito");
    CHK(m.cpu.r[10]==0,"LDRB na regiao do CLIO le 0");
    CHK(m.cpu.r[15]==0x1000+20*4,"programa chegou ao B . final");

    /* 5) Parte 12: fetch/read8/write8/blocos (fecha os 5 itens em aberto da Parte 11) */
    ph_free(&m); ph_init(&m); m.vram_touch=vram_touch; vtouch=0;
    m.vram_flag=0;  /* idem acima: forca o caminho flag==0 de proposito para o resto deste bloco */
    ph_write32(&m,0x200000,0x11223344u);

    /* fetch NUNCA chama vram_touch, nem na regiao 2 com vram_flag==0 (ao contrario do read32) */
    (void)ph_fetch(&m,0x200000); CHK(vtouch==0,"fetch na regiao 2 nao chama vram_touch (FUN_004a0030 nao tem a call)");

    /* read8 segue a MESMA regra do read32: so chama se vram_flag==0 */
    CHK(ph_read8(&m,0x200000)==0x11,"read8 regiao 2: byte 0 do inteiro big-endian");
    CHK(vtouch==1,"read8 regiao 2 com vram_flag==0 chama vram_touch (igual ao read32)");
    m.vram_flag=1; (void)ph_read8(&m,0x200001); CHK(vtouch==1,"read8 regiao 2 com vram_flag==1 nao chama");
    m.vram_flag=0;

    /* write8 tambem segue a mesma regra na regiao 2 (ao contrario do write32, que nunca chama) */
    ph_write8(&m,0x200000,0x99); CHK(vtouch==2,"write8 regiao 2 com vram_flag==0 chama vram_touch");
    m.vram_flag=1;
    CHK(ph_read32(&m,0x200000)==0x99223344u,"write8 gravou o byte certo (addr^3)"); /* flag=1: nao soma vtouch */
    ph_write8(&m,0x200001,0x55); CHK(vtouch==2,"write8 regiao 2 com vram_flag==1 nao chama");
    m.vram_flag=0;

    /* regiao 0/1 nunca chama vram_touch, nem por read8/write8 */
    int before = vtouch;
    (void)ph_read8(&m,0x100000); ph_write8(&m,0x100000,1); CHK(vtouch==before,"read8/write8 regiao 0/1 nunca chamam vram_touch");

    /* NVRAM por byte: indice = (addr>>2)&0x7fff SEM xor 3 (confirmado por objdump; RAM/BIOS usam ^3) */
    ph_write8(&m,0x3140000,0xab);                 /* addr>>2 & 0x7fff = 0 */
    CHK(m.nvram[0]==0xab,"write8 NVRAM: indice (addr>>2)&0x7fff, sem ^3");
    CHK(ph_read8(&m,0x3140000)==0xab,"read8 NVRAM le de volta com o mesmo indice");
    ph_write8(&m,0x3140004,0xcd);                  /* addr>>2 & 0x7fff = 1 */
    CHK(m.nvram[1]==0xcd && ph_read8(&m,0x3140004)==0xcd,"write8/read8 NVRAM indice 1 (addr 0x3140004)");
    m.bios[0]=0xdeadbeef;
    ph_write8(&m,0x3000000,1);                     /* BIOS: escrita de byte ignorada (sem case 0x30/0x60) */
    CHK(m.bios[0]==0xdeadbeef,"write8 em BIOS ignorado (nao ha case 0x30/0x60 em FUN_004a0530)");

    /* ponteiros de bloco (FUN_004a0090/004a0340): so aceleracao, nao usados pelo laco hoje */
    const uint32_t *rp = ph_block_read_ptr(&m,0x1000,0x10);
    CHK(rp!=NULL && rp[0]==ph_read32(&m,0x1000),"ph_block_read_ptr RAM dentro da mesma regiao");
    CHK(ph_block_read_ptr(&m,0x1ffff0,0x20)==NULL,"ph_block_read_ptr NULL se o bloco cruza regiao (1->2)");
    uint32_t *wp = ph_block_write_ptr(&m,0x2000,0x10);
    CHK(wp!=NULL,"ph_block_write_ptr dentro da RAM (0..0x2fffff)");
    CHK(ph_block_write_ptr(&m,0x2ffff8,0x10)==NULL,"ph_block_write_ptr NULL se ultrapassa 0x2fffff");
    CHK(ph_block_write_ptr(&m,0x3300000,4)==NULL,"ph_block_write_ptr NULL fora da RAM (MADAM)");


    /* 6) Parte 13: MADAM e SPORT internos (sem ganchos) pelo barramento */
    ph_free(&m); ph_init(&m);
    ph_write32(&m,0x3300004,0); CHK(ph_read32(&m,0x3300004)==0x29,"MADAM interno: poke 0x04 -> 0x29");
    ph_write32(&m,0x3300540,0x12345678); CHK(ph_read32(&m,0x3300540)==0x345678,"MADAM interno: 0x540 mascarado");
    ph_write32(&m,0x3300100,1);                                    /* sem cel_exec: termina na hora, stall 0 */
    CHK(m.stall==0 && m.madam.cel_state==0 && ph_read32(&m,0x3300028)==0,"CEL interno: 0x100 sem motor termina e volta a ocioso");
    ph_write32(&m,0x3200000+0x2000,0xdeadbeef);                    /* cor do SPORT */
    ph_write32(&m,0x3200000+0x4000+0x10,0xffffffff);               /* flash write na pagina 0x10 */
    CHK(ph_read32(&m,0x200000+(0x10u*0x80u)*4u)==0xdeadbeefu,"SPORT interno: flash write chega na VRAM (RAM+2MB)");
    ph_write32(&m,0x200000+0x200*4,0x55);                          /* VRAM palavra 0x200 = inicio da pagina de origem 4 */
    (void)ph_read32(&m,0x3200000+0x4);                             /* Peek: src = 4<<7 = 0x200 palavras */
    ph_write32(&m,0x3200000+0x20,0xffffffff);                      /* copia a pagina para o destino (0x20<<7 = 0x1000 palavras) */
    CHK(ph_read32(&m,0x200000+0x1000*4)==0x55,"SPORT interno: copia de pagina pelo barramento");
    CHK(ph_read32(&m,0x3200000+0x8)==(0x4u<<7),"SPORT interno: Peek devolve a origem anterior");

    ph_free(&m);
    puts(fails?"ERRO":"OK"); return fails!=0;
}
