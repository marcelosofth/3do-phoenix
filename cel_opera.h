/* cel_opera.h - motor de CEL do Opera (opera-libretro) ligado ao Phoenix. Ver cel_opera.c (gerado por gen_cel_opera.py). */
#ifndef PH_CEL_OPERA_H
#define PH_CEL_OPERA_H
#include <stdint.h>

#define CEL_OPERA_RAM_BYTES 0x300000u
void opera_cel_set_mem(uint32_t dram_bytes, uint32_t vram_bytes);   /* tamanhos da DRAM/VRAM (padrao 2 MB + 1 MB) */   /* DRAM 2 MB + VRAM 1 MB, mesmo layout do Phoenix */

/* Estado que o Opera guarda em variaveis estaticas e que vale entre CCBs/cadeias; fica dentro do celexec_t (entra no savestate). */
typedef struct {
    int32_t  hdx, hdy, vdx, vdy, hddx, hddy;    /* 16.16 (hdx/hdy/hddx/hddy ja com o >>4 do CCB) */
    int32_t  xpos, ypos;                        /* 16.16 */
    uint32_t pixc;                              /* PIXC/PPMP lido do CCB (LDPPMP) */
    uint32_t pre1;                              /* PRE1 nao e relido nos CCBs empacotados */
    uint32_t shadow[9];                         /* ultimo valor escrito nos cel_w[8,16..23]: se a CPU mudar, o registrador vence */
    uint32_t shadow_ok, runs;
    /* estatisticas (so contadores) */
    unsigned n_drawn, n_packed, n_unpacked, n_lrform, n_hdd, n_scaled, bpp_n[8], ppmp_nonid, pixc_nonzero;
    uint32_t flags_seen, pre0_seen, pre1_seen;
} opera_cel_t;

/* Percorre a cadeia de CCBs que comeca em dma_arr[1] e desenha na RAM. dma_arr = madam.dma_arr, cel_w = madam.cel_w, ram = RAM do Phoenix (3 MB). Retorna 0 (stall). */
uint32_t opera_cel_run(opera_cel_t *o, uint32_t *dma_arr, uint32_t *cel_w, uint32_t *ram);
/* HiRes CEL Rendering: planos extras (copias de 1 MB da VRAM); NULL desliga. Ver cel_opera.c. */
void opera_cel_set_hires(uint32_t *p1, uint32_t *p2, uint32_t *p3);
/* Diagnostico HiRes: CEL 3D desenhado na VRAM cuja altura em 2x e <= 1 linha (preenche so a linha 2x PAR/IMPAR e deixa a outra sem a faixa). */
typedef struct { int32_t x[4], y[4]; uint32_t ccb, flags, regctl3; int32_t w, h; int kind; } cel_thin_t;   /* kind 0 = quad fino, 1 = quad que cruza a linha-sonda */
unsigned opera_cel_thin_pop(cel_thin_t *out, unsigned max);   /* esvazia o anel; retorna quantos copiou (mais antigos perdidos se encheu) */
void opera_cel_probe_row(int y2);   /* linha 2x a vigiar (-1 desliga): registra todo quad 3D largo (>=48 px) da VRAM que cruza y2-2..y2+2 */
/* Diagnostico v6: cobertura por linha 2x. Conta escritas 2x na VRAM por (CCB, linha 2x, faixa de 32 px 2x) e, no relatorio, acha
 * "buracos": faixa com 0 escritas entre duas linhas vizinhas com >= 8. slot 0 = todos os CCBs juntos; ccb != 0 = so aquele CCB. */
typedef struct { uint32_t ccb, dst; int row, nbk, bk0, nq; int32_t q[6][8]; } cel_hole_t;
int opera_cel_topfix(void);   /* 1 se o conserto do topo do telhado (v7) foi compilado */
void opera_cel_cov_enable(int on);
unsigned opera_cel_cov_report(cel_hole_t *out, unsigned max);   /* calcula, devolve ate max buracos e zera a contagem */
#endif
