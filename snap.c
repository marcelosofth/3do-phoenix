#include <stdio.h>
#include <stdlib.h>
#include "machine.h"
static ph_machine_t m;
int main(int argc,char**argv){
  ph_init(&m); FILE*f=fopen(argv[1],"rb"); static uint8_t b[0x200000]; size_t n=fread(b,1,sizeof b,f); fclose(f);
  ph_load_bios(&m,b,n); m.clio.R[0x28>>2]=1; if(ph_cd_insert(&m,argv[2])) return 1;
  uint32_t until=strtoul(argv[3],0,0);
  while(m.total_cycles<until) ph_run(&m,0x4000);
  FILE*d=fopen(argc>4?argv[4]:"/tmp/ram.bin","wb"); fwrite(m.ram,4,0xC0000,d); fclose(d);
  printf("cyc=%u pc=%08x cpsr=%08x\n",m.total_cycles,m.cpu.r[15],m.cpu.cpsr);
  for(int k=0;k<16;k++) printf("r%d=%08x ",k,m.cpu.r[k]); puts("");
  uint32_t *R=m.clio.R;
  printf("IRQ0 pend=%08x en=%08x | IRQ1 pend=%08x en=%08x\n",R[0x40>>2],R[0x48>>2],R[0x60>>2],R[0x68>>2]);
  printf("timers: 100=%08x 104=%08x 108=%08x 10c=%08x 110=%08x 114=%08x 118=%08x 11c=%08x\n",R[0x100>>2],R[0x104>>2],R[0x108>>2],R[0x10c>>2],R[0x110>>2],R[0x114>>2],R[0x118>>2],R[0x11c>>2]);
  printf("timer ctl: 200=%08x 204=%08x 208=%08x 20c=%08x  304=%08x 400=%08x\n",R[0x200>>2],R[0x204>>2],R[0x208>>2],R[0x20c>>2],R[0x304>>2],R[0x400>>2]);
  return 0; }
