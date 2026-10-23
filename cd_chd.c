/* cd_chd.c - leitor de imagens .chd para o Phoenix 3DO. Ver cd_chd.h.
 *
 * Segue retro_cdimage.c do opera-libretro (retro_cdimage_open_chd / cdimage_chd_read_sector):
 *   - abre o decodificador rchd e atende cada RCHD_PENDING lendo o arquivo (pedidos de ate 64 KB);
 *   - para a faixa 1 calcula onde comecam os dados no fluxo logico do CHD (pregap, tamanho do quadro);
 *   - o setor LBA e lido como 2048 bytes em  origem_da_faixa + indice * unit_bytes + offset_dos_dados,
 *     com offset 16 (MODE1_RAW), 24 (MODE2_RAW) ou 0 (demais); pregap/postgap leem como zeros.
 */
#include "cd_chd.h"

#ifdef PH_NO_CHD

int  cd_load_chd(cd_t *cd, const char *path) { (void)cd; fprintf(stderr, "[Phoenix][CHD] suporte a .chd desligado neste build (PH_NO_CHD): %s\n", path); return -1; }
int  cd_chd_read(cd_t *cd, uint32_t lba, uint8_t *out) { (void)cd; (void)lba; (void)out; return 0; }
void cd_chd_close(cd_t *cd) { (void)cd; }

#else

#include <formats/rchd.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define CHD_REQ_BUF 65536u

struct cd_chd {
    FILE     *f;
    rchd_t   *dec;
    uint8_t  *reqbuf;
    uint32_t  unit_bytes;     /* bytes por quadro no fluxo logico (2352, 2448, ...) */
    uint64_t  source_offset;  /* offset logico do 1o quadro de dados da faixa 1 */
    uint32_t  span_start;     /* primeiro LBA da faixa (inclui o pregap) */
    uint32_t  start_lba;      /* LBA do 1o quadro de dados */
    uint32_t  frames;         /* quadros de dados */
    uint32_t  span_end;       /* ultimo LBA + 1 (dados + postgap) */
    uint32_t  data_offset;    /* inicio dos 2048 bytes dentro do quadro: 0, 16 ou 24 */
};

static void chd_free(struct cd_chd *c) {
    if (!c) return;
    if (c->f) fclose(c->f);
    rchd_free(c->dec);
    free(c->reqbuf);
    free(c);
}

/* Atende um pedido do decodificador com bytes do arquivo (leitura curta e legal: o rchd pede o resto). */
static int chd_feed(struct cd_chd *c, const rchd_request_t *req) {
    size_t want, got;
    if (req->source != RCHD_SOURCE_SELF) {
        fprintf(stderr, "[Phoenix][CHD] imagem usa arquivo \"pai\" (parent/diff), nao suportado\n");
        return 0;
    }
    if (req->offset > (uint64_t)LONG_MAX || fseek(c->f, (long)req->offset, SEEK_SET) != 0) {
        fprintf(stderr, "[Phoenix][CHD] seek falhou no offset %llu\n", (unsigned long long)req->offset);
        return 0;
    }
    want = req->length < CHD_REQ_BUF ? req->length : CHD_REQ_BUF;
    got = fread(c->reqbuf, 1, want, c->f);
    if (got == 0) {
        fprintf(stderr, "[Phoenix][CHD] leitura falhou no offset %llu (%u bytes pedidos)\n",
                (unsigned long long)req->offset, (unsigned)req->length);
        return 0;
    }
    return rchd_feed(c->dec, c->reqbuf, got) == RCHD_OK;
}

static int chd_open_decoder(struct cd_chd *c) {
    rchd_request_t req;
    for (;;) {
        int st = rchd_open_step(c->dec, &req);
        if (st == RCHD_OK) return 1;
        if (st != RCHD_PENDING) { fprintf(stderr, "[Phoenix][CHD] cabecalho/mapa invalido (codigo %d)\n", st); return 0; }
        if (!chd_feed(c, &req)) return 0;
    }
}

static int chd_read_bytes(struct cd_chd *c, uint64_t off, void *dst, size_t len) {
    rchd_request_t req;
    if (rchd_read_begin(c->dec, off, dst, len) != RCHD_OK) return 0;
    for (;;) {
        int st = rchd_read_step(c->dec, &req);
        if (st == RCHD_OK) return 1;
        if (st != RCHD_PENDING) return 0;
        if (!chd_feed(c, &req)) return 0;
    }
}

