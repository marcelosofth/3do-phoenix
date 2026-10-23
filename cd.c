/* cd.c - xbusMeiCdDrive reimplementado a partir do PhoenixEmuProject.exe.
 * RE: vtable 0x00c217e0; anéis devFIFO (FUN_00951b40 push, FUN_004a9f10 pop);
 * dispatcher de comandos FUN_004aa440; tick FUN_008fb060; prefetch FUN_004aa1b0.
 */
#include "cd.h"
#include "cd_chd.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

/* +0x2fc no construtor (FUN_004aca20): ciclos por setor */
#define CD_RATE_DEFAULT 0x14585u

/* Painel de velocidade (FUN_004a1390, case 6): escreve drive+0x2fc com o indice
 * da combo do front-end (qfrontend.cpp). Menor rate = leitora mais rapido. */
static const uint32_t k_cd_panel_rate[CD_SPEED_PANEL_N] = {
    0x14585u,   /* 0 predefinido   83333 = 1x  */
    0x28b0au,   /* 1              166666 = 0.5x */
    0x14585u,   /* 2               83333 = 1x  */
    0xa2c2u,    /* 3               41666 = 2x  */
    0x5161u,    /* 4               20833 = 4x  */
    0x28b0u,    /* 5               10416 = 8x  */
    0x1458u,    /* 6                5208 = 16x */
    0xa2cu,     /* 7                2604 = 32x */
    0u          /* 8                    0 = sem limite (nunca espera) */
};
void cd_set_speed(cd_t *cd, int panel) {
    if ((unsigned)panel >= CD_SPEED_PANEL_N) panel = 0;
    cd->rate = k_cd_panel_rate[panel];
}

/* ------------------------------------------------------------------ anel devFIFO */

static uint32_t fifo_avail(const cdfifo_t *f) {
    return f->wr < f->rd ? f->wr + f->cap - f->rd : f->wr - f->rd;
}

static int fifo_push(cdfifo_t *f, uint8_t b) {
    uint32_t free;
    if (f->wr < f->rd) free = f->rd - f->wr - 1u;
    else               free = f->rd + f->cap - f->wr - 1u;
    if (free == 0) return 0;
    f->buf[f->wr] = b;
    f->wr = (f->wr + 1) % f->cap;
    return 1;
}

static int fifo_pop(cdfifo_t *f, uint8_t *out) {
    if (fifo_avail(f) == 0) return 0;
    *out = f->buf[f->rd];
    f->rd = (f->rd + 1) % f->cap;
    return 1;
}

static void fifo_reset(cdfifo_t *f) {
    memset(f->buf, 0, f->cap);
    f->wr = f->rd = 0;
}

void (*cd_trace_hook)(const char *msg) = NULL;
static int cd_trace_n;
static void cd_tr(const char *fmt, ...) {
    char b[160]; va_list ap;
    static int cd_cmd_n;
    if (!cd_trace_hook) return;
    if (!strncmp(fmt, "CDT cmd", 7)) { if (cd_cmd_n >= 40000) return; cd_cmd_n++; }      /* comandos: sempre (diagnostico de carga/saida do jogo) */
    else { if (cd_trace_n >= 4000) return; cd_trace_n++; }                                  /* poll/fiq: so os primeiros 4000 */
    va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    cd_trace_hook(b);
}

static void irq_check(cd_t *cd) {                     /* FUN_004aa150 */
    if (((cd->S & 0x11) == 0x11 || (cd->S & 0x22) == 0x22) && cd->irq) {
        cd_tr("CDT fiq=1 poll=%02x", cd->S);
        cd->irq(cd->ud);
    }
}

static void cd_update_msf(cd_t *cd) {               /* +0x40 (FUN_008fb200) */
    uint32_t t = (uint32_t)cd->lba + 300;
    cd->msf[0] = (uint8_t)(t / 4500);
    cd->msf[1] = (uint8_t)((t % 4500) / 75);
    cd->msf[2] = (uint8_t)(t % 75);
}

/* Lê o setor atual no DataFIFO; devolve 1 se houver dados (machine +0x130/+0x138).
 * A fonte (imagem em memoria, .iso sob demanda ou .chd) e escolhida por cd_read_sector(). */
