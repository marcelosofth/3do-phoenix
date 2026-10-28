/* vdlp.h - VDLP do Phoenix 3DO (lista de video + captura de linha + conversao RGB24).
 * Reimplementado a partir do PhoenixEmuProject.exe com REA/Ghidra (PARTE18_VDLP_RODADA1.md):
 *   FUN_00474410 = vdlp_line()       (captura de UMA linha, chamada por FUN_00490890 a cada linha)
 *   FUN_004740d0 = vdl_read()        (le uma entrada da VDL na VRAM)
 *   FUN_004788b0 = vdlp_frame_end()  (fim de frame: fundo -> host, converte, apresenta)
 *   FUN_00474e90 = convert (so o caminho simples; o filtro de interpolacao "+0x8c != 0" NAO foi portado)
 * Convencao de memoria: vram = ponteiro para 0x40000 palavras (1 MB, VRAM = RAM + 2 MB); cada palavra tem o valor
 * inteiro big-endian do 3DO (igual a ph_machine_t.ram). */
#ifndef PH_VDLP_H
#define PH_VDLP_H
#include <stdint.h>

#define VDLP_MAX_LINES 288
#define VDLP_MAX_WIDTH 384

typedef struct vdlp_line {            /* estado de uma linha (struct de 0x674 bytes no .exe, +0x94) */
    uint16_t pix[VDLP_MAX_WIDTH];     /* pixels capturados (so as n primeiras posicoes) */
    uint16_t pixh[3][VDLP_MAX_WIDTH]; /* HiRes: mesmos pixels nos planos 1..3 (subamostras x impar / y impar / ambos); so preenchido com vdlp_t.hires */
    uint8_t  clut[3][32];             /* [0]=B (+0x694), [1]=G (+0x6b4), [2]=R (+0x6d4) */
    uint32_t outctl, clutdma, bg;     /* +0x6f4 / +0x6f8 / +0x6fc: estado no momento da linha */
    int      dirty;                   /* a linha mudou desde a ultima conversao (no .exe: faixa +0x700/+0x704) */
} vdlp_line_t;

typedef struct vdlp vdlp_t;
struct vdlp {
    int      pal;                     /* 0 = NTSC 320x240, 1 = PAL (384x288 ou 320x288, ver vdlp_init_mode) */
    int      bypass_clut;             /* opcao "VDLP Bypass CLUT": ignora a CLUT do jogo e usa a rampa linear (cores possivelmente erradas, renderizacao mais rapida) */
    int      lines, width, first_line;/* +0x38, +0x3c, +0x40 : 240/320/0x10  ou  288/384/0x11 */
    const uint32_t *vram;             /* +0x74398 */
    uint32_t vram_base, vram_bytes;   /* "Memory Capacity": endereco 3DO e tamanho da VRAM (0 = stock: 0x200000 e 1 MB) */
    uint32_t clutdma;                 /* +0x74314 */
    uint8_t  clut[3][32];             /* +0x74318 B / +0x74338 G / +0x74358 R */
    uint32_t bg;                      /* +0x74378 : byte0=R byte1=G byte2=B */
    uint32_t headvdl;                 /* +0x7437c */
    uint32_t width_sel;               /* +0x74380 : largura em pixels da linha (tabela 0xb02d00) */
    uint32_t curvdl;                  /* +0x74384 */
    uint32_t curbmp, prevbmp;         /* +0x74388 / +0x7438c */
    uint32_t outctl;                  /* +0x74390 : OUTCONTROLL */
    int32_t  linedelay;               /* +0x74394 : linhas restantes da entrada de VDL atual */
    vdlp_line_t line[VDLP_MAX_LINES]; /* +0x94 */
    uint8_t *rgb;                     /* +0x78 : width*lines*3 bytes (R,G,B) */
    /* HiRes CEL Rendering (Phoenix): com hires=1 cada linha nativa vira 2 linhas de 2*width pixels (planos 0..3 da VRAM; ver cel_opera.c).
     * hr[] = planos extras (copias de 1 MB da VRAM, mantidas por machine.c/sport.c/cel_opera.c); rgb_hr = (2*width)*(2*lines)*3 bytes. */
    int      force_dirty;                     /* 1 = o proximo quadro reconverte todas as linhas (troca de Bypass CLUT / HiRes); zerado em vdlp_frame_end */
    int      hires;
    const uint32_t *hr[3];
    uint8_t *rgb_hr;
    uint32_t frame_id;                /* +0x88 */
    uint32_t stat_ctl_seen, stat_ctl_bits;   /* AUDITORIA: OR dos bits dos controles de VDL (ctl) e das palavras de controle (0xC...) vistos */
    uint32_t stat_hires_lines, stat_width_seen, stat_empty_vdl;   /* linhas com largura >= 640, mascara de WIDTH_TAB usados, VDLs vazias/fora da VRAM */
    void    *ud;
    /* ganchos do host (vtable de _DAT_00cf4050); NULL = ignora */
    void (*set_background)(void *ud, uint32_t id, uint8_t r, uint8_t g, uint8_t b);                 /* +0x18 */
    void (*blit)(void *ud, uint32_t id, int y0, int count, const uint8_t *rgb, int width);          /* +0x28 : faixa de linhas (com hires: y0/count em linhas 2x e width = 2*width nativa) */
    void (*present)(void *ud, uint32_t id);                                                         /* +0x30 : troca de frame */
};

int      vdlp_init(vdlp_t *v, int pal, const uint32_t *vram);   /* 0 = ok; pal: 0 = NTSC 320x240, 1 = PAL2 384x288 */
#define VDLP_MODE_NTSC 0          /* 320x240 @60 */
#define VDLP_MODE_PAL2 1          /* 384x288 @50 (o "PAL" de sempre) */
#define VDLP_MODE_PAL1 2          /* 320x288 @50 */
int      vdlp_init_mode(vdlp_t *v, int mode, const uint32_t *vram);   /* idem, com os tres modos do Opera (Region / Mode) */
void     vdlp_set_bypass_clut(vdlp_t *v, int on);              /* liga/desliga o bypass e marca todas as linhas para reconverter */
void     vdlp_free(vdlp_t *v);
int      vdlp_set_hires(vdlp_t *v, int on, const uint32_t *p1, const uint32_t *p2, const uint32_t *p3);   /* liga/desliga o modo 2x e marca todas as linhas para reconverter; 0 = ok */
void     vdlp_set_head(vdlp_t *v, uint32_t addr);               /* MADAM 0x580 (ja mascarado com 0xfffffc) */
int      vdlp_line(vdlp_t *v, uint32_t line_reg);               /* FUN_00474410: 1 = ultima linha visivel (apresentar) */
void     vdlp_frame_end(vdlp_t *v);                             /* FUN_004788b0 */
#endif
