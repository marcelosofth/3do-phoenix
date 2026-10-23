#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dlfcn.h>
#include "libretro.h"
static unsigned held;
static int16_t st(unsigned p,unsigned d,unsigned i,unsigned id){ return (d==RETRO_DEVICE_JOYPAD && p==0 && ((held>>id)&1)); }
static void pl(void){}
static void vr(const void*d,unsigned w,unsigned h,size_t p){}
static void as(int16_t l,int16_t r){}
static size_t asb(const int16_t*d,size_t n){return n;}
static void lg(enum retro_log_level lv,const char*fmt,...){ va_list a; va_start(a,fmt); char b[512]; vsnprintf(b,sizeof b,fmt,a); va_end(a); if(strstr(b,"[pad]")) fputs(b,stdout); }
static bool env(unsigned cmd,void*data){
  switch(cmd){
  case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback*)data)->log=lg; return true;
  case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char**)data="/home/claude/phoenix_opencode"; return true;
  case RETRO_ENVIRONMENT_GET_VARIABLE: ((struct retro_variable*)data)->value=NULL; return false;
  case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
  default: return false; } }
int main(int argc,char**argv){
  void*h=dlopen("./phoenix_libretro.so",RTLD_NOW); if(!h){puts(dlerror());return 1;}
  #define F(n) void(*n)() = dlsym(h,#n)
  void(*set_env)(retro_environment_t)=dlsym(h,"retro_set_environment"); void(*init)(void)=dlsym(h,"retro_init");
  bool(*load)(const struct retro_game_info*)=dlsym(h,"retro_load_game"); void(*run)(void)=dlsym(h,"retro_run");
  void(*s_in)(retro_input_state_t)=dlsym(h,"retro_set_input_state"); void(*s_po)(retro_input_poll_t)=dlsym(h,"retro_set_input_poll");
  void(*s_v)(retro_video_refresh_t)=dlsym(h,"retro_set_video_refresh"); void(*s_a)(retro_audio_sample_t)=dlsym(h,"retro_set_audio_sample"); void(*s_ab)(retro_audio_sample_batch_t)=dlsym(h,"retro_set_audio_sample_batch");
  void(*s_dev)(unsigned,unsigned)=dlsym(h,"retro_set_controller_port_device");
  set_env(env); init(); s_v(vr); s_a(as); s_ab(asb);
  int late=atoi(argv[2]);
  if(!late){ s_in(st); s_po(pl); }
  struct retro_game_info gi={argv[1],0,0,0}; if(!load(&gi)){puts("load falhou");return 1;}
  if(late){ s_in(st); s_po(pl); }   /* como o RetroArch pode fazer: callbacks DEPOIS do load_game */
  s_dev(0,RETRO_DEVICE_JOYPAD); s_dev(1,RETRO_DEVICE_JOYPAD);
  for(int f=0;f<40;f++){ held = (f>=10&&f<20)?(1u<<RETRO_DEVICE_ID_JOYPAD_START)|(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN):0; run(); }
  return 0; }
