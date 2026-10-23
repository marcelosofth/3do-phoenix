#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"
extern void (*cd_trace_hook)(const char *);
static ph_machine_t m;
static uint32_t (*o_fetch)(void*,uint32_t); static uint8_t (*o_rd8)(void*,uint32_t);
static void show(uint32_t a,int n){ for(int i=0;i<n;i++){ uint8_t c=o_rd8(&m,a+i); printf("%02x(%c) ",c,(c>=32&&c<127)?c:'.'); } }
static uint32_t w_fetch(void*u,uint32_t a){
  if(a==0x88790){ uint32_t r0=m.cpu.r[0],r1=m.cpu.r[1],r2=m.cpu.r[2];
    printf("[cyc=%u] cmp(0x88790) lr=%08x  n=%u\n   A@%08x: ",m.total_cycles,m.cpu.r[14],r1,r0); show(r0,r1>8?8:r1); printf("\n   B@%08x: ",r2); show(r2,r1>8?8:r1); printf("\n"); }
  return o_fetch(u,a); }
static void cdtr(const char*s){ if(!strncmp(s,"cmd 10",6)||!strncmp(s,"cmd 8",5)||!strncmp(s,"cmd 0",5)) printf("[cyc=%u] CD> %s\n",m.total_cycles,s); }
int main(int argc,char**argv){
  ph_init(&m); FILE*f=fopen(argv[1],"rb"); static uint8_t b[0x200000]; size_t n=fread(b,1,sizeof b,f); fclose(f);
  ph_load_bios(&m,b,n); m.clio.R[0x28>>2]=1; if(ph_cd_insert(&m,argv[2])) return 1;
  o_fetch=m.cpu.fetch; o_rd8=m.cpu.rd8; m.cpu.fetch=w_fetch; cd_trace_hook=cdtr;
  int it=argc>3?atoi(argv[3]):2000; for(int i=0;i<it;i++) ph_run(&m,0x10000); return 0; }
