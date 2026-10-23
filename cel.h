/* cel.h - leitor de CCBs, PLUT, decoder empacotado e conversao indice->cor do CEL do Phoenix 3DO.
 * Reimplementado a partir do PhoenixEmuProject.exe (Parte 14):
 *   FUN_00488700 (laco de CCBs, so a parte de LEITURA), FUN_0047ca50 (indice -> cor via PLUT),
 *   FUN_0059c720 (leitor de bits MSB-first), FUN_004859b0 (decoder de CEL empacotado).
 * Pixel pipeline PPMP/PIXC (FUN_0047d0d0): ver cel_pixel (Parte 16). NAO esta aqui: custo em ciclos (FUN_00482950/004821b0),
 * cache de textura (FUN_00487100 / parte do 004859b0), rasterizador (FUN_00480290 = Parte 15).
 * RAM: uint32_t[] com o inteiro big-endian do 3DO (byte = idx^3, meia-palavra = endereco^2), como machine.h. */
#ifndef PH_CEL_H
#define PH_CEL_H
#include <stdint.h>

#define CEL_RAM_BYTES 0x300000u
/* bits da palavra de flags do CCB (conferidos nos testes de FUN_00488700) */
#define CCB_SKIP   0x80000000u  /* nao desenha (mas o CCB e lido) */
#define CCB_LAST   0x40000000u  /* zera o ponteiro do proximo CCB */
#define CCB_NPABS  0x20000000u  /* ponteiro "next" absoluto (senao relativo a CCB+8) */
#define CCB_SPABS  0x10000000u  /* ponteiro de dados absoluto (senao relativo a CCB+0xc) */
#define CCB_PPABS  0x08000000u  /* ponteiro da PLUT absoluto (senao relativo a CCB+0x10) */
#define CCB_LDSIZE 0x04000000u  /* le HDX,HDY,VDX,VDY */
#define CCB_LDPRS  0x02000000u  /* le HDDX,HDDY */
#define CCB_LDPPMP 0x01000000u  /* le PPMP */
#define CCB_LDPLUT 0x00800000u  /* carrega a PLUT (8/16/32 entradas conforme o bpp) */
#define CCB_CCBPRE 0x00400000u  /* preambulo dentro do CCB (senao no inicio dos dados) */
#define CCB_YOXY   0x00200000u  /* le XPOS,YPOS do CCB */
#define CCB_ACW    0x00040000u
#define CCB_ACCW   0x00020000u  /* ACW|ACCW == 0 => CEL nao e desenhado */
#define CCB_BGND   0x00000020u  /* pixel 0 nao e transparente */
#define CCB_PACKED 0x00000200u

typedef struct {
    /* estado persistente entre CCBs (offsets = base do CEL, ver PARTE14_CEL.md) */
    uint32_t next;      /* +0x10001f4 */
    uint32_t src;       /* +0x10001fc */
    uint32_t pc;        /* +0x10001f0 */
    uint32_t plutp;     /* +0x10001f8 */
    uint32_t flags;     /* +0x1000210 (copiado p/ +0x100027c) */
    int32_t  xpos, ypos;/* +0x1000130, +0x1000134 (ypos e reescrito: ypos += hdy*... ver doc) */
    int32_t  hdx, hdy, vdx, vdy, hddx, hddy; /* +0x1000140/144/138/13c/148/14c */
    uint32_t ppmp;      /* +0x1000110 */
    uint32_t pre0, pre1;/* +0x1000214, +0x1000218 */
    uint32_t plut32[32];/* +0x1000170 (32 palavras) */
    uint16_t plut16[32];/* +0x1000300 (truncado a 16 bits) */
} cel_ccb_t;

typedef struct { int width, height, stride_bytes, bpp; } cel_geom_t;

void cel_ccb_reset(cel_ccb_t *c);
/* Le UM CCB em c->next (FUN_00488700, rotulo LAB_004887da ate a copia dos registradores derivados).
 * Retorna -1 se c->next == 0 ou fora da RAM (fim da cadeia), 0 se le mas nao desenha, 1 se deve desenhar. */
