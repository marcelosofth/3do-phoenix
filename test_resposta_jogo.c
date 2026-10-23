#include <stdio.h>
#include <stdlib.h>
#include "machine.h"
static ph_machine_t m; static int mode;
static int16_t st(unsigned p,unsigned d,unsigned i,unsigned id){
  if(!mode||p!=0) return 0;
  /* pulsa Start/A/Baixo a cada ~0,5 s emulado depois de 150M ciclos */
  if(m.total_cycles<150000000u) return 0;
  unsigned ph=(m.total_cycles/6000000u)%4;
  return (ph==1 && (id==RETRO_DEVICE_ID_JOYPAD_START||id==RETRO_DEVICE_ID_JOYPAD_B||id==RETRO_DEVICE_ID_JOYPAD_Y)) || (ph==3&&id==RETRO_DEVICE_ID_JOYPAD_DOWN);
}
static void pl(void){}
int main(int argc,char**argv){
  mode=atoi(argv[3]); ph_init(&m); FILE*f=fopen(argv[1],"rb"); static uint8_t b[0x200000]; size_t n=fread(b,1,sizeof b,f); fclose(f);
  ph_load_bios(&m,b,n); m.clio.R[0x28>>2]=1; if(ph_cd_insert(&m,argv[2])) return 1;
  joybus_set_callbacks(&m.joybus,pl,st);
  unsigned maxc=strtoul(argv[4],0,0), nxt=0; unsigned long h=0;
  while(m.total_cycles<maxc){ joybus_poll(&m.joybus); ph_run(&m,0x4000*4);
    if(m.total_cycles>=nxt){ nxt+=50000000u; unsigned long hh=1469598103934665603UL; for(int i=0;i<m.vdlp.width*m.vdlp.lines*3;i++) hh=(hh^m.vdlp.rgb[i])*1099511628211UL; printf("cyc=%u PC=%08x lba=%d frame_hash=%016lx\n",m.total_cycles,m.cpu.r[15],m.cd.lba,hh);} }
  return 0; }