static int cd_fill_sector(cd_t *cd) {
    if (cd->lba < 0 || !cd_read_sector(cd, (uint32_t)cd->lba, cd->buf_data)) {
        cd_tr("setor %d FORA da imagem ou ilegivel (%u setores)", cd->lba, (unsigned)cd->num_sectors);
        cd->fifo_data.wr = cd->fifo_data.rd = 0;
        return 0;
    }
    cd->fifo_data.wr = CD_SECTOR_SIZE;
    cd->fifo_data.rd = 0;
    return 1;
}

/* ------------------------------------------------------------------ init / imagem */

int cd_init(cd_t *cd, void *ud, void (*irq)(void *)) {
    memset(cd, 0, sizeof *cd);
    cd->ud = ud; cd->irq = irq;
    cd->fifo_status.buf = cd->buf_status; cd->fifo_status.cap = CD_FIFO_STATUS_SZ;
    cd->fifo_cmd.buf    = cd->buf_cmd;    cd->fifo_cmd.cap    = CD_FIFO_CMD_SZ;
    cd->fifo_data.buf   = cd->buf_data;   cd->fifo_data.cap   = CD_FIFO_DATA_SZ;
    cd->rate = CD_RATE_DEFAULT;
    cd_reset(cd, 0);
    return 0;
}

void cd_free(cd_t *cd) {
    free(cd->image); cd->image = NULL; cd->image_size = 0;
    if (cd->iso_fp) { fclose(cd->iso_fp); cd->iso_fp = NULL; }
    cd->iso_ssize = 0; cd->iso_soff = 0;
    cd_chd_close(cd);
    cd->num_sectors = 0;
}

/* extensao (sem diferenciar maiusculas/minusculas) */
static int has_ext(const char *path, const char *ext) {
    size_t n = strlen(path), e = strlen(ext);
    if (n < e) return 0;
    for (size_t i = 0; i < e; i++) {
        char c = path[n - e + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != ext[i]) return 0;
    }
    return 1;
}

/* .iso: aberta e lida sob demanda. 2048 B/setor (cooked) ou 2352 (bruta, sync 00 FF*10 00; dados no byte 16, ou 24 no Mode 2). */
static int cd_load_iso(cd_t *cd, const char *path) {
    uint8_t h[16];
    long sz;
    uint32_t ssize = CD_SECTOR_SIZE, soff = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) <= 0) { fclose(f); return -1; }
    fseek(f, 0, SEEK_SET);
    if (fread(h, 1, sizeof h, f) == sizeof h && h[0] == 0x00 && h[11] == 0x00) {
        int sync = 1;
        for (int i = 1; i <= 10; i++) if (h[i] != 0xff) sync = 0;
        if (sync && (sz % 2352) == 0) { ssize = 2352; soff = (h[15] == 2) ? 24u : 16u; }
    }
    /* Sem sync no inicio: mesma regra do retro_cdimage_open_iso do Opera. Tamanho multiplo de 2048 = cooked;
     * so se NAO for e for multiplo de 2352 trata como bruto com os dados no byte 16 (dumps com cabecalho zerado). */
    if (ssize == CD_SECTOR_SIZE && (sz % 2048) != 0 && (sz % 2352) == 0) { ssize = 2352; soff = 16; }
    if ((size_t)sz < ssize) { fclose(f); return -1; }
    cd_free(cd);                                  /* so troca a imagem atual depois de tudo validado */
    cd->iso_fp = f;
    cd->iso_ssize = ssize;
    cd->iso_soff = soff;
    cd->num_sectors = (uint32_t)((size_t)sz / ssize);
    cd->image_size = (size_t)cd->num_sectors * CD_SECTOR_SIZE;
    return 0;
}

/* .cue: pega o primeiro FILE "x" BINARY (relativo a pasta do .cue) e abre como imagem bruta/cooked (a deteccao 2048/2352 e feita
 * por cd_load_iso). 3DO = disco de dados unico: faixas seguintes (audio) ficam dentro do mesmo arquivo e sao ignoradas. */
