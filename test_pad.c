#include <stdio.h>
#include <string.h>
#include "joybus.h"
#include "opera_pbus.h"
static unsigned g_mask[2];
static int16_t st(unsigned port, unsigned dev, unsigned idx, unsigned id){ (void)dev;(void)idx; return (g_mask[port]>>id)&1; }
static void pl(void){}
int main(void){
  joybus_t j; joybus_init(&j); joybus_set_callbacks(&j,pl,st); joybus_set_connected(&j,1,1);
  unsigned bad=0, n=0;
  for(unsigned m0=0;m0<4096;m0+=7) for(unsigned m1=0;m1<4096;m1+=97){
    g_mask[0]=m0; g_mask[1]=m1;
    joybus_poll(&j); uint32_t w[4]; joybus_get_pbus_buffer(&j,0,w);
    /* referencia: Opera */
    opera_pbus_reset();
    for(int p=0;p<2;p++){ unsigned m=g_mask[p]; opera_pbus_joypad_t jp;
      #define B(x) ((m>>RETRO_DEVICE_ID_JOYPAD_##x)&1)
      jp.u=B(UP); jp.d=B(DOWN); jp.l=B(LEFT); jp.r=B(RIGHT); jp.lt=B(L); jp.rt=B(R); jp.x=B(SELECT);
      jp.p=B(START)|B(X); jp.a=B(Y); jp.b=B(B); jp.c=B(A);
      /* Phoenix anula direcoes opostas simultaneas */
      if(jp.u&&jp.d) jp.u=jp.d=0; if(jp.l&&jp.r) jp.l=jp.r=0;
      opera_pbus_add_joypad(&jp);} 
    opera_pbus_pad();
    uint8_t *ref=opera_pbus_buf(); uint32_t rw[4]; 
    for(int i=0;i<4;i++){ rw[i]=0xFFFFFFFFu; if(i*4<(int)opera_pbus_size()) rw[i]=(ref[i*4]<<24)|(ref[i*4+1]<<16)|(ref[i*4+2]<<8)|ref[i*4+3]; }
    n++; if(memcmp(w,rw,16)){ if(bad<5) printf("DIFF m0=%03x m1=%03x ph=%08x opera=%08x\n",m0,m1,w[0],rw[0]); bad++; }
  }
  printf("%u casos, %u diferencas\n",n,bad);
  /* exemplos */
  g_mask[0]=(1<<RETRO_DEVICE_ID_JOYPAD_UP)|(1<<RETRO_DEVICE_ID_JOYPAD_Y); g_mask[1]=0; joybus_poll(&j); uint32_t w[4]; joybus_get_pbus_buffer(&j,0,w);
  printf("P1 Cima+Y(A3DO): %08x %08x\n",w[0],w[1]);
  return bad!=0;
}
