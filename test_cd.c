#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
/* test_cd.c - CD no XBUS (PARTE20): monta uma ISO sintetica e fala com o drive como a BIOS faria, pelos registradores do
 * CLIO em 0x03400500..0x034005ff (clio_peek/clio_poke), sem tocar em cd_*() diretamente.
 *   0x500 select/res  0x540 poll (+0x227)  0x580 comando / status FIFO  0x5c0 dados
 * Os valores esperados vem de cd.c (FUN_004aa440 etc., decompilado do exe): coerencia com a leitura do .exe, nao
 * com uma execucao dele. */
#include "machine.h"
#include "cd_chd.h"
#include <formats/rchd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, total;
#define CHK(c, msg) do { total++; if (!(c)) { printf("FALHA: %s\n", msg); fails++; } } while (0)

#define NSEC 40
#define ISO_NAME "test_cd_tmp.iso"

static ph_machine_t m;

static uint8_t iso_byte(uint32_t s, uint32_t i) {
    if (i < 4) return (uint8_t)(s >> (8 * (3 - i)));      /* numero do setor, big-endian */
    return (uint8_t)(s * 31u + i);
}

static int make_iso(void) {
    FILE *f = fopen(ISO_NAME, "wb");
    if (!f) return -1;
    for (uint32_t s = 0; s < NSEC; s++)
        for (uint32_t i = 0; i < CD_SECTOR_SIZE; i++) fputc(iso_byte(s, i), f);
    fclose(f);
    return 0;
}

/* ---- acesso ao XBUS pelo CLIO ---- */
#define XB(off) (0x03400000u | (off))
static uint32_t xpeek(uint32_t off)             { return clio_peek(&m.clio, XB(off)); }
static void     xpoke(uint32_t off, uint32_t v) { clio_poke(&m.clio, XB(off), v); }

static void xbus_enable(void) {
    clio_poke(&m.clio, 0x03400400u, 0);             /* v sem 0x800 => liga o bit 0x80 (acesso ao XBUS) */
    xpoke(0x500, 0);                                /* seleciona o dispositivo 0 (CD) */
}

static void send_cmd(uint8_t op, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5, uint8_t b6) {
    if (op == 0x08) { xpoke(0x580, op); return; }    /* unico comando de 1 byte */
    uint8_t c[7] = { op, b1, b2, b3, b4, b5, b6 };
    for (int i = 0; i < 7; i++) xpoke(0x580, c[i]);
}

static int rd_status(uint8_t *out, int max) {
    int n = 0;
    while ((xpeek(0x540) & CD_S_CMD_RDY) && n < max) out[n++] = (uint8_t)xpeek(0x580);
    return n;
}

static void lba_to_msf(uint32_t lba, uint8_t *mm, uint8_t *ss, uint8_t *ff) {
    uint32_t t = lba + 150;
    *mm = (uint8_t)(t / 4500); *ss = (uint8_t)((t % 4500) / 75); *ff = (uint8_t)(t % 75);
}

static void send_read(uint32_t lba, uint32_t count) {
    uint8_t mm, ss, ff; lba_to_msf(lba, &mm, &ss, &ff);
    send_cmd(0x10, mm, ss, ff, 0, (uint8_t)(count >> 8), (uint8_t)count);
}

static int read_sector_ok(uint32_t s) {                /* 2048 bytes pelo registrador de dados, comparados com a ISO */
    for (uint32_t i = 0; i < CD_SECTOR_SIZE; i++)
        if ((uint8_t)xpeek(0x5c0) != iso_byte(s, i)) return 0;
    return 1;
}


/* ------------------------------------------------------------------------------------------------
 * Carregamento de .iso e .chd (mesmas regras do retro_cdimage do opera-libretro)
 * ---------------------------------------------------------------------------------------------- */
#define RAW_NAME "test_cd_tmp_raw.iso"
#define CHD_NAME "test_cd_tmp.chd"

static const uint8_t SYNC12[12] = { 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0 };