static int cd_load_cue(cd_t *cd, const char *cue) {
    FILE *f = fopen(cue, "rb"); char line[1024], name[1024], full[2048];
    if (!f) return -1;
    name[0] = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line; while (*p == ' ' || *p == '\t' || (unsigned char)*p == 0xEF || (unsigned char)*p == 0xBB || (unsigned char)*p == 0xBF) p++;
        if (strncasecmp(p, "FILE", 4) != 0) continue;
        p += 4; while (*p == ' ' || *p == '\t') p++;
        if (*p == '"') { p++; char *q = strchr(p, '"'); if (!q) continue; *q = 0; snprintf(name, sizeof name, "%s", p); }
        else { char *q = p; while (*q && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n') q++; *q = 0; snprintf(name, sizeof name, "%s", p); }
        break;
    }
    fclose(f);
    if (!name[0]) return -1;
    const char *sl = strrchr(cue, '/'), *bs = strrchr(cue, '\\');
    if (bs && (!sl || bs > sl)) sl = bs;
    if (sl && name[0] != '/' && !(name[1] == ':')) snprintf(full, sizeof full, "%.*s%s", (int)(sl - cue + 1), cue, name);
    else snprintf(full, sizeof full, "%s", name);
    return cd_load_iso(cd, full);
}

/* .iso/.img/.bin (cooked 2048 ou bruto 2352), .cue e .chd. Falha nao derruba a imagem que ja estava carregada. */
int cd_load_image(cd_t *cd, const char *path) {
    if (!path) return -1;
    if (has_ext(path, ".chd")) return cd_load_chd(cd, path);
    if (has_ext(path, ".cue")) return cd_load_cue(cd, path);
    if (has_ext(path, ".iso") || has_ext(path, ".img") || has_ext(path, ".bin")) return cd_load_iso(cd, path);
    return -1;
}

/* 2048 bytes de dados do setor `lba`; 1 = ok, 0 = fora da imagem / erro de leitura.
 * Ordem: image (memoria, so testes) -> iso_fp -> chd. */
int cd_read_sector(cd_t *cd, uint32_t lba, uint8_t *out) {
    if (lba >= cd->num_sectors) { cd->stat_read_oob++; return 0; }
    if (cd->image) {
        if (((size_t)lba + 1u) * CD_SECTOR_SIZE > cd->image_size) return 0;
        memcpy(out, cd->image + (size_t)lba * CD_SECTOR_SIZE, CD_SECTOR_SIZE);
        return 1;
    }
    if (cd->iso_fp) {
        long off = (long)((uint64_t)lba * cd->iso_ssize + cd->iso_soff);
        if (fseek(cd->iso_fp, off, SEEK_SET) != 0) return 0;
        return fread(out, 1, CD_SECTOR_SIZE, cd->iso_fp) == CD_SECTOR_SIZE;
    }
    if (cd->chd) return cd_chd_read(cd, lba, out);
    return 0;
}

/* FUN_004aa020: reset do drive (valores do Phoenix). */
void cd_reset(cd_t *cd, int media_present) {
    cd->lba = 0;
    if (media_present) cd_update_msf(cd);
    cd->R = 0xe1;
    /* Com disco: POLRE (0x80, "acesso a midia") ligado e nibble de enables zerado, como o
     * opera_cdrom_media_load (poll=0x80). A BIOS le o 0x80, escreve v=0x80 para limpar e
     * segue o caminho de identificacao de disco novo. Sem disco: 0x0f do exe, como antes. */
    cd->S = media_present ? CD_S_MEDIA_ACC : 0x0f;
    cd->state = 0;
    cd->track = 1; cd->index = 1; cd->f0x226 = 0;
    /* faixa 1: 5 bytes em +0x2c + t*5 */
    memset(cd->toc, 0, sizeof cd->toc);
    cd->toc[1 * 5 + 0] = 4;
    cd->toc[1 * 5 + 1] = 1;
    cd->toc[1 * 5 + 2] = 0;
    cd->toc[1 * 5 + 3] = 2;
    cd->toc[1 * 5 + 4] = 0;
    fifo_reset(&cd->fifo_status);
    fifo_reset(&cd->fifo_cmd);
    fifo_reset(&cd->fifo_data);
    cd->acc = 0;
    cd->remain = 0;
}

/* TOC real do disco carregado.
 * O exe so escreve a faixa 1 no reset (+0x31..+0x35) e mantem o resto zerado;
 * o array do painel sao 500 bytes em +0x2c, entao faixa 0..99 e a unica janela
 * com leitura coerente (t = 100/101 cairia em +0x228/+0x22c, os registradores
 * da propria unidade). Um disco 3DO gravado e sessao unica de dados: faixa 1 no
 * setor 0, sem outras faixas. O que falta preencher e o fim do disco, e o
 * indice livre ate a guarda do dispatcher (< 0x65) e o 100. */
void cd_build_toc(cd_t *cd) {
    uint32_t f = cd->num_sectors + 150u;      /* MSF = LBA + 2 s (150 quadros) */
    uint8_t *e = &cd->toc[CD_TOC_LEADOUT * 5u];
    e[0] = 4;                                /* control: faixa de dados */
    e[1] = 0xaa;                             /* numero de faixa do lead-out */
    e[2] = (uint8_t)(f / 4500u);
    e[3] = (uint8_t)((f % 4500u) / 75u);
    e[4] = (uint8_t)(f % 75u);
}

/* ------------------------------------------------------------------ vtable xdev_t */

uint32_t cd_poll_read(void *ctx) {                                     /* +0xa8 */
    static int last_poll = -1;
    cd_t *cd = ctx;
    if ((int)cd->S != last_poll) { cd_tr("CDT poll_read %02x", cd->S); last_poll = cd->S; }
    return cd->S;
}

void cd_poll_write(void *ctx, uint8_t v) {                             /* +0xb0 */
    cd_t *cd = ctx;
    uint8_t old_S = cd->S;
    /* POLRE: escrever 1 no bit 7 limpa o latch de acesso a midia (opera_cdrom_set_poll). */
    if (v & CD_S_MEDIA_ACC) cd->S &= (uint8_t)~CD_S_MEDIA_ACC;
    cd->S = (uint8_t)((cd->S & 0xf0) | (v & 0x0f));
    cd_tr("CDT poll_write v=%02x S: %02x -> %02x lba=%u", v, old_S, cd->S, (unsigned)cd->lba);
    irq_check(cd);
}

uint32_t cd_media_access(void *ctx) { return ((cd_t *)ctx)->S & CD_S_MEDIA_ACC; }

uint32_t cd_status_read(void *ctx) {                                   /* +0x98 */
    cd_t *cd = ctx;
    uint8_t b = 0;
    if (fifo_avail(&cd->fifo_status) == 0) return 0;
    if (!fifo_pop(&cd->fifo_status, &b)) return 0;
    if (fifo_avail(&cd->fifo_status) == 0) cd->S &= (uint8_t)~CD_S_CMD_RDY;
    return b;
}

uint32_t cd_res_read(void *ctx) { return ((cd_t *)ctx)->R; }

void cd_data_write(void *ctx, uint8_t v) { (void)ctx; (void)v; }

uint32_t cd_data_read(void *ctx) {                                     /* +0xb8 */
    cd_t *cd = ctx;
    uint8_t b = 0;
    if (fifo_avail(&cd->fifo_data) == 0) return 0;
    if (!fifo_pop(&cd->fifo_data, &b)) return 0;
    if (fifo_avail(&cd->fifo_data) != 0) return b;

    fifo_reset(&cd->fifo_data);
    if (cd->remain != 0 && cd->rate <= cd->acc) {
        if (cd->remain < 0) { cd->S |= CD_S_CMD_RDY; cd->remain = -cd->remain; irq_check(cd); }
        if (cd_fill_sector(cd)) {
            cd->lba++;
            if (fifo_avail(&cd->fifo_data) == 0) cd->S &= (uint8_t)~CD_S_DATA_RDY;
            else if (!(cd->S & CD_S_DATA_RDY)) { cd->S |= CD_S_DATA_RDY; irq_check(cd); }
        } else {
            cd->S &= (uint8_t)~CD_S_DATA_RDY;
        }
        cd->remain--;
        cd->acc -= cd->rate;
    } else {
        cd->S &= (uint8_t)~CD_S_DATA_RDY;
    }
    return b;
}

void cd_cmd_write(void *ctx, uint8_t v) {                              /* +0xa0 */
    cd_t *cd = ctx;
    if (fifo_avail(&cd->fifo_cmd) < 7) fifo_push(&cd->fifo_cmd, v);
    if (fifo_avail(&cd->fifo_cmd) < 7 && cd->buf_cmd[cd->fifo_cmd.rd % CD_FIFO_CMD_SZ] != 0x08) return;
    cd_dispatch(cd);
    fifo_reset(&cd->fifo_cmd);
    irq_check(cd);
}

/* ------------------------------------------------------------------ tick (+0x90) */

void cd_tick(cd_t *cd, uint32_t cyc) {
    cd->acc += cyc;
    if ((cd->rate << 4) < cd->acc) cd->acc = cd->rate << 4;
    if (cd->remain == 0) return;
    if (fifo_avail(&cd->fifo_data) != 0) return;
    if (cd->rate > cd->acc) return;

    if (cd->remain < 0) { cd->S |= CD_S_CMD_RDY; cd->remain = -cd->remain; irq_check(cd); }
    if (cd_fill_sector(cd)) {
        cd->lba++;
        cd_update_msf(cd);
        if (fifo_avail(&cd->fifo_data) == 0) cd->S &= (uint8_t)~CD_S_DATA_RDY;
        else if (!(cd->S & CD_S_DATA_RDY)) { cd->S |= CD_S_DATA_RDY; irq_check(cd); }
    } else {
        cd->S &= (uint8_t)~CD_S_DATA_RDY;
    }
    cd->remain--;
    cd->acc -= cd->rate;
}

/* ------------------------------------------------------------------ comandos MEI */

#define CDB(i) cd->buf_cmd[(cd->fifo_cmd.rd + (i)) % CD_FIFO_CMD_SZ]
#define P(b)   fifo_push(&cd->fifo_status, (uint8_t)(b))
/* os dois portoes "unidade pronta" do exe: bit 7 de R com 0x40, e com 0x60 inteiro */
#define READY40 (((char)Rm < '\0') && ((Rraw & 0x40) != 0))
#define READY60 (((char)Rm < '\0') && ((Rraw & 0x60) == 0x60))

static uint32_t msf_to_lba(uint32_t m, uint32_t s, uint32_t fr) {
    int32_t l = (int32_t)((m * 60 + s) * 75 + fr) - 150;
    return l < 0 ? 0 : (uint32_t)l;
}

/* caminho de recusa do exe (LAB_004aa800): R |= 0x10, eco do opcode + R, estado 2 */
static void refuse(cd_t *cd, uint8_t op, uint8_t *Rm) {
    *Rm = (uint8_t)(*Rm | 0x10);
    cd->R = *Rm;
    P(op);
    P(*Rm);
    cd->state = 2;
}

void cd_dispatch(cd_t *cd) {                                           /* FUN_004aa440 */
    uint8_t Rraw = cd->R, Sraw = cd->S;
    uint8_t Rm = (uint8_t)(Rraw & 0xee), Sm = (uint8_t)(Sraw & 0xcf);
    fifo_reset(&cd->fifo_status);     /* +0x260 memset + wr/rd zerados na entrada */
    cd->R = Rm;                     /* +0x228 &= 0xee: cai o bit 0 (erro) e o 0x10 */
    cd->S = Sm;                     /* +0x227 &= 0xcf: cai CMD_RDY (0x10) e DATA_RDY (0x20) */
    uint8_t op = CDB(0);
    cd_tr("CDT cmd %02x | %02x %02x %02x %02x %02x %02x S=%02x lba=%u R=%02x", op, CDB(1), CDB(2), CDB(3), CDB(4), CDB(5), CDB(6), Sraw, (unsigned)cd->lba, Rraw);
    if (op > 0x93) return;

    cd->stat_cmd[op]++;
    switch (op) {
    case 0x02:
        if (READY40) { cd->state = 0; cd->R = (uint8_t)(Rm | 0x21); }
        else         { cd->state = 2; cd->R = (uint8_t)(Rm | 0x10); }
        cd->S = (uint8_t)(Sm | CD_S_CMD_RDY);
        P(0x02); P(cd->R);
        break;
    case 0x03:
        if (READY40) { cd->state = 0; cd->R = (uint8_t)((Rraw & 0xce) | 1); }
        else         { cd->state = 2; cd->R = (uint8_t)(Rm | 0x11); }
        cd->S = (uint8_t)(Sm | CD_S_CMD_RDY);
        P(0x03); P(cd->R);
        break;
    case 0x06:
        cd->R = (uint8_t)((Rraw & 0x0c) | 1);
        cd->S = (uint8_t)((Sraw & 0x8f) | CD_S_CMD_RDY);   /* 0x06 tambem derruba DATA_RDY */
        cd->state = 0;
        P(0x06); P(cd->R);
        break;
    case 0x08:
        P(0x08);
        for (int i = 0; i < 31; i++) P(0);
        P(cd->R);                    /* exe escreve o R ANTES de liga o bit 0 */
        cd->state = 0;
        cd->R = (uint8_t)(cd->R | 1);
        return;                      /* LAB_004abd51/return: nao passa pelo rabito CMD_RDY */
    case 0x09:
        cd->R = (uint8_t)(Rm | 1);
        cd->state = 0;
        cd->S = (uint8_t)(Sm | CD_S_CMD_RDY);
        P(0x09); P(cd->R);
        break;
    case 0x0b:
        cd->R = (uint8_t)(Rm | 1);
        P(0x0b);
        for (int i = 0; i < 31; i++) P(0);
        P(cd->R);
        cd->state = 0;
        return;                      /* goto LAB_004ab80c: so state = 0 */
    case 0x10: {                          /* seek + leitura: MSF em cdb[1..3], nº de setores em cdb[4..6] (24 bits) */
        if (!READY60) { refuse(cd, 0x10, &Rm); cd->S = (uint8_t)(cd->S | CD_S_CMD_RDY); break; }
        int32_t old = cd->lba;
        cd->R = (uint8_t)(Rm | 1);
        P(0x10); P(cd->R);
        cd->lba = (int32_t)msf_to_lba(CDB(1), CDB(2), CDB(3));
        /* contagem de setores de 24 bits (cdb[4..6], MSB primeiro) — confirmado
         * contra opera_cdrom.c:3615-3618 (blocks_requested = cmd[4]<<16 | cmd[5]<<8 | cmd[6]).
         * a versao anterior lia so cdb[5..6] (16 bits) e descartava cdb[4]. */
        cd->remain = -((int32_t)(((uint32_t)CDB(4) << 16) | ((uint32_t)CDB(5) << 8) | (uint32_t)CDB(6)));
        fifo_reset(&cd->fifo_data);
        if (cd->lba == old) { if (cd->rate < cd->acc) cd->acc = cd->rate; }
        else                { cd->acc = 0; }
        cd_update_msf(cd);
        cd->state = 0;
        return;                      /* goto LAB_004ab80c: CMD_RDY so acende no primeiro setor */
    }
    case 0x80:
        cd->R = (uint8_t)(Rm | 1);
        P(0x80); P(0xaa); P(0x55); P(cd->R);
        cd->S = (uint8_t)(cd->S | CD_S_CMD_RDY);
        cd->state = 0;
        break;
    case 0x82:
        P(0x82);
        for (int i = 0; i < 8; i++) P(cd->state);
        cd->R = (uint8_t)(cd->R | 1);
        P(cd->R);
        break;
    case 0x83:
        cd->R = (uint8_t)(Rm | 1);
        P(0x83); P(0); P(0x10); P(0); P(1);
        for (int i = 0; i < 6; i++) P(0);
        P(cd->R);
        cd->S = (uint8_t)(cd->S | CD_S_CMD_RDY);
        cd->state = 0;
        break;
    case 0x84:
        P(0); P(0); P(0);
        if (((char)cd->R < '\0') && ((cd->R & 0x40) != 0)) cd->R = (uint8_t)(cd->R | 1);
        else cd->R = (uint8_t)((cd->R & 0xfe) | 0x10);
        cd->S = (uint8_t)(cd->S | CD_S_CMD_RDY);
        P(cd->R);
        break;
    case 0x85:                                       /* posicao atual em MSF */
        if (!READY60) { refuse(cd, 0x85, &Rm); cd->S = (uint8_t)(cd->S | CD_S_CMD_RDY); break; }
        /* O kernel (fz1, 0x32724..0x3275c) faz dev_blocos = min(dev_blocos, MSF desta resposta),
         * ou seja, espera o MSF do fim do disco, nao a posicao atual (antes: 363 blocos). */
        { uint32_t fe = cd->num_sectors + 150u;
          P(0x85); P(0); P((uint8_t)(fe / 4500u)); P((uint8_t)((fe % 4500u) / 75u)); P((uint8_t)(fe % 75u)); P(0); P(0); }
        cd->R = (uint8_t)(cd->R | 1);
        P(cd->R);
        cd->state = 0;
        cd->S = (uint8_t)(cd->S | CD_S_CMD_RDY);
        break;
    case 0x87: case 0x8a: {                          /* LAB_004ab783: eco + 10 zeros + R */
        /* o bloco de recusa compartilhado (LAB_004aa800) devolve SEMPRE 0x85, nao o opcode */
        if (!READY60) { refuse(cd, 0x85, &Rm); cd->S = (uint8_t)(cd->S | CD_S_CMD_RDY); break; }
        P(op);
        for (int i = 0; i < 5; i++) P(0);
        cd->R = (uint8_t)(cd->R | 1);
        for (int i = 0; i < 5; i++) P(0);
        P(cd->R);
        cd->S = (uint8_t)(cd->S | CD_S_CMD_RDY);
        cd->state = 0;
        break;
    }
    case 0x8b:                                       /* status da unidade + posicao */
        P(0x8b);
        if ((cd->R & 0xe0) != 0) {
            /* O kernel (fz1, 0x32614..0x32640) le estes 3 bytes como MSF do FIM do disco e faz
             * blocos = (min*60+seg)*75+quadro -> [dev+0xb4]; leituras alem disso dao 0xd13d100d.
             * Antes devolviamos a posicao atual (363 blocos) e o folio 'audio' nao carregava. */
            uint32_t fe = cd->num_sectors + 150u;
            P(cd->f0x226); P(cd->track); P(cd->index);
            P((uint8_t)(fe / 4500u)); P((uint8_t)((fe % 4500u) / 75u)); P((uint8_t)(fe % 75u));
            cd->state = 0;
            cd->R = (uint8_t)(cd->R | 1);
            P(cd->R);
        } else {
            cd->state = 2;
            cd->R = (uint8_t)(Rm | 0x10);
            P(cd->R);
        }
        break;
    case 0x8c: {                                     /* TOC da faixa cdb[2] (5 bytes por faixa em +0x2c) */
        uint8_t t = CDB(2);
        P(0x8c);
        if (t >= 0x65) {
            cd->R = (uint8_t)(Rm | 0x10);
            P(cd->R);
            cd->state = 4;
        } else if ((cd->R & 0xe0) == 0) {
            cd->R = (uint8_t)(Rm | 0x10);
            P(cd->R);
            cd->state = 2;
        } else {
            const uint8_t *e = &cd->toc[(uint32_t)t * 5u];
            P(0); P(e[0]); P(e[1]); P(0); P(e[2]); P(e[3]); P(e[4]); P(0);
            cd->R = (uint8_t)(cd->R | 1);
            P(cd->R);
            cd->state = 0;
        }
        break;
    }
    case 0x8d:                                       /* SubQ */
        P(0x8d);
        if (((char)cd->R < '\0') && ((cd->R & 0x40) != 0)) {
            P(0); P(0); P(2); P(0); P(0);
            cd->R = (uint8_t)(cd->R | 1);
            P(0); P(cd->R);
            cd->state = 0;
        } else {
            cd->R = (uint8_t)(Rm | 0x10);
            P(cd->R);
            cd->state = 2;
        }
        break;
    case 0x93:
        P(0); P(0); P(0);
        if (((char)cd->R < '\0') && ((cd->R & 0x40) != 0)) cd->R = (uint8_t)(cd->R | 1);
        else cd->R = (uint8_t)(cd->R | 0x11);
        cd->S = (uint8_t)(cd->S | CD_S_CMD_RDY);
        P(cd->R);
        break;
    default:
        cd->stat_unk_cmd++; if (cd->stat_unk_cmd == 1) cd->unk_cmd_first = op;
        return;                                       /* switchD_004aa4d5_caseD_0: nao toca em nada */
    }

    cd->S = (uint8_t)(cd->S | CD_S_CMD_RDY);          /* rabito comum do switch no exe */
}
