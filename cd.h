#ifndef PHOENIX_CD_H
#define PHOENIX_CD_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* Dispositivo de CD no XBUS (ID 0) — reproduz xbusMeiCdDrive do Phoenix.
 * RE (Ghidra 12.1.4, PhoenixEmuProject.exe):
 *   vtable xbusMeiCdDrive .rdata:0x00c217e0 (24 slots)
 *   devFIFO (ring)        .rdata:0x00c24180, push = FUN_00951b40
 *   CommandFIFO push      FUN_004ac8a0   -> dispatcher FUN_004aa440
 *   StatusFIFO pop        FUN_004a9f10   (slot +0x98)
 *   DataFIFO pop/prefetch FUN_004aa1b0   (slot +0xb8)
 *   tick/prefetch         FUN_008fb060   (slot +0x90)
 *   status read/write     FUN_004a9f00 / FUN_004aa190 (slots +0xa8 / +0xb0)
 *   init/reset            FUN_004aa020
 *   IRQ                   FUN_004aa150
 */

#define CD_XBUS_ID        0
#define CD_SECTOR_SIZE    2048
/* capacidades dos aneis dadas pelo construtor do drive (FUN_004aca20):
 * StatusFIFO 0x3f, CommandFIFO 0xf, DataFIFO 0xfff */
#define CD_FIFO_STATUS_SZ 0x3f
#define CD_FIFO_CMD_SZ    0xf
#define CD_FIFO_DATA_SZ   0x1000

/* +0x227 StatusReg */
#define CD_S_CMD_RDY   0x10   /* StatusFIFO com byte pendente */
#define CD_S_DATA_RDY  0x20   /* DataFIFO com byte pendente */
#define CD_S_EN_CMD    0x01   /* enable de IRQ escrita pelo host (nibble baixo) */
#define CD_S_EN_DATA   0x02
#define CD_S_MEDIA_ACC 0x80   /* POLRE: latch de acesso a midia (disco novo); escrever 1 limpa */

/* +0x228 DriveStatus/LastError (código MEI 0..23 nos bits baixos) */
#define CD_R_BUSY      0x10
#define CD_R_OK        0x01

/* o dispatcher so aceita indice de faixa < 0x65 (FUN_004aa440 case 0x8c).
 * No exe o array do TOC sao 500 bytes registrados em +0x2c pelo construtor
 * (painel "C T 8@h;M S F 8@u"), entao t = 100/101 leria +0x228/+0x22c, isto e,
 * os proprios registradores da unidade. Aqui a entrada 100 existe de verdade e
 * guarda o lead-out -- divergencia documentada, nao alcancada pelo exe. */
#define CD_TOC_LEADOUT 100
/* indices aceitos pelo painel de velocidade (FUN_004a1390 case 6) */
#define CD_SPEED_PANEL_N 9

/* anel do devFIFO (offsets dentro do objeto Phoenix: +0x28/+0x30/+0x34/+0x38) */
typedef struct {
    uint8_t  *buf;
    uint32_t  cap;
    uint32_t  wr, rd;
} cdfifo_t;

typedef struct {
    /* Fonte dos setores (o mesmo desenho do retro_cdimage do opera-libretro): a imagem NAO e carregada
     * inteira na RAM. Ordem de uso em cd_read_sector(): image (memoria, so testes) -> iso_fp -> chd. */
    uint8_t  *image;
    size_t    image_size;
    uint32_t  num_sectors;
    FILE     *iso_fp;         /* .iso aberta, lida sob demanda */
    uint32_t  iso_ssize;      /* bytes por setor no arquivo: 2048 (cooked) ou 2352 (bruto) */
    uint32_t  iso_soff;       /* inicio dos 2048 bytes de dados no setor: 0 ou 16 */
    struct cd_chd *chd;       /* .chd aberto (cd_chd.c) */

    /* registradores do drive (offset Phoenix entre parênteses) */
    uint8_t   S;              /* +0x227 StatusReg */
    uint8_t   R;              /* +0x228 DriveStatus/LastError */
    uint32_t  state;          /* +0x22c 0=idle 2=pendente 4=? */
    int32_t   lba;            /* +0x230 setor atual */
    int32_t   remain;         /* +0x234 setores restantes (sinal = direção) */
    uint8_t   track, index;   /* +0x224 / +0x225 */
    uint8_t   f0x226;         /* +0x226 */
    uint8_t   msf[3];         /* +0x300/+0x301/+0x302 min, seg, frame */
    uint32_t  acc;            /* +0x220 acumulador de ciclos */
    uint32_t  rate;           /* +0x2fc ciclos por setor (0x14585 = 83333 = 1x) */

    cdfifo_t  fifo_status;    /* +0x238 */
    cdfifo_t  fifo_cmd;       /* +0x278 */
    cdfifo_t  fifo_data;      /* +0x2b8 */
    uint8_t   buf_status[CD_FIFO_STATUS_SZ];
    uint8_t   buf_cmd[CD_FIFO_CMD_SZ];
    uint8_t   buf_data[CD_FIFO_DATA_SZ];

    uint8_t   toc[0x65 * 5];  /* +0x2c: 5 bytes/faixa (control, no, M, S, F); 100 = lead-out */

    uint32_t  stat_unk_cmd; uint8_t unk_cmd_first;   /* AUDITORIA: comandos que o dispatcher ignora (1o opcode visto) */
    uint32_t  stat_read_oob;                          /* AUDITORIA: leituras de setor fora da imagem */
    uint32_t  stat_cmd[256];                          /* AUDITORIA: histograma de opcodes */
    void    (*irq)(void *ud);
    void     *ud;
} cd_t;

/* depuracao: se nao for NULL recebe uma linha por comando do CD (limitado a ~300 linhas por execucao) */
extern void (*cd_trace_hook)(const char *msg);

int  cd_init(cd_t *cd, void *ud, void (*irq)(void *));
void cd_free(cd_t *cd);
int  cd_load_image(cd_t *cd, const char *path);   /* .iso .img .bin .cue .chd; 0 = ok, -1 = erro */
int  cd_read_sector(cd_t *cd, uint32_t lba, uint8_t *out);   /* 2048 bytes de dados do setor; 1 = ok, 0 = falhou */
void cd_reset(cd_t *cd, int media_present);   /* FUN_004aa020 */
void cd_build_toc(cd_t *cd);                  /* lead-out em +0x2c+100*5 a partir de num_sectors */
void cd_set_speed(cd_t *cd, int panel);       /* FUN_004a1390 case 6: indice 0..8 do painel */
void cd_dispatch(cd_t *cd);                   /* FUN_004aa440 */

/* vtable xdev_t do CLIO */
uint32_t cd_status_read(void *ctx);   /* slot +0x98: pop StatusFIFO */
void     cd_cmd_write(void *ctx, uint8_t v);  /* slot +0xa0: push CommandFIFO + dispatch */
uint32_t cd_poll_read(void *ctx);     /* slot +0xa8: byte +0x227 */
uint32_t cd_media_access(void *ctx);   /* S & POLRE: acesso a midia pendente (lido pelo CLIO antes de WR_COM/WR_POL) */
void     cd_poll_write(void *ctx, uint8_t v);  /* slot +0xb0: nibble baixo de +0x227 */
uint32_t cd_data_read(void *ctx);     /* slot +0xb8: pop DataFIFO + prefetch */
void     cd_data_write(void *ctx, uint8_t v);
uint32_t cd_res_read(void *ctx);      /* +0x228 DriveStatus */

void     cd_tick(cd_t *cd, uint32_t cyc);      /* slot +0x90 */

#endif