/* ISO bruta: cada setor = sync(12) + cabecalho(4) + 2048 de dados + 288 de EDC/ECC (lixo) */
static int make_raw_iso_ex(uint32_t n, int with_sync) {
    FILE *f = fopen(RAW_NAME, "wb");
    if (!f) return -1;
    for (uint32_t s = 0; s < n; s++) {
        uint8_t hdr[16] = {0}; if (with_sync) { memcpy(hdr, SYNC12, 12); hdr[15] = 1; }
        fwrite(hdr, 1, 16, f);
        for (uint32_t i = 0; i < CD_SECTOR_SIZE; i++) fputc(iso_byte(s, i), f);
        for (int i = 0; i < 288; i++) fputc(0xee, f);
    }
    fclose(f);
    return 0;
}
static int make_raw_iso(uint32_t n) { return make_raw_iso_ex(n, 1); }

static int sector_equals(cd_t *cd, uint32_t s) {
    uint8_t b[CD_SECTOR_SIZE];
    if (!cd_read_sector(cd, s, b)) return 0;
    for (uint32_t i = 0; i < CD_SECTOR_SIZE; i++) if (b[i] != iso_byte(s, i)) return 0;
    return 1;
}

/* ---- CHD sintetico: o escritor do proprio rchd (hunks sem compressao) + um bloco de metadados CHT2 anexado ---- */
typedef struct { FILE *f; } wctx_t;
static int wsink(void *ctx, uint64_t off, const void *d, size_t n) {
    FILE *f = ((wctx_t *)ctx)->f;
    return fseek(f, (long)off, SEEK_SET) == 0 && fwrite(d, 1, n, f) == n;
}

/* Cria um CHD v5 de CD com 1 faixa. unit = tamanho do quadro no fluxo logico (2352 ou 2448); `type` = texto TYPE:
 * "MODE1_RAW" (dados no byte 16 do quadro) ou "MODE1" (dados no byte 0). frames = numero de quadros da faixa.
 * pregap_stored: 1 = PGTYPE ausente com PREGAP:0; so usamos pregap 0 aqui. */
