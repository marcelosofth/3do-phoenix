#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "machine.h"
extern void (*cd_trace_hook)(const char *);
static void be32(uint8_t*p,uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static void mkiso(const char*fn){
  static uint8_t d[64*2048]; memset(d,0,sizeof d);
  uint8_t*h=d; h[0]=1; memset(h+1,0x5a,5); h[6]=1;
  strcpy((char*)h+0x28,"TESTDISC"); be32(h+0x48,1234); be32(h+0x4c,2048); be32(h+0x50,64);
  be32(h+0x54,1234); be32(h+0x58,1); be32(h+0x5c,2048); be32(h+0x60,0); be32(h+0x64,0);
  be32(h+0x68,1); /* copia da raiz no bloco 1 */
  uint8_t*r=d+2048; be32(r,0xffffffffu); be32(r+4,0xffffffffu); be32(r+8,0); be32(r+12,0x14+72+4); be32(r+16,0x14);
  uint8_t*e=r+0x14; be32(e,0x02); be32(e+4,5678); memcpy(e+8,"*lmc",4); be32(e+12,2048); be32(e+16,2048); be32(e+20,1);
  be32(e+24,0); be32(e+28,0); strcpy((char*)e+32,"LaunchMe"); be32(e+64,0); be32(e+68,2);
  be32(e+72-0,0); /* fim */
  FILE*f=fopen(fn,"wb"); fwrite(d,1,sizeof d,f); fclose(f);
}
static void tr(const char*s){ fprintf(stderr,"CD> %s\n",s);}
int main(int argc,char**argv){
  const char*bios=argc>1?argv[1]:"BIOS/fz1.bin"; const char*iso=argc>2?argv[2]:"/tmp/o.iso";
  if(argc<=2) mkiso(iso);
  static ph_machine_t m; ph_init(&m);
  FILE*f=fopen(bios,"rb"); static uint8_t b[0x200000]; size_t n=fread(b,1,sizeof b,f); fclose(f);
  ph_load_bios(&m,b,n); m.clio.R[0x28>>2]=1;
  if(ph_cd_insert(&m,iso)) {puts("iso falhou");return 1;}
  cd_trace_hook=tr;
  uint32_t last=0; int same=0;
  for(int i=0;i<1200;i++){
    ph_run(&m,0x40000);
    uint32_t pc=m.cpu.r[15];
    if(i==300){for(int k=0;k<16;k++)fprintf(stderr,"r%d=%08x ",k,m.cpu.r[k]);fprintf(stderr,"\n");} if(i%100==0||i>=1190) fprintf(stderr,"it=%d pc=%08x cpsr=%08x cyc=%u irq0=%08x/%08x irq1=%08x/%08x\n",i,pc,m.cpu.cpsr,m.total_cycles,m.clio.R[0x40>>2],m.clio.R[0x48>>2],m.clio.R[0x60>>2],m.clio.R[0x68>>2]);
  }
  return 0;
}
