/* celexec.h - laco da cadeia de CCBs (FUN_00488700, async == 0) ligado ao MADAM (Parte 19).
 * Registradores MADAM <-> campos do CEL (CEL = madam+0x38; cel_w[i] = CEL+0x10000f0+4*i = registrador 0x100+4*i):
 *   dma_arr[0..3] = 0x5a0..0x5ac = CEL+0x10001f0/f4/f8/fc = pc, NEXTCCB, PLUTDATA, PDATA (o inicio da cadeia e dma_arr[1])
 *   cel_w[4]=pixc  [8]=ppmp  [13]=clip (reg 0x134)  [14]=base de leitura  [15]=base de escrita  [16..23]=xpos,ypos,vdx,vdy,hdx,hdy,hddx,hddy
 *   cel_w[0x20..0x3f] = PLUT (32 palavras). PRE0/PRE1 (CEL+0x1000214/218) ficam so aqui, em ccb.
 * Dois modos: async==1 (disparo, 0x03300100) so DEVERIA medir o custo (FUN_00482950, nao portado): devolve 0 =
 * "sem espera", e o laco da CPU chama o desenho (async==0) em seguida. async==0 percorre a cadeia e desenha. */
#ifndef PH_CELEXEC_H
#define PH_CELEXEC_H
#include "madam.h"
#include "cel.h"
#include "cel_opera.h"

typedef struct {
    uint32_t clip, wrb, rdb, pixc, path, hdd; unsigned n, px, tri, wind, area0, first, last, ntr;
    struct { int32_t x, y, vdx, vdy, hdx, hdy, hddx, hddy; uint32_t w, h, flags, pre0, pre1, px0, dpx; } tr[3];
} cel_grp_t;

typedef struct {
    cel_ccb_t ccb;                    /* estado persistente entre CCBs e entre execucoes */
    int       last_h;                 /* CEL+0x1000274: altura do ultimo CEL (avanco de xpos/ypos) */
    unsigned  drawn, unsupported, poly;     /* CCBs desenhados pelo caminho 1 / que precisam dos caminhos 2/3 (ainda nao portados) */
    struct { uint32_t flags, pre0, pre1, ppmp, pixc, path, clip, wrb, rdb, wrs, rds, src, hdd, w, h; unsigned n; } hist[160]; unsigned hist_other;   /* diagnostico: formatos de CEL desenhados (path 1=blit11, 2=poly, 0=nao suportado) */
    unsigned bpp_n[8], n_packed, n_unpacked, n_lrform, n_scaled, n_hdd, n_culled_skip;  /* AUDITORIA */
    uint32_t flags_seen, pre0_seen, pre1_seen, ppmp_nonid, pixc_nonzero, chains;
    /* DIAGNOSTICO: grupos por contexto de destino (recorte, bases de escrita/leitura, PIXC, caminho, hdd): contagem, pixels gravados e 3 CELs de amostra */
    cel_grp_t grp[48]; unsigned grp_other;
    uint32_t *dec, *tex;              /* buffers do decoder empacotado (alocados na 1a vez) */
    /* DIAGNOSTICO (libretro.c, linha CELINV): CELs sem nenhum pixel gravado e por que. Com o motor do Opera so cels_total e preenchido
     * (o Opera nao classifica); inv_* e inv[] seguem existindo para o motor legado/diagnostico. */
    unsigned cels_total, inv_total, inv_culled, inv_offscreen, inv_transp, inv_degen, inv_other, ninv;
    struct { uint32_t path, flags, pre0, pre1, xpos, ypos, hdx, hdy, vdx, vdy, hddx, hddy, clip, tri, tri_culled; int w, h; } inv[8];
    /* MOTOR DO OPERA (cel_opera.c): padrao. legacy = 1 volta para o motor reimplementado a partir do .exe (cel.c) - so para comparar A/B. */
    opera_cel_t op;
    int         legacy;
} celexec_t;

void     celexec_free(celexec_t *s);
void     celexec_reset_window(celexec_t *s);   /* zera histograma, grupos e contadores do rasterizador (diagnostico) */
/* ram = RAM da maquina (3 MB, palavras big-endian). Retorna o stall em ciclos (hoje sempre 0).
 * Por padrao desenha com o motor de CEL do Opera (cel_opera.c); s->legacy = 1 usa o motor antigo (cel.c). */
uint32_t celexec_run(celexec_t *s, madam_t *m, uint32_t *ram, uint8_t async);
#endif
