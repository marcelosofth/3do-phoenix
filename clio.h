/* phoenix_core/clio.h — CLIO do Phoenix 3DO, reimplementado a partir do PhoenixEmuProject.exe.
 * Fonte dos comportamentos: PARTE2..PARTE8.md (FUN_0048f9f0 Poke, FUN_0048f170 Peek, FUN_00490890 tick,
 * FUN_0048f540 timers, FUN_0048f690 DMA XBUS, FUN_0048ef80 IRQ). "TODO(RE)" = ainda nao decompilado. */
#ifndef PHOENIX_CLIO_H
#define PHOENIX_CLIO_H
#include <stdint.h>
#include <stddef.h>

#define PH_RAM_BYTES 0x300000u          /* DRAM+VRAM: o DMA XBUS ignora escrita >= isto (FUN_0048f690) */

/* dispositivo no XBUS: slots de vtable +0x88..+0xb8 (FUN_00498500..00498690) */
typedef struct xdev {
    void    *ctx;
    uint32_t (*status_read)(void *);          /* +0x88 */
    void     (*cmd_write)(void *, uint8_t);   /* +0x90 */
    uint32_t (*poll_read)(void *);            /* +0x98 */
    void     (*poll_write)(void *, uint8_t);  /* +0xa0 */
    uint32_t (*data_read)(void *);            /* +0xa8 */
    void     (*data_write)(void *, uint8_t);  /* +0xb0 */
    uint32_t (*res_read)(void *);             /* +0xb8 */
    uint32_t (*media_access)(void *);         /* != 0: o dispositivo tem acesso a midia pendente (POLRE) */
} xdev_t;

typedef struct clio clio_t;
struct clio {
    uint32_t R[0x4000];                 /* 0x10000 bytes: clio.bin do savestate (R[off>>2]) */
    int      pal;                       /* clio+0x86678 */
    uint32_t audio_acc;                 /* clio+0x96690 */
    uint32_t timer_acc;                 /* clio+0x9668c */
    uint32_t line_acc;                  /* clio+0x96694 (tambem fase do HCNT) */
    uint8_t  xbus_sel;                  /* clio+0x488 (devXBUS+0x460) */
    xdev_t  *dev[16];                   /* clio+0x408 (devXBUS+0x3e0); so o ID 0 (CD) existe no Phoenix */
    uint16_t sem_data, sem_status;      /* clio+0x12216 / +0x12214 (semaforo DSP/ARM, 0x17d0) */
    uint32_t dsp_rand;                  /* clio+0x12244 (LCG do 0x17f0) */
    uint8_t  dsp_run, dsp_halt;         /* clio+0x12264 / +0x12260 */
    uint16_t dsp_in_a, dsp_in_b;        /* clio+0x1223c / +0x1226c (Peek 0x17f4 / 0x17f8) */
    uint32_t ram_bytes;                 /* tamanho de ram8 (DRAM+VRAM); 0 = PH_RAM_BYTES (stock) */
    uint8_t *ram8;                      /* RAM: PH_RAM_BYTES, big-endian por palavra (indice ^3) */
    /* ganchos para os blocos ainda nao reimplementados */
    void    *ctx;
    uint32_t (*madam_peek)(void *, uint32_t off);
    void     (*madam_poke)(void *, uint32_t off, uint32_t val);
    void     (*ram_written)(void *, uint32_t addr, uint32_t nbytes);
    uint32_t (*madam_raw_peek)(void *, uint32_t off);          /* registrador de canal DMA cru (FIFOs do DSP: sem mascara nem "zera o proximo") */
    void     (*madam_raw_poke)(void *, uint32_t off, uint32_t val);
    void     (*dsp_nwrite16)(void *, uint32_t idx, uint16_t v);        /* N-mem (instrucoes) escrita pelo ARM: 0x1800.. e 0x2000.. */
    void     (*frame_cb)(void *);                      /* linha 0x18 */
    int      (*vdlp_line_pending)(void *);             /* FUN_00474410 */
    void     (*vdlp_line)(void *);                     /* FUN_004788b0 */
    uint32_t (*dsp_step)(void *);                      /* FUN_0046d570: 1 amostra */
    void     (*audio_out)(void *, uint32_t sample);
    void     (*dsp_reset)(void *, int flag);           /* FUN_00427d90 (0x17e8: 0, 0x17e4: 1) */
    void     (*dsp_write16)(void *, uint32_t idx, uint16_t v);   /* FUN_0045f8a0 */
    uint16_t (*dsp_read16)(void *, uint32_t idx);                /* FUN_0045f8c0 */
    void     (*cd_tick)(void *, uint32_t cyc);         /* bloco de setor no inicio de FUN_00490890 */
    uint32_t stat_sem_ack, stat_irq11, stat_xbus_nodev, xbus_seen;   /* AUDITORIA: ack forcado do semaforo DSP, IRQ DSP->ARM (bit 11), acessos XBUS sem dispositivo, mascara de IDs XBUS selecionados */
    uint32_t rtouch_r[0x4000 / 32], rtouch_w[0x4000 / 32];            /* AUDITORIA: mapa de registradores lidos/escritos (1 bit por palavra de 0x10000 bytes) */
    uint32_t reset_req;                        /* pedido de soft reset vindo do poke de CSTAT (0x28): 0x10 = reset, 0x20 = limpa DIPIR; a maquina consome e zera */
};

/* depuracao: se nao for NULL recebe uma linha por evento de DMA/registrador do CLIO
 * (limitado a ~300 linhas por execucao, mesmo esquema do cd_trace_hook em cd.h) */
extern void (*clio_trace_hook)(const char *msg);

void     clio_reset(clio_t *c);
void     clio_xbus_media_access(clio_t *c, uint32_t unit);   /* latch DIPIR (0x410/0x414) + IRQ1 bit 1 */
uint32_t clio_peek(clio_t *c, uint32_t addr);
void     clio_poke(clio_t *c, uint32_t addr, uint32_t val);
void     clio_tick(clio_t *c, uint32_t cyc);               /* FUN_00490890 */
void     clio_raise_irq(clio_t *c, uint32_t irq0, uint32_t irq1);   /* FUN_0048ef80 */
#endif
