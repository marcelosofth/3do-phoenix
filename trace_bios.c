#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"
extern void (*cd_trace_hook)(const char *);
extern void (*clio_trace_hook)(const char *);
static ph_machine_t m;
static int tracing, ncmd10, done_win, win_no, want_win = 3;
static FILE *lf;
static uint32_t (*o_fetch)(void*,uint32_t); static uint32_t (*o_rd32)(void*,uint32_t);
static uint8_t (*o_rd8)(void*,uint32_t); static void (*o_wr32)(void*,uint32_t,uint32_t); static void (*o_wr8)(void*,uint32_t,uint8_t);
static uint32_t w_fetch(void*u,uint32_t a){ if(tracing) fprintf(lf,"I %08x\n",a); return o_fetch(u,a); }
static uint32_t w_rd32(void*u,uint32_t a){ uint32_t v=o_rd32(u,a); if(tracing && (a>=0x03000000u || (a>=0x50000&&a<0x50810))) fprintf(lf,"R %08x = %08x\n",a,v); return v; }
static uint8_t w_rd8(void*u,uint32_t a){ uint8_t v=o_rd8(u,a); if(tracing && (a>=0x03000000u || (a>=0x50000&&a<0x50810))) fprintf(lf,"r %08x = %02x\n",a,v); return v; }
static void w_wr32(void*u,uint32_t a,uint32_t v){ if(tracing && (a>=0x03000000u || (a>=0x50000&&a<0x50810))) fprintf(lf,"W %08x <- %08x  (pc=%08x)\n",a,v,m.cpu.r[15]); o_wr32(u,a,v); }
static void w_wr8(void*u,uint32_t a,uint8_t v){ if(tracing && (a>=0x03000000u || (a>=0x50000&&a<0x50810))) fprintf(lf,"w %08x <- %02x\n",a,v); o_wr8(u,a,v); }
static void cdtr(const char*s){
  if(!strncmp(s,"cmd 10",6)){ if(tracing){ tracing=0; win_no++; fprintf(lf,"# fim janela, prox: %s\n",s); done_win=1;} ncmd10++; }
  if(tracing) fprintf(lf,"# CD %s\n",s);
}
static void cltr(const char*s){ if(strstr(s,"IRQ29") && ncmd10>=want_win && !tracing && !done_win){ tracing=1; fprintf(lf,"# inicio: %s pc=%08x\n",s,m.cpu.r[15]); } if(tracing) fprintf(lf,"# CLIO %s\n",s); }
int main(int argc,char**argv){
  const char*bios=argv[1], *img=argv[2]; lf=fopen(argv[3],"w");
  ph_init(&m);
  FILE*f=fopen(bios,"rb"); static uint8_t b[0x200000]; size_t n=fread(b,1,sizeof b,f); fclose(f);
  ph_load_bios(&m,b,n); m.clio.R[0x28>>2]=1;
  if(ph_cd_insert(&m,img)) return 1;
  o_fetch=m.cpu.fetch;o_rd32=m.cpu.rd32;o_rd8=m.cpu.rd8;o_wr32=m.cpu.wr32;o_wr8=m.cpu.wr8;
  m.cpu.fetch=w_fetch;m.cpu.rd32=w_rd32;m.cpu.rd8=w_rd8;m.cpu.wr32=w_wr32;m.cpu.wr8=w_wr8;
  cd_trace_hook=cdtr; clio_trace_hook=cltr;
  for(int i=0;i<4000 && !done_win;i++) ph_run(&m,0x4000*4);
  {FILE*d=fopen("/tmp/ram.bin","wb"); fwrite(m.ram,4,0x40000,d); fclose(d);} fclose(lf); printf("ncmd10=%d done=%d\n",ncmd10,done_win); return 0; }
