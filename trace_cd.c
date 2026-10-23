#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"
extern void (*cd_trace_hook)(const char *);
static ph_machine_t m;
static void tr(const char *s){ fprintf(stdout,"[it pc=%08x cyc=%u] CD> %s\n", m.cpu.r[15], m.total_cycles, s); }
extern void (*clio_trace_hook)(const char *);
static void tr2(const char *s){ printf("[pc=%08x cyc=%u] CLIO> %s\n", m.cpu.r[15], m.total_cycles, s);}
int main(int argc,char**argv){
  const char*bios=argv[1], *img=argv[2]; int iters=argc>3?atoi(argv[3]):400;
  ph_init(&m);
  FILE*f=fopen(bios,"rb"); static uint8_t b[0x200000]; size_t n=fread(b,1,sizeof b,f); fclose(f);
  ph_load_bios(&m,b,n); m.clio.R[0x28>>2]=1;
  if(ph_cd_insert(&m,img)){puts("imagem falhou");return 1;}
  printf("setores=%u\n", m.cd.num_sectors);
  cd_trace_hook=tr; clio_trace_hook=tr2;
  for(int i=0;i<iters;i++){ ph_run(&m,0x40000);
    if(i%50==0) printf("it=%d pc=%08x cyc=%u lba=%d remain=%d S=%02x R=%02x\n",i,m.cpu.r[15],m.total_cycles,m.cd.lba,m.cd.remain,m.cd.S,m.cd.R); }
  return 0;
}
