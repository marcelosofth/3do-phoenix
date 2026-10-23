/* shot.c - harness: roda BIOS+imagem, loga chamadas ao CEL engine e grava quadros PPM.
 * Uso: shot <bios> <img> <ciclos_max> <passo_ppm_em_ciclos> [logCEL_max]
 * Build: ver LEIA-ME_shot.txt (inclui machine.c) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.c"
static ph_machine_t m;
static unsigned swihist[0x40000]; static char dbgline[256]; static int dbgn; static int swiw=0;
static int swi_h(void*u,uint32_t imm){
  if(imm==0x1000e){ char c=(char)m.cpu.r[0]; if(c=='\n'||dbgn>250){dbgline[dbgn]=0;printf("DBG[%u]: %s\n",m.total_cycles,dbgline);dbgn=0;} else if(c!='\r') dbgline[dbgn++]=c; return 0; }
  if(m.total_cycles>170000000u && m.total_cycles<171000000u){ {uint32_t ct=m.ram[(0x20230+0x98)/4]; const char*cn=ct==0x1ef1f0?"launchme":ct==0x1f14d0?"eventbroker":ct==0x1edf20?"audio":ct==0x1f21e0?"shell":ct==0x1fc4b0?"FileSystem":ct==0x1f34b0?"Aahz":ct==0x1ff840?"Operator":ct==0x1fd3c0?"MKEdaemon":ct==0x1fede0?"Idle":"?"; printf("  [%s] ",cn);} printf("  SWI %05x pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x\n",imm,m.cpu.r[15],m.cpu.r[0],m.cpu.r[1],m.cpu.r[2],m.cpu.r[3]); }
  if(imm==0x2002d && m.total_cycles>170000000u){ static int dn=0; if(dn++<2){ uint32_t r2=m.cpu.r[2], r3=m.cpu.r[3];
     printf("  DUMP 2002d r2=%08x:",r2); for(int i=0;i<24;i++) printf(" %08x", m.ram[r2/4+i]); printf("\n");
     printf("  DUMP 2002d r3=%08x:",r3); for(int i=0;i<24;i++) printf(" %08x", m.ram[r3/4+i]); printf("\n"); } }
  if(imm==0x20017 && m.total_cycles>170000000u){ static int dn2=0; if(dn2++<2){ uint32_t r0=m.cpu.r[0]; printf("  DUMP 20017 r0=%08x r2=%08x\n",r0,m.cpu.r[2]); } }
  if(imm<0x40000) swihist[imm]++; return 0; }
extern void (*cd_trace_hook)(const char *);
static void cdtr(const char*t){ if(!strncmp(t,"CDT cmd",7)&&(t[8]=='1'&&t[9]=='0'||t[8]=='0'&&t[9]=='6'||!strncmp(t+8,"10",2))) printf("CD@%u %s\n",m.total_cycles,t); if(strstr(t,"FORA")) printf("CD@%u %s\n",m.total_cycles,t); }
static int g_btn=-1; static uint32_t g_t0=0,g_t1=0;
static void poll_cb(void){}
static int16_t btn_cb(unsigned port,unsigned dev,unsigned idx,unsigned id){ if(g_btn<0||port!=0) return 0; return (id==(unsigned)g_btn && m.total_cycles>=g_t0 && m.total_cycles<g_t1)?1:0; }
static int nonzero_chain, cel_calls, cel_log_max = 40, wlog = 0;
static void (*o_wr32)(void*,uint32_t,uint32_t);
static void w_wr32(void*u,uint32_t a,uint32_t v){
  if(wlog && a>=0x03300000u && a<0x03300800u){ uint32_t o=a&0x7fc;
    if((o>=0x100&&o<0x1c0)||(o>=0x5a0&&o<0x5c0)||o==0x580) { printf("  W %08x <- %08x pc=%08x cyc=%u\n",a,v,m.cpu.r[15],m.total_cycles); if(--wlog==0) printf("  (fim log W)\n"); } }
  o_wr32(u,a,v); }
static uint32_t (*o_cel)(void*,uint8_t);
static uint32_t w_cel(void *u, uint8_t async){
  cel_calls++; if(!async && (m.madam.dma_arr[1]&0xfffffc)) nonzero_chain++;
  if(!async && cel_calls <= cel_log_max){
    uint32_t nx = m.madam.dma_arr[1] & 0xfffffc;
    printf("  dma_arr=%08x %08x %08x %08x %08x %08x %08x %08x\n", m.madam.dma_arr[0],m.madam.dma_arr[1],m.madam.dma_arr[2],m.madam.dma_arr[3],m.madam.dma_arr[4],m.madam.dma_arr[5],m.madam.dma_arr[6],m.madam.dma_arr[7]);
    printf("CEL#%d cyc=%u pc=%08x next=%06x pixc=%08x clip=%08x rd=%08x wr=%08x\n", cel_calls, m.total_cycles, m.cpu.r[15], nx,
       m.madam.cel_w[4], m.madam.cel_w[13], m.madam.cel_w[14], m.madam.cel_w[15]);
    int n = 0;
    while(nx && nx < 0x300000 && n < 3){
      const uint32_t *w = m.ram + nx/4;
      printf("   CCB@%06x:", nx); for(int i=0;i<15;i++) printf(" %08x", w[i]); printf("\n");
      if(w[1]==0 || (w[1]&1)) break; nx = w[1] & 0xfffffc; n++;
    }
  }
  unsigned d0 = m.cel.drawn, u0 = m.cel.unsupported;
  uint32_t r = o_cel(u, async);
  if(!async && cel_calls <= cel_log_max) printf("   -> drawn+%u unsup+%u\n", m.cel.drawn-d0, m.cel.unsupported-u0);
  return r;
}
static void ppm(const char *fn){
  FILE *f = fopen(fn,"wb"); if(!f) return;
  int w = m.vdlp.width, h = m.vdlp.lines; fprintf(f,"P6\n%d %d\n255\n", w, h);
  unsigned nz = 0; for(int i=0;i<w*h*3;i++) nz += m.vdlp.rgb[i] != 0;
  fwrite(m.vdlp.rgb,1,(size_t)w*h*3,f); fclose(f);
  { uint32_t h=m.vdlp.headvdl; printf("  VDL@%06x:",h); for(int i=0;i<16;i++) printf(" %08x", m.ram[h/4+i]); printf("\n");
    for(int b=0;b<2;b++){ uint32_t base=b?0x225800:0x200000; unsigned z=0; for(uint32_t i=0;i<0x25800/4;i++) z+= m.ram[base/4+i]!=0; printf("  buf@%06x nz_words=%u\n",base,z);} }
  printf("PPM %s %dx%d nao-nulos=%u  headvdl=%08x clutdma=%08x bg=%06x outctl=%08x\n", fn, w, h, nz, m.vdlp.headvdl, m.vdlp.clutdma, m.vdlp.bg, m.vdlp.outctl);
}
int main(int argc,char**argv){
  uint32_t maxc = strtoul(argv[3],0,0), step = strtoul(argv[4],0,0); if(argc>5) cel_log_max = atoi(argv[5]);
  if(argc>6){ g_btn=atoi(argv[6]); g_t0=strtoul(argv[7],0,0); g_t1=strtoul(argv[8],0,0); }
  ph_init(&m);
  FILE*f=fopen(argv[1],"rb"); static uint8_t b[0x200000]; size_t n=fread(b,1,sizeof b,f); fclose(f);
  ph_load_bios(&m,b,n); m.clio.R[0x28>>2]=1;
  if(ph_cd_insert(&m,argv[2])) return 1;
  joybus_set_callbacks(&m.joybus,poll_cb,btn_cb);
  cd_trace_hook=cdtr; m.cpu.swi_hook=swi_h; o_wr32=m.cpu.wr32; m.cpu.wr32=w_wr32;
  o_cel = m.madam.cel_exec; m.madam.cel_exec = w_cel;
  uint32_t next = step; int k = 0;
  while(m.total_cycles < maxc){
    if(!wlog && m.total_cycles>161500000u && m.total_cycles<161600000u) wlog=60;
    joybus_poll(&m.joybus); ph_run(&m, 0x4000*4);
    if(m.total_cycles >= next){ char fn[64]; snprintf(fn,sizeof fn,"/tmp/f%02d.ppm",k++); ppm(fn);
      printf("  nonzero_chain=%d\n", nonzero_chain); printf("  cyc=%u PC=%08x lba=%d celcalls=%d drawn=%u unsup=%u\n", m.total_cycles, m.cpu.r[15], m.cd.lba, cel_calls, m.cel.drawn, m.cel.unsupported); next += step; }
  }
  printf("CLIO IRQ0 st=%08x en=%08x IRQ1 st=%08x en=%08x R[0x44]=%08x R[0x64]=%08x\n",m.clio.R[0x40>>2],m.clio.R[0x48>>2],m.clio.R[0x60>>2],m.clio.R[0x68>>2],m.clio.R[0x44>>2],m.clio.R[0x64>>2]);
  {FILE*d=fopen("/tmp/ramdump.bin","wb"); for(int i=0;i<0xC0000;i++){uint32_t v=m.ram[i]; uint8_t b[4]={v>>24,v>>16,v>>8,v}; fwrite(b,1,4,d);} fclose(d);} /*ramdump*/
  for(int i=0;i<0x40000;i++) if(swihist[i]) printf("SWIHIST %05x %u\n",i,swihist[i]);
  return 0;
}