static int make_chd(const char *path, const char *type, uint32_t unit, uint32_t frames, int single_track_only,
                    uint32_t audio_frames) {
    uint32_t hunk = unit * 4;                          /* 4 quadros por hunk (como o chdman) */
    uint32_t pad  = (4 - frames % 4) % 4;
    uint32_t total = frames + pad;
    uint32_t afr = audio_frames, apad = (4 - afr % 4) % 4;
    if (!single_track_only) total += afr + apad;
    uint64_t logical = (uint64_t)total * unit;
    uint32_t hunks = (uint32_t)((logical + hunk - 1) / hunk);
    FILE *f = fopen(path, "w+b");
    wctx_t ctx = { f };
    if (!f) return -1;
    rchd_writer_t *w = rchd_write_new((uint64_t)hunks * hunk, hunk, unit, wsink, &ctx);
    if (!w) { fclose(f); return -1; }
    /* o escritor exige hunks inteiros; monta o fluxo quadro a quadro */
    uint8_t *frame = malloc(unit), *acc = calloc(1, hunk);
    uint32_t in_hunk = 0;
    for (uint32_t fr = 0; fr < hunks * 4; fr++) {
        memset(frame, 0, unit);
        if (fr < frames) {
            /* dados: o setor fr com 2048 bytes de iso_byte() na posicao `off`; o resto do quadro e marcador */
            uint32_t off = !strcmp(type, "MODE1_RAW") ? 16 : 0;
            memset(frame, 0x77, unit);
            if (off) memcpy(frame, SYNC12, 12);
            for (uint32_t i = 0; i < CD_SECTOR_SIZE; i++) frame[off + i] = iso_byte(fr, i);
        } else if (!single_track_only && fr >= frames + pad && fr < frames + pad + afr) {
            memset(frame, 0x55, unit);
        }
        memcpy(acc + (size_t)in_hunk * unit, frame, unit);
        if (++in_hunk == 4) {
            if (rchd_write_hunk(w, acc, hunk) != RCHD_OK) { free(frame); free(acc); rchd_write_free(w); fclose(f); return -1; }
            in_hunk = 0; memset(acc, 0, hunk);
        }
    }
    free(frame); free(acc);
    if (rchd_write_finish(w) != RCHD_OK) { rchd_write_free(w); fclose(f); return -1; }
    rchd_write_free(w);

    /* metadados: entradas CHT2 encadeadas no fim do arquivo; o ponteiro da cabeca (offset 48 no v5) aponta para a 1a */
    char t1[160], t2[160];
    snprintf(t1, sizeof t1, "TRACK:1 TYPE:%s SUBTYPE:NONE FRAMES:%u PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0", type, frames);
    snprintf(t2, sizeof t2, "TRACK:2 TYPE:AUDIO SUBTYPE:NONE FRAMES:%u PREGAP:0 PGTYPE:AUDIO PGSUB:NONE POSTGAP:0", afr);
    fseek(f, 0, SEEK_END);
    long meta1 = ftell(f);
    size_t l1 = strlen(t1) + 1, l2 = strlen(t2) + 1;
    uint8_t h[16] = { 'C','H','T','2', 1, (uint8_t)(l1 >> 16), (uint8_t)(l1 >> 8), (uint8_t)l1 };
    long meta2 = single_track_only ? 0 : (long)(meta1 + 16 + l1);
    for (int i = 0; i < 8; i++) h[8 + i] = (uint8_t)((uint64_t)meta2 >> (8 * (7 - i)));
    fwrite(h, 1, 16, f); fwrite(t1, 1, l1, f);
    if (!single_track_only) {
        uint8_t h2[16] = { 'C','H','T','2', 1, (uint8_t)(l2 >> 16), (uint8_t)(l2 >> 8), (uint8_t)l2 };
        fwrite(h2, 1, 16, f); fwrite(t2, 1, l2, f);
    }
    fseek(f, 48, SEEK_SET);
    uint8_t mp[8]; for (int i = 0; i < 8; i++) mp[i] = (uint8_t)((uint64_t)meta1 >> (8 * (7 - i)));
    fwrite(mp, 1, 8, f);
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------------------------------------
 * Opcao "CD: comandos extras (Opera)" (phoenix_cd_opera_extra_cmds, padrao desligada): fala com um
 * cd_t isolado pela mesma via de protocolo da BIOS (cd_cmd_write/cd_status_read/cd_poll_read), sem
 * passar pelo CLIO/XBUS (nao precisa de uma ph_machine_t inteira para isto).
 * ---------------------------------------------------------------------------------------------- */
static void direct_send_cmd(cd_t *cd, uint8_t op, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5, uint8_t b6) {
    if (op == 0x08) { cd_cmd_write(cd, op); return; }
    uint8_t c[7] = { op, b1, b2, b3, b4, b5, b6 };
    for (int i = 0; i < 7; i++) cd_cmd_write(cd, c[i]);
}
static int direct_rd_status(cd_t *cd, uint8_t *out, int max) {
    int n = 0;
    while ((cd_poll_read(cd) & CD_S_CMD_RDY) && n < max) out[n++] = (uint8_t)cd_status_read(cd);
    return n;
}

static void test_opera_extra_cmds(void) {
    cd_t cd;
    uint8_t st[16]; int n;
    static const uint8_t ops[] = { 0x01, 0x04, 0x07, 0x0a, 0x0e, 0x0f, 0x86, 0x88, 0x89, 0x8e };

    cd_init(&cd, NULL, NULL);
    CHK(cd_load_image(&cd, ISO_NAME) == 0, "extra: carrega a iso");
    cd_build_toc(&cd);
    CHK(cd.opera_extra == 0, "extra: cd_init comeca com a opcao desligada (padrao)");

    /* desligada: os 10 opcodes extras caem no MESMO caminho do default -- nenhuma resposta, so
     * incrementa o histograma de comandos desconhecidos, igual a um opcode que o .exe tambem ignora */
    for (unsigned i = 0; i < sizeof ops; i++) {
        uint32_t unk_before = cd.stat_unk_cmd;
        direct_send_cmd(&cd, ops[i], 0, 0, 0, 0, 0, 0);
        n = direct_rd_status(&cd, st, 16);
        CHK(n == 0 && !(cd.S & CD_S_CMD_RDY) && cd.stat_unk_cmd == unk_before + 1,
            "extra desligada: opcode cai no caminho do default (nenhuma resposta)");
    }

    /* ligada: cada opcode responde como o opera_cdrom.c (opera-libretro), adaptado aos registradores do Phoenix */
    cd.opera_extra = 1;

    direct_send_cmd(&cd, 0x01, 0, 0, 0, 0, 0, 0);
    n = direct_rd_status(&cd, st, 16);
    CHK(n == 2 && st[0] == 0x01 && (st[1] & 0x10) && cd.state == 2, "extra ligada: 0x01 SEEK sempre recusa (illegal_request)");

    direct_send_cmd(&cd, 0x04, 0, 0, 0, 0, 0, 0);
    n = direct_rd_status(&cd, st, 16);
    CHK(n == 2 && st[0] == 0x04 && (st[1] & 0x10), "extra ligada: 0x04 DIAGNOSTICS sempre recusa");

    direct_send_cmd(&cd, 0x86, 0, 0, 0, 0, 0, 0);
    n = direct_rd_status(&cd, st, 16);
    CHK(n == 2 && st[0] == 0x86 && (st[1] & 0x10), "extra ligada: 0x86 READ_HEADER sempre recusa");

    direct_send_cmd(&cd, 0x88, 0, 0, 0, 0, 0, 0);
    n = direct_rd_status(&cd, st, 16);
    CHK(n == 2 && st[0] == 0x88 && (st[1] & 0x10), "extra ligada: 0x88 READ_UPC sempre recusa");

    direct_send_cmd(&cd, 0x89, 0, 0, 0, 0, 0, 0);
    n = direct_rd_status(&cd, st, 16);
    CHK(n == 2 && st[0] == 0x89 && (st[1] & 0x10), "extra ligada: 0x89 READ_ISRC sempre recusa");

    direct_send_cmd(&cd, 0x8e, 0, 0, 0, 0, 0, 0);
    n = direct_rd_status(&cd, st, 16);
    CHK(n == 2 && st[0] == 0x8e && (st[1] & 0x10), "extra ligada: 0x8e READ_DEVICE_DRIVER sempre recusa");

    cd.lba = 30; cd.remain = -5; cd.fifo_data.wr = 100;      /* simula transferencia em curso + posicao fora do 0 */
    direct_send_cmd(&cd, 0x07, 0, 0, 0, 0, 0, 0);
    n = direct_rd_status(&cd, st, 16);
    CHK(n == 2 && st[0] == 0x07 && (st[1] & 1) && cd.lba == 0 && cd.remain == 0 &&
        cd.fifo_data.wr == cd.fifo_data.rd && (cd.S & CD_S_MEDIA_ACC),
        "extra ligada: 0x07 INJECT_DISC reseta posicao/transferencia e sinaliza midia nova (sem troca real de midia)");

    cd.remain = -3; cd.fifo_data.wr = 50; cd.lba = 17;
    direct_send_cmd(&cd, 0x0a, 0, 0, 0, 0, 0, 0);
    n = direct_rd_status(&cd, st, 16);
    CHK(n == 2 && st[0] == 0x0a && (st[1] & 1) && cd.lba == 17 && cd.remain == 0 && cd.fifo_data.wr == cd.fifo_data.rd,
        "extra ligada: 0x0a RESET limpa a transferencia em curso mas mantem a posicao/midia");

    direct_send_cmd(&cd, 0x0e, 0, 0, 0, 0, 0, 0);
    n = direct_rd_status(&cd, st, 16);
    CHK(n == 2 && st[0] == 0x0e && (st[1] & 1), "extra ligada: 0x0e PLAY_AUDIO_MSF so confirma (sem trilha de audio real)");

    direct_send_cmd(&cd, 0x0f, 0, 0, 0, 0, 0, 0);
    n = direct_rd_status(&cd, st, 16);
    CHK(n == 2 && st[0] == 0x0f && (st[1] & 1), "extra ligada: 0x0f PLAY_AUDIO_TRACK so confirma (sem trilha de audio real)");

    /* um opcode de verdade desconhecido (nem no .exe, nem no Opera) continua ignorado mesmo com a opcao ligada */
    { uint32_t unk_before = cd.stat_unk_cmd;
      direct_send_cmd(&cd, 0x50, 0, 0, 0, 0, 0, 0);
      n = direct_rd_status(&cd, st, 16);
      CHK(n == 0 && cd.stat_unk_cmd == unk_before + 1, "extra ligada: opcode realmente desconhecido (0x50) continua ignorado"); }

    cd_free(&cd);
}

static void test_iso_and_chd(void) {
    cd_t cd;
    cd_init(&cd, NULL, NULL);

    /* ---- ISO ---- */
    CHK(cd_load_image(&cd, "nao_existe.iso") == -1, "iso inexistente: -1");
    CHK(cd_load_image(&cd, ISO_NAME) == 0 && cd.num_sectors == NSEC && cd.iso_ssize == 2048 && cd.iso_soff == 0, "iso cooked: 2048 B/setor, dados no byte 0");
    CHK(cd.image == NULL, "iso: nao carrega a imagem inteira na RAM");
    CHK(sector_equals(&cd, 0) && sector_equals(&cd, 17) && sector_equals(&cd, NSEC - 1), "iso cooked: setores 0, 17 e o ultimo");
    { uint8_t b[2048]; CHK(!cd_read_sector(&cd, NSEC, b), "iso: setor alem do fim falha"); }

    CHK(make_raw_iso(25) == 0, "gera iso bruta");
    CHK(cd_load_image(&cd, RAW_NAME) == 0 && cd.num_sectors == 25 && cd.iso_ssize == 2352 && cd.iso_soff == 16, "iso bruta (2352): dados a partir do byte 16");
    CHK(sector_equals(&cd, 0) && sector_equals(&cd, 9) && sector_equals(&cd, 24), "iso bruta: setores certos (sem o sync/EDC)");

    /* 2352 B/setor SEM sync (cabecalho zerado) e tamanho que nao e multiplo de 2048: regra do retro_cdimage_open_iso do Opera => bruta, dados no byte 16 */
    CHK(make_raw_iso_ex(25, 0) == 0 && cd_load_image(&cd, RAW_NAME) == 0 && cd.num_sectors == 25 && cd.iso_ssize == 2352 && cd.iso_soff == 16, "iso bruta sem sync (tamanho multiplo so de 2352): dados no byte 16");
    CHK(sector_equals(&cd, 0) && sector_equals(&cd, 24), "iso bruta sem sync: setores certos");

    /* 2352 B/setor com N multiplo de 128: o tamanho tambem e multiplo de 2048; o sync decide */
    CHK(make_raw_iso(128) == 0 && cd_load_image(&cd, RAW_NAME) == 0 && cd.num_sectors == 128 && cd.iso_ssize == 2352, "iso bruta ambigua (tamanho multiplo de 2048 e 2352): sync => bruta");
    CHK(sector_equals(&cd, 127), "iso bruta ambigua: ultimo setor");

    CHK(cd_load_image(&cd, "arquivo.bin") == -1 && cd_load_image(&cd, "arquivo.cue") == -1, "so aceita .iso e .chd (bin/cue recusados)");

#ifndef _WIN32
    /* ISO acima de 2 GiB (arquivo esparso): o seek de 64 bits do ph_file le o ultimo setor; com `long` de 32 bits (Windows) estourava */
    {   const uint32_t big = 1048577u;                        /* 1048577 * 2048 = 2 GiB + 2048 */
        FILE *f = fopen("test_cd_tmp_big.iso", "wb");
        if (f) { uint8_t pat[2048]; memset(pat, 0xAB, sizeof pat); fseeko(f, (off_t)(big - 1u) * 2048, SEEK_SET); fwrite(pat, 1, sizeof pat, f); fclose(f); }
        { uint8_t sb[2048]; int ok = f && cd_load_image(&cd, "test_cd_tmp_big.iso") == 0 && cd.num_sectors == big && cd_read_sector(&cd, big - 1u, sb) && sb[0] == 0xAB && sb[2047] == 0xAB;
          CHK(ok, "iso acima de 2 GiB: ultimo setor lido (seek de 64 bits)"); }
        remove("test_cd_tmp_big.iso");
    }
#endif

    /* ---- .cue (lido pelo ph_file: BOM, CRLF, nome com/sem aspas; faixas seguintes ignoradas) ---- */
    {   const char *cues[3] = {
            "\xEF\xBB\xBF" "REM teste\r\nFILE \"" ISO_NAME "\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\nFILE \"outra.bin\" BINARY\r\n",
            "  file " ISO_NAME " BINARY\n TRACK 01 MODE1/2048\n",
            "REM sem FILE\r\nTRACK 01 MODE1/2048\r\n" };
        for (int k = 0; k < 3; k++) {
            FILE *f = fopen("test_cd_tmp.cue", "wb"); fputs(cues[k], f); fclose(f);
            if (k < 2) CHK(cd_load_image(&cd, "test_cd_tmp.cue") == 0 && cd.num_sectors == NSEC && sector_equals(&cd, 5) && sector_equals(&cd, NSEC - 1),
                           k == 0 ? "cue com BOM+CRLF+aspas: abre o 1o FILE (e ignora o 2o)" : "cue sem aspas, minusculas, so LF: abre o FILE");
            else       CHK(cd_load_image(&cd, "test_cd_tmp.cue") == -1, "cue sem linha FILE: -1");
        }
        remove("test_cd_tmp.cue");
        CHK(cd_load_image(&cd, "test_cd_tmp.cue") == -1, "cue inexistente: -1");
    }
    CHK(cd_load_image(&cd, RAW_NAME) == 0, "recarrega a iso bruta depois de recusas (a imagem anterior nao e perdida em falha)");

    /* ---- CHD ---- */
#ifndef PH_NO_CHD
    CHK(cd_load_image(&cd, "nao_existe.chd") == -1, "chd inexistente: -1");
    { FILE *f = fopen(CHD_NAME, "wb"); fputs("isto nao e um CHD", f); fclose(f); }
    CHK(cd_load_image(&cd, CHD_NAME) == -1, "arquivo .chd invalido: -1");
    CHK(cd.iso_fp != NULL && cd.num_sectors == 128, "falha no .chd nao derruba a imagem que ja estava carregada");

    CHK(make_chd(CHD_NAME, "MODE1_RAW", 2352, 30, 1, 0) == 0, "gera CHD MODE1_RAW 2352");
    CHK(cd_load_image(&cd, CHD_NAME) == 0 && cd.chd && cd.iso_fp == NULL && cd.num_sectors == 30, "chd MODE1_RAW: carregado (30 setores), troca a ISO anterior");
    CHK(sector_equals(&cd, 0) && sector_equals(&cd, 3) && sector_equals(&cd, 4) && sector_equals(&cd, 29), "chd MODE1_RAW: setores 0, 3, 4 (outro hunk) e 29 integros (offset 16 do quadro)");
    CHK(sector_equals(&cd, 11) && sector_equals(&cd, 2) && sector_equals(&cd, 20), "chd: leitura fora de ordem (hunks fora de sequencia)");
    { uint8_t b[2048]; CHK(!cd_read_sector(&cd, 30, b), "chd: setor alem do fim falha"); }

    CHK(make_chd(CHD_NAME, "MODE1", 2448, 21, 1, 0) == 0, "gera CHD MODE1 2448");
    CHK(cd_load_image(&cd, CHD_NAME) == 0 && cd.num_sectors == 21, "chd MODE1 (quadro 2448, dados no byte 0): carregado");
    CHK(sector_equals(&cd, 0) && sector_equals(&cd, 5) && sector_equals(&cd, 20), "chd MODE1 2448: setores integros");

    CHK(make_chd(CHD_NAME, "MODE1_RAW", 2352, 16, 0, 8) == 0, "gera CHD com faixa de dados + faixa de audio");
    CHK(cd_load_image(&cd, CHD_NAME) == 0 && cd.num_sectors == 16, "chd com audio: usa so a faixa 1 (16 setores)");
    CHK(sector_equals(&cd, 0) && sector_equals(&cd, 15), "chd com audio: dados da faixa 1 integros");

    /* mesmo disco, ISO x CHD: o drive entrega os mesmos bytes */
    CHK(cd_load_image(&cd, ISO_NAME) == 0 && sector_equals(&cd, 7), "volta para a ISO cooked");
#else
    CHK(cd_load_image(&cd, "x.chd") == -1, "PH_NO_CHD: .chd recusado");
#endif
    cd_free(&cd);
    CHK(cd.iso_fp == NULL && cd.chd == NULL && cd.num_sectors == 0, "cd_free libera tudo");
    remove(RAW_NAME); remove(CHD_NAME);
}

/* um CHD inserido numa maquina: o drive do XBUS entrega os mesmos setores pelo caminho normal (cmd 0x10 + DMA/FIFO) */
static void test_chd_machine(void) {
#ifndef PH_NO_CHD
    ph_machine_t mm;
    if (make_chd(CHD_NAME, "MODE1_RAW", 2352, NSEC, 1, 0) != 0) { CHK(0, "gera CHD para a maquina"); return; }
    if (ph_init(&mm)) { CHK(0, "ph_init"); return; }
    CHK(ph_cd_insert(&mm, CHD_NAME) == 0 && mm.cd.num_sectors == NSEC, "ph_cd_insert(.chd)");
    cd_set_speed(&mm.cd, 8);
    mm.cd.R = 0xe1;
    /* leitura direta pela API do drive: posiciona e puxa 3 setores pelo FIFO de dados */
    mm.cd.lba = 5; mm.cd.remain = 3;
    int good = 1;
    for (uint32_t s = 5; s < 8 && good; s++) {
        cd_tick(&mm.cd, 1u << 20);
        for (uint32_t i = 0; i < CD_SECTOR_SIZE; i++)
            if ((uint8_t)cd_data_read(&mm.cd) != iso_byte(s, i)) { good = 0; break; }
    }
    CHK(good, "drive entrega os setores 5..7 do CHD pelo DataFIFO");
    ph_free(&mm);
    remove(CHD_NAME);
#endif
}

int main(void) {
    if (make_iso()) { printf("nao consegui criar %s\n", ISO_NAME); return 2; }
    if (ph_init(&m)) return 2;

    /* 1) sem disco: a unidade existe no XBUS ID 0 e responde ao poll */
    CHK(m.clio.dev[0] == &m.cd_dev, "clio.dev[0] aponta para o drive");
    xbus_enable();
    CHK(xpeek(0x540) == 0x0f, "reset: poll (+0x227) = 0x0f");
    CHK(xpeek(0x500) == 0xe1, "reset: DriveStatus (+0x228) = 0xe1");

    /* 2) inserir a imagem */
    CHK(ph_cd_insert(&m, ISO_NAME) == 0, "ph_cd_insert");
    CHK(m.cd.num_sectors == NSEC, "numero de setores = tamanho/2048");
    cd_set_speed(&m.cd, 8);                          /* sem limite de velocidade: um tick entrega o setor */
    xbus_enable();
    CHK(xpeek(0x540) == 0x80 && xpeek(0x500) == 0xe1, "apos insert: poll 0x80 (POLRE) / R 0xe1");
    xpoke(0x540, 0x8f);                              /* BIOS limpa o POLRE (v&0x80) e rearma os enables */
    CHK(xpeek(0x540) == 0x0f, "escrever bit 7 limpa o POLRE: poll 0x0f");

    uint8_t st[64]; int n;

    /* 3) comandos de status */
    send_cmd(0x80, 0, 0, 0, 0, 0, 0);
    n = rd_status(st, 64);
    CHK(n == 4 && st[0] == 0x80 && st[1] == 0xaa && st[2] == 0x55 && (st[3] & 1), "cmd 0x80: 80 aa 55 R");
    CHK(!(xpeek(0x540) & CD_S_CMD_RDY), "StatusFIFO vazio apaga CMD_RDY");

    send_cmd(0x83, 0, 0, 0, 0, 0, 0);
    n = rd_status(st, 64);
    CHK(n == 12 && st[0] == 0x83 && st[1] == 0x00 && st[2] == 0x10 && st[3] == 0x00 && st[4] == 0x01, "cmd 0x83: identificacao");

    send_cmd(0x8b, 0, 0, 0, 0, 0, 0);
    n = rd_status(st, 64);
    CHK(n == 8 && st[0] == 0x8b && (st[7] & 1), "cmd 0x8b: status + posicao (8 bytes, R com bit 0)");

    /* 4) TOC: faixa 1 e lead-out */
    send_cmd(0x8c, 0, 1, 0, 0, 0, 0);
    n = rd_status(st, 64);
    CHK(n == 10 && st[0] == 0x8c && st[2] == 0x04 && st[3] == 0x01 && st[5] == 0 && st[6] == 2 && st[7] == 0, "TOC faixa 1: control 4, no 1, MSF 00:02:00");
    send_cmd(0x8c, 0, CD_TOC_LEADOUT, 0, 0, 0, 0);
    n = rd_status(st, 64);
    {   uint8_t mm, ss, ff; lba_to_msf(NSEC, &mm, &ss, &ff);
        CHK(n == 10 && st[3] == 0xaa && st[5] == mm && st[6] == ss && st[7] == ff, "TOC lead-out = fim do disco (MSF = setores+150)"); }
    send_cmd(0x8c, 0, 0x65, 0, 0, 0, 0);
    n = rd_status(st, 64);
    CHK(n == 2 && st[0] == 0x8c && (st[1] & 0x10), "TOC faixa >= 0x65: recusa (eco + R com bit 0x10)");

    /* 5) leitura de 3 setores seguidos pelo registrador de dados (sem DMA) */
    send_read(2, 3);
    CHK(!(xpeek(0x540) & CD_S_CMD_RDY), "cmd 0x10: CMD_RDY so acende no primeiro setor");
    CHK(!(xpeek(0x540) & CD_S_DATA_RDY), "antes do tick nao ha dado");
    clio_tick(&m.clio, 4);
    CHK(xpeek(0x540) & CD_S_DATA_RDY, "apos o tick: DATA_RDY");
    CHK(xpeek(0x540) & CD_S_CMD_RDY, "primeiro setor acende CMD_RDY (fim da busca)");
    n = rd_status(st, 64);
    CHK(n == 2 && st[0] == 0x10 && (st[1] & 1), "cmd 0x10: eco do opcode + R (lido depois do 1o setor)");
    CHK(read_sector_ok(2), "setor 2 integro");
    CHK(read_sector_ok(3), "setor 3 integro (continua sem novo comando)");
    CHK(read_sector_ok(4), "setor 4 integro");
    CHK(!(xpeek(0x540) & CD_S_DATA_RDY), "fim da contagem: DATA_RDY apagado");
    CHK(m.cd.lba == 5, "posicao atual avancou para o setor 5");

    /* 6) leitura por DMA do XBUS para a RAM (FUN_0048f690): setor 7 -> 0x100000 */
    send_read(7, 1);
    rd_status(st, 64);
    clio_tick(&m.clio, 4);
    ph_write32(&m, 0x03300540u, 0x100000u);          /* MADAM +0x540: endereco */
    ph_write32(&m, 0x03300544u, CD_SECTOR_SIZE - 4); /* MADAM +0x544: bytes-4 */
    m.clio.R[0x400 >> 2] = 0x800 | 0x80; m.clio.R[0x404 >> 2] = 0x200; m.clio.R[0x304 >> 2] = 0x100000;
    m.clio.R[0x40 >> 2] = 0;
    clio_tick(&m.clio, 1);
    int ok = 1;
    for (uint32_t i = 0; i < CD_SECTOR_SIZE; i++) if (ph_read8(&m, 0x100000u + i) != iso_byte(7, i)) { ok = 0; break; }
    CHK(ok, "DMA: setor 7 copiado integro para a RAM (big-endian por palavra)");
    CHK(m.clio.R[0x40 >> 2] & 0x20000000u, "DMA: IRQ0 bit 29 (fim do DMA do XBUS)");
    CHK(!(m.clio.R[0x304 >> 2] & 0x100000), "DMA: canal desarmado");

    /* 7) leitura fora da imagem: sem dado */
    send_read(NSEC + 10, 1);
    rd_status(st, 64);
    clio_tick(&m.clio, 4);
    CHK(!(xpeek(0x540) & CD_S_DATA_RDY), "leitura alem do fim: sem DATA_RDY");

    /* 8) comando desconhecido nao deixa nada no StatusFIFO */
    send_cmd(0x7f, 0, 0, 0, 0, 0, 0);
    CHK(rd_status(st, 64) == 0, "opcode desconhecido: nada no StatusFIFO");

    /* 9) ejetar: a unidade continua no XBUS, sem imagem */
    ph_cd_eject(&m);
    xbus_enable();
    CHK(m.cd.image == NULL && m.cd.iso_fp == NULL && m.cd.chd == NULL && m.cd.num_sectors == 0, "eject: imagem liberada");
    send_read(2, 1);
    rd_status(st, 64);
    clio_tick(&m.clio, 4);
    CHK(!(xpeek(0x540) & CD_S_DATA_RDY), "sem imagem: leitura nao entrega dado");

    ph_free(&m);
    test_iso_and_chd();
    test_chd_machine();
    test_opera_extra_cmds();
    remove(ISO_NAME);
    printf("test_cd: %d verificacoes, %d falhas\n", total, fails);
    return fails ? 1 : 0;
}