int  cel_ccb_read(cel_ccb_t *c, const uint32_t *ram);
int  cel_bpp(uint32_t pre0);                                   /* tabela 0xb030a0 = {1,1,2,4,6,8,16,16} */
cel_geom_t cel_geometry(const cel_ccb_t *c);                   /* FUN_00487100 / FUN_004859b0 */
uint32_t cel_index_to_color(const cel_ccb_t *c, uint32_t v);   /* FUN_0047ca50; bit31 = transparente */

/* leitor de bits MSB-first (FUN_0059c720): n em 1..32, n==0 vira 1, n>32 vira 32 */
typedef struct { const uint32_t *ram; uint32_t base; uint32_t byte; uint32_t bit; } cel_bits_t;
uint32_t cel_bits_get(cel_bits_t *b, unsigned n);

/* Decodifica CEL empacotado (FUN_004859b0): out = height*0x800 palavras (stride 0x800); retorna a maior largura
 * de linha (vira cel_w). Pixels transparentes = 0x80000000. */
int  cel_decode_packed(const cel_ccb_t *c, const uint32_t *ram, uint32_t *out, int *rows_out);

/* CEL nao empacotado (FUN_0047d760): out = width*height palavras compactas (cel_geometry); suporta 1/2/4/6/8/16 bpp e LRFORM. */
int  cel_decode_unpacked(const cel_ccb_t *c, const uint32_t *ram, uint32_t *out);

/* ---- Parte 15: testes do rasterizador (FUN_00480290 cabeca, FUN_0047ed60, FUN_00482950) ---- */
#define CEL_WIND_CW  0x40000u   /* o .exe chama de 0x40000 quando cross >= 0 */
#define CEL_WIND_CCW 0x20000u
/* cross = vdx*hdy - hdx*vdy (hdx,hdy em 12.20; vdx,vdy em 16.16 -> doubles como os +0x2c8..0x2e0 do .exe) */
double   cel_cross(const cel_ccb_t *c);
uint32_t cel_winding(const cel_ccb_t *c);               /* 0x40000 se cross>=0, senao 0x20000 */
/* 1 = o CEL e descartado pelo teste de orientacao: flags&0x60000 != 0x60000 e igual ao enrolamento */
int      cel_culled(const cel_ccb_t *c);
/* caminho rapido de FUN_00480290: |hdx|==1.0 && hdy==0 (ou trocado) e |vdx|==1.0 && vdy==0 (ou trocado) */
int      cel_is_unit_blit(const cel_ccb_t *c);
/* endereco (em bytes) do pixel (x,y) num framebuffer 3DO: stride*(y>>1) + (y&1)*2 + x*4 + base; valor 16 bits em (addr^2) */
uint32_t cel_fb_addr(uint32_t base, uint32_t stride, int x, int y);
uint16_t cel_fb_read16(const uint32_t *ram, uint32_t addr);          /* 0 fora de 3 MB */
void     cel_fb_write16(uint32_t *ram, uint32_t addr, uint16_t v);   /* ignora fora de 3 MB */