int cd_load_chd(cd_t *cd, const char *path) {
    struct cd_chd *c;
    const rchd_info_t *info;
    const rchd_track_t *t;
    uint32_t ntracks, skipped, data_frames;
    uint64_t skipped_bytes, src, src_bytes, span_end;

    c = (struct cd_chd *)calloc(1, sizeof *c);
    if (!c) return -1;
    c->f = fopen(path, "rb");
    if (!c->f) { fprintf(stderr, "[Phoenix][CHD] nao consegui abrir %s\n", path); chd_free(c); return -1; }
    c->dec = rchd_new();
    c->reqbuf = (uint8_t *)malloc(CHD_REQ_BUF);
    if (!c->dec || !c->reqbuf) { fprintf(stderr, "[Phoenix][CHD] sem memoria\n"); chd_free(c); return -1; }
    if (!chd_open_decoder(c)) { chd_free(c); return -1; }

    info = rchd_info(c->dec);
    ntracks = rchd_track_count(c->dec);
    if (!info || info->unit_bytes == 0 || ntracks == 0) {
        fprintf(stderr, "[Phoenix][CHD] %s: sem tabela de faixas de CD (nao e um dump de CD-ROM?)\n", path);
        chd_free(c); return -1;
    }
    if (info->has_parent) { fprintf(stderr, "[Phoenix][CHD] %s: imagem com arquivo \"pai\" (parent/diff), nao suportada\n", path); chd_free(c); return -1; }
    if (ntracks > 1)
        fprintf(stderr, "[Phoenix][CHD] %s: %u faixas; usando so a faixa 1 (3DO = disco de dados unico; faixas de audio sao ignoradas)\n", path, (unsigned)ntracks);

    t = rchd_track(c->dec, 0);
    if (!t || t->data_size == 0) { fprintf(stderr, "[Phoenix][CHD] %s: faixa 1 invalida\n", path); chd_free(c); return -1; }
    if (t->type == RCHD_TRACK_AUDIO) { fprintf(stderr, "[Phoenix][CHD] %s: a faixa 1 e de audio, nao de dados\n", path); chd_free(c); return -1; }

    /* onde ficam os 2048 bytes dentro do quadro: igual a cdimage_chd_configure_track do Opera */
    if (t->type == RCHD_TRACK_MODE1_RAW)      c->data_offset = 16;
    else if (t->type == RCHD_TRACK_MODE2_RAW) c->data_offset = 24;
    else                                      c->data_offset = 0;
    if (c->data_offset == 0 && t->data_size < CD_SECTOR_SIZE) {
        fprintf(stderr, "[Phoenix][CHD] %s: setor da faixa 1 tem so %u bytes (esperava >= 2048)\n", path, (unsigned)t->data_size);
        chd_free(c); return -1;
    }

    /* pregap/dados/postgap: mesma conta do retro_cdimage_open_chd (faixa 1 comeca no LBA 0 do disco) */
    skipped     = (t->pregap_stored == 0) ? t->pregap : 0;
    data_frames = t->frames;
    if (t->pregap_stored == 0) data_frames = (t->frames > t->pregap) ? t->frames - t->pregap : 0;
    skipped_bytes = (uint64_t)skipped * info->unit_bytes;
    src = t->logical_offset + skipped_bytes;
    src_bytes = (uint64_t)data_frames * info->unit_bytes;
    span_end = (uint64_t)t->pregap + data_frames + t->postgap;
    if (data_frames == 0 || src > info->logical_bytes || src_bytes > info->logical_bytes - src || span_end > UINT32_MAX) {
        fprintf(stderr, "[Phoenix][CHD] %s: geometria da faixa 1 inconsistente\n", path);
        chd_free(c); return -1;
    }
    c->unit_bytes    = info->unit_bytes;
    c->source_offset = src;
    c->span_start    = 0;
    c->start_lba     = t->pregap;
    c->frames        = data_frames;
    c->span_end      = (uint32_t)span_end;

    /* prova de vida: o primeiro quadro de dados tem que decodificar de verdade */
    {   uint8_t probe[CD_SECTOR_SIZE];
        if (!chd_read_bytes(c, c->source_offset + c->data_offset, probe, sizeof probe)) {
            fprintf(stderr, "[Phoenix][CHD] %s: nao consegui decodificar o primeiro setor de dados\n", path);
            chd_free(c); return -1;
        }
    }

    cd_free(cd);                                  /* so troca a imagem atual depois de tudo validado */
    cd->chd = c;
    cd->num_sectors = c->span_end;
    cd->image_size = (size_t)c->span_end * CD_SECTOR_SIZE;
    return 0;
}

int cd_chd_read(cd_t *cd, uint32_t lba, uint8_t *out) {
    struct cd_chd *c = cd->chd;
    if (!c || lba < c->span_start || lba >= c->span_end) return 0;
    if (lba < c->start_lba || lba - c->start_lba >= c->frames) { memset(out, 0, CD_SECTOR_SIZE); return 1; }   /* pregap/postgap */
    return chd_read_bytes(c, c->source_offset + (uint64_t)(lba - c->start_lba) * c->unit_bytes + c->data_offset,
                          out, CD_SECTOR_SIZE);
}

void cd_chd_close(cd_t *cd) { chd_free(cd->chd); cd->chd = NULL; }

#endif
