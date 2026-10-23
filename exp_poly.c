/* experimento: cobertura do caminho poligonal contra um teste analitico de ponto-no-quadrilatero */
#include "machine.h"
#include <stdio.h>
#include <math.h>
static ph_machine_t M;
#define WR(a, v) ph_write32(&M, (a), (v))
#define FB 0x100000u
#define STEP 0x500u
static uint16_t dst(int x, int y) { return cel_fb_read16(M.ram, FB + STEP * (y >> 1) + (y & 1) * 2 + x * 4); }
static void clearfb(void){ for(int y=0;y<240;y++)for(int x=0;x<320;x++) cel_fb_write16(M.ram, FB + STEP*(y>>1)+(y&1)*2+x*4, 0); }
static void regs(void){ WR(0x03300130,0x14|0x1400); WR(0x03300134,(239u<<16)|319u); WR(0x03300138,FB); WR(0x0330013c,FB); WR(0x03300110,0); }
/* 3DO MapCel: quad P0..P3 (P0 topo-esq, P1 topo-dir, P2 base-dir, P3 base-esq), textura WxH */
static int cross3(double ax,double ay,double bx,double by,double px,double py){ return ((bx-ax)*(py-ay)-(by-ay)*(px-ax))>0; }
static int inquad(double *X,double *Y,double px,double py){
  int s[4]; for(int i=0;i<4;i++){int j=(i+1)&3; double c=(X[j]-X[i])*(py-Y[i])-(Y[j]-Y[i])*(px-X[i]); s[i]=c>0?1:(c<0?-1:0);}
  return (s[0]>=0&&s[1]>=0&&s[2]>=0&&s[3]>=0)||(s[0]<=0&&s[1]<=0&&s[2]<=0&&s[3]<=0);
}
static void run(const char *name,double *X,double *Y,int W,int H,uint32_t winding_flags){
  clearfb(); regs();
  const uint32_t A=0x1000,SRC=0x2000;
  for(int y=0;y<H;y++)for(int x=0;x<W;x++) cel_fb_write16(M.ram,SRC+y*(W*2)+x*2,0x7fff & ~1);  /* branco */
  /* MapCel: hdx=(x1-x0)/W (12.20) ; vdx=(x3-x0)/H (16.16) ; hddx=((x2-x3)-(x1-x0))/(W*H) (12.20) */
  double hdx=(X[1]-X[0])/W, hdy=(Y[1]-Y[0])/W, vdx=(X[3]-X[0])/H, vdy=(Y[3]-Y[0])/H;
  double hddx=((X[2]-X[3])-(X[1]-X[0]))/(W*H), hddy=((Y[2]-Y[3])-(Y[1]-Y[0]))/(W*H);
  uint32_t fl = winding_flags | CCB_NPABS|CCB_SPABS|CCB_LDSIZE|CCB_LDPRS|CCB_LDPPMP|CCB_CCBPRE|CCB_YOXY|CCB_BGND|CCB_LAST;
  uint32_t w[]={fl,0,SRC,0,(uint32_t)(int32_t)llround(X[0]*65536),(uint32_t)(int32_t)llround(Y[0]*65536),
    (uint32_t)(int32_t)llround(hdx*1048576),(uint32_t)(int32_t)llround(hdy*1048576),(uint32_t)(int32_t)llround(vdx*65536),(uint32_t)(int32_t)llround(vdy*65536),
    (uint32_t)(int32_t)llround(hddx*1048576),(uint32_t)(int32_t)llround(hddy*1048576),0x1F001F00u, 6|0x10|((H-1)<<6), (uint32_t)((W*2+3)/4-2)<<16|(W-1)};
  /* PRE1: WOFFSET(16bpp: bits 16..25) = palavras-2 ; largura-1 em 0..10 */
  for(unsigned i=0;i<sizeof w/4;i++) WR(A+i*4,w[i]);
  unsigned p0=M.cel.poly,d0=M.cel.drawn,u0=M.cel.unsupported;
  WR(0x033005a4,A); WR(0x03300100,0);
  int drawn=0,exp=0,miss=0,extra=0;
  for(int y=0;y<240;y++)for(int x=0;x<320;x++){
    int got=dst(x,y)!=0; int e=inquad(X,Y,x+0.5,y+0.5);
    drawn+=got; exp+=e; if(e&&!got)miss++; if(!e&&got)extra++; }
  printf("%-28s flags=%05x poly+%u draw+%u unsup+%u  desenhado=%5d esperado=%5d faltando=%4d sobrando=%4d\n",name,winding_flags,M.cel.poly-p0,M.cel.drawn-d0,M.cel.unsupported-u0,drawn,exp,miss,extra);
}
int main(void){
  ph_init(&M); M.madam.cel_enabled=1;
  double X1[4]={100,200,200,100},Y1[4]={50,50,150,150};
  double X2[4]={100,200,180,120},Y2[4]={50,50,150,150};     /* trapezio (perspectiva) */
  double X3[4]={100,150,200,120},Y3[4]={60,40,120,160};     /* generico */
  double X4[4]={200,100,100,200},Y4[4]={50,50,150,150};     /* espelhado */
  double X5[4]={100,104,104,100},Y5[4]={100,100,104,104};   /* minusculo */
  uint32_t both=CCB_ACW|CCB_ACCW;
  const int Ws[]={1,4,16}; 
  for(int k=0;k<3;k++){ int W=Ws[k]; printf("--- textura %dx%d ---\n",W,W);
    run("retangulo",X1,Y1,W,W,both); run("trapezio",X2,Y2,W,W,both); run("generico",X3,Y3,W,W,both);
    run("espelhado both",X4,Y4,W,W,both); run("espelhado ACW",X4,Y4,W,W,CCB_ACW); run("espelhado ACCW",X4,Y4,W,W,CCB_ACCW);
    run("retangulo ACW",X1,Y1,W,W,CCB_ACW); run("retangulo ACCW",X1,Y1,W,W,CCB_ACCW);
    run("generico ACW",X3,Y3,W,W,CCB_ACW); run("generico ACCW",X3,Y3,W,W,CCB_ACCW);
    run("minusculo",X5,Y5,W,W,both);}
  return 0;}
