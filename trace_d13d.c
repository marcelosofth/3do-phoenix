/* trace_d13d.c: uso: trace_d13d BIOS.bin jogo.iso|chd [iteracoes=400] [addr_hex_p/_watch, ex: 1fd3c0]
 * 1) imprime r0-r15 (LR!) na 1a instrucao em que algum registrador vira 0xD13D100D e quando algo grava esse valor;
 * 2) se addr dado, loga as primeiras 40 escritas (valor, PC, LR) em [addr, addr+0x40) -> quem cria o TCB. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"
static ph_machine_t m;
static uint32_t (*o_fetch)(void*,uint32_t); static void (*o_wr32)(void*,uint32_t,uint32_t);
static uint32_t prev_pc; static int found=0, nw=0, nwatch=0; static uint32_t wlo=0, whi=0;
#define MAGIC 0xD13D100Du
static void dump(const char*t){ printf("%s cyc=%u prevpc=%08x pc=%08x cpsr=%08x\n ",t,m.total_cycles,prev_pc,m.cpu.r[15],m.cpu.cpsr);
  for(int k=0;k<16;k++) printf("r%d=%08x ",k,m.cpu.r[k]); printf("\n"); }
static uint32_t w_fetch(void*u,uint32_t a){
  if(!found){ for(int k=0;k<15;k++) if(m.cpu.r[k]==MAGIC){ found=1; dump("REG HOLDS MAGIC (first)"); break; } }
  prev_pc=a; return o_fetch(u,a); }
static void w_wr32(void*u,uint32_t a,uint32_t v){
  if(v==MAGIC && nw<5){ nw++; printf("WRITE MAGIC to %08x by pc~%08x\n",a,prev_pc); dump("  ctx"); }
  if(wlo && a>=wlo && a<whi && nwatch<40){ nwatch++; printf("WATCH W %08x=%08x pc=%08x lr=%08x cyc=%u\n",a,v,prev_pc,m.cpu.r[14],m.total_cycles); }
  o_wr32(u,a,v); }
int main(int argc,char**argv){
  ph_init(&m); FILE*f=fopen(argv[1],"rb"); static uint8_t b[0x200000]; size_t n=fread(b,1,sizeof b,f); fclose(f);
  ph_load_bios(&m,b,n); m.clio.R[0x28>>2]=1;
  if(ph_cd_insert(&m,argv[2])) return 1;
  o_fetch=m.cpu.fetch; o_wr32=m.cpu.wr32; m.cpu.fetch=w_fetch; m.cpu.wr32=w_wr32;
  if(argc>4){ wlo=strtoul(argv[4],0,16); whi=wlo+0x40; }
  int it=argc>3?atoi(argv[3]):400; for(int i=0;i<it;i++) ph_run(&m,0x40000);
  printf("done found=%d\n",found); return 0; }
