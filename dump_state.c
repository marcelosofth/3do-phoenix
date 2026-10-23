#include <stdio.h>
#include <stdlib.h>
#include "machine.h"
static ph_machine_t m;
extern unsigned long ph_irq_cnt[2][32];
int main(int argc,char**argv){
  ph_init(&m); FILE*f=fopen(argv[1],"rb"); static uint8_t b[0x200000]; size_t n=fread(b,1,sizeof b,f); fclose(f);
  ph_load_bios(&m,b,n); m.clio.R[0x28>>2]=1; ph_cd_insert(&m,argv[2]);
  int iters=atoi(argv[3]);
  for(int i=0;i<iters;i++){ ph_run(&m,0x40000); }
  uint32_t *R=m.clio.R;
  printf("pc=%08x cpsr=%08x\n",m.cpu.r[15],m.cpu.cpsr);
  printf("IRQ0 pend=%08x en=%08x | IRQ1 pend=%08x en=%08x\n",R[0x40>>2],R[0x48>>2],R[0x60>>2],R[0x68>>2]);
  printf("ctl200=%08x ctl208=%08x R220=%03x vint0=%08x vint1=%08x\n",R[0x200>>2],R[0x208>>2],R[0x220>>2],R[0x8>>2],R[0xc>>2]);
  for(int i=0;i<16;i++) printf("t%02d cnt=%04x rld=%04x ctl=%x\n",i,R[(0x100+8*i)>>2]&0xffff,R[(0x104+8*i)>>2]&0xffff,((i<8?R[0x200>>2]:R[0x208>>2])>>((i*4)&0x1c))&0xf);
  for(int k=0;k<2;k++)for(int b=0;b<32;b++) if(ph_irq_cnt[k][b]) printf("IRQ%d bit%d raised %lu\n",k,b,ph_irq_cnt[k][b]);
  return 0;}