/* ---- Parte 16: pipeline de pixel PPMP/PIXC (FUN_0047d0d0) ---- */
/* Campos lidos por FUN_0047d0d0 (offsets do CEL no .exe). Montados por cel_pix_setup a partir de FUN_00488700. */
typedef struct {
    uint32_t flags;         /* +0x100027c  (flags do CCB; bits usados: 0x10 NOBLK, 0x80/0x100 POVER, 0x400 USEAV, 0x800 PXOR) */
    uint16_t ppmp0, ppmp1;  /* +0x1000340 / +0x1000342  (metade baixa / alta do PPMP) */
    uint32_t pixc;          /* +0x1000344  (copia do registrador +0x1000100) */
    int32_t  mode_f8;       /* +0x10002f8  (de onde vem o bit 0 do pixel de origem) */
    int32_t  mode_fc;       /* +0x10002fc  (de onde vem o bit 15/bit 0 do pixel de saida; bit0 = flags&0x40, bit1 = pixc&0x8000000 condicionado) */
    int32_t  xpos, ypos;    /* +0x10002b0 / +0x10002b4  (XPOS/YPOS 16.16 do CCB) */
} cel_pix_t;
/* FUN_00488700: copia flags, PPMP, pixc e deriva mode_f8/mode_fc. pixc = registrador +0x1000100 do CEL. */
void     cel_pix_setup(cel_pix_t *p, const cel_ccb_t *c, uint32_t pixc);
/* FUN_0047d0d0(CEL, src, dst): src = cor do texel (saida de cel_index_to_color ou textura), dst = pixel atual do framebuffer.
 * Devolve o pixel de 16 bits (bits 31..16 ficam 0). */
uint32_t cel_pixel(const cel_pix_t *p, uint32_t src, uint32_t dst);

/* ---- Parte 17: caminho 1 de FUN_00480290 (blit 1:1 alinhado), de ponta a ponta ---- */
/* entrada de textura decodificada (ponteiro +0x268; +0x5c largura, +0x60 altura, +0x64 "ja convertida pelo PPMP", +0x78 dados).
 * data = largura*altura palavras COMPACTAS (passo = width); cel_decode_packed devolve passo 0x800, entao compacte antes. */
typedef struct { int width, height, converted; const uint32_t *data; } cel_tex_t;
typedef struct {
    int width, height;           /* +0x278 / +0x274 */
    int clip_x, clip_y;          /* +0x350 / +0x354 (limites inclusivos) */
    uint32_t wr_base, wr_step;   /* +0x348 / +0x35c */
    uint32_t rd_base, rd_step;   /* +0x34c / +0x358 */
    uint32_t src;                /* +0x270 (ponteiro-fonte na RAM, = c->src depois do preambulo) */
    int tex_x, tex_y;            /* +0x260 / +0x264 (origem dentro da textura decodificada) */
    const cel_tex_t *tex;        /* +0x268 (NULL = le direto da RAM) */
} cel_raster_t;
/* FUN_00488700 (linhas ~76-81): clip de reg 0x124, base de escrita reg 0x12c, de leitura reg 0x128, passos reg 0x220 / 0x21c.
 * width/height vem de cel_geometry (CEL nao empacotado); ajuste r->width/height/tex se o CEL for empacotado. */
void cel_raster_setup(cel_raster_t *r, const cel_ccb_t *c, uint32_t reg124, uint32_t reg128, uint32_t reg12c, uint32_t reg21c, uint32_t reg220);
/* Devolve 0 se o CEL NAO e do caminho 1 (o chamador segue para os caminhos 2/3); 1 se o caminho 1 tratou (desenhou, descartou ou nao havia nada). */
int cel_blit11(const cel_ccb_t *c, const cel_pix_t *px, const cel_raster_t *r, uint32_t *ram);

/* Caminho 3 de FUN_00480290 (PARTE21): CEL com escala, rotacao ou perspectiva (hddx/hddy) = malha de triangulos como o .exe.
 * Substitui cel_blit22/cel_blit33 (aproximacoes antigas que liam lixo). Chamar depois de cel_blit11 devolver 0. Devolve sempre 1. */
int cel_blit_poly(const cel_ccb_t *c, const cel_pix_t *px, const cel_raster_t *r, uint32_t *ram);

/* DIAGNOSTICO (so contadores, nao muda o desenho): o que o rasterizador de poligonos fez. Indice 0 = CEL sem hddx/hddy, 1 = com. */
typedef struct { unsigned tri, tri_wind, tri_area0, tri_far, early, px, px_transp, px_oob; } cel_dbg_t;
extern cel_dbg_t g_cel_dbg[2];
extern int g_cel_dbg_cls;
#endif
