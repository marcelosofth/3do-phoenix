#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <dlfcn.h>
#include "libretro.h"
static void lg(enum retro_log_level l,const char*f,...){ if(l<RETRO_LOG_WARN) return; va_list a; va_start(a,f); fprintf(stderr,"[core] "); vfprintf(stderr,f,a); va_end(a);}
static bool env(unsigned c,void*d){
  switch(c){
  case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**)d="/home/claude/work/fe/sys"; return true;
  case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
  case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback*)d)->log=lg; return true;
  case RETRO_ENVIRONMENT_GET_VARIABLE: { struct retro_variable*v=d; if(!strcmp(v->key,"opera_bios")){v->value="panafz1.bin";return true;} v->value=NULL; return false; }
  case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool*)d=false; return true;
  case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned*)d=0; return true;
  default: return false; }
}
static void vid(const void*a,unsigned w,unsigned h,size_t p){}
static size_t aud(const int16_t*d,size_t n){return n;}
static void aud1(int16_t l,int16_t r){}
static void inp(void){}
static int16_t ist(unsigned p,unsigned d,unsigned i,unsigned id){return 0;}
int main(int argc,char**argv){
  void*h=dlopen(argv[1],RTLD_NOW); if(!h){puts(dlerror());return 1;}
#define S(n) void(*n)()=dlsym(h,#n)
  void(*set_env)(void*)=dlsym(h,"retro_set_environment");
  void(*set_vid)(void*)=dlsym(h,"retro_set_video_refresh");
  void(*set_a)(void*)=dlsym(h,"retro_set_audio_sample");
  void(*set_ab)(void*)=dlsym(h,"retro_set_audio_sample_batch");
  void(*set_ip)(void*)=dlsym(h,"retro_set_input_poll");
  void(*set_is)(void*)=dlsym(h,"retro_set_input_state");
  void(*init)(void)=dlsym(h,"retro_init");
  bool(*load)(const struct retro_game_info*)=dlsym(h,"retro_load_game");
  void(*run)(void)=dlsym(h,"retro_run");
  set_env(env); set_vid(vid); set_a(aud1); set_ab(aud); set_ip(inp); set_is(ist); init();
  struct retro_game_info gi={argv[2],NULL,0,NULL}; if(!load(&gi)){puts("load falhou");return 1;}
  int frames=atoi(argv[3]); for(int i=0;i<frames;i++) run();
  return 0; }
