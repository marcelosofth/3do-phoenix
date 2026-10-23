#ifndef PHOENIX_CD_CHD_H
#define PHOENIX_CD_CHD_H

/* Leitura de imagens .chd para o CD do Phoenix.
 *
 * Mesma abordagem do retro_cdimage.c do opera-libretro: o decodificador e o rchd (libretro-common/formats/chd/rchd.c,
 * copiado do opera-libretro para libretro-common/), que nao faz E/S propria -- cd_chd.c atende cada pedido
 * (offset, tamanho) lendo o arquivo. Nada e descompactado de antemao: cada setor e decodificado quando o drive
 * o pede (o rchd guarda 1 hunk em cache, entao leituras sequenciais nao pagam a descompressao de novo).
 *
 * Cobre: CHD v1-v5 de CD-ROM, codecs zlib/lzma/flac/huff/zstd e os CD (cdzl/cdlz/cdfl/cdzs); discos de dados
 * Mode 1 (2048 / 2352 / 2448 bytes por quadro) e Mode 2 (XA). Usa a faixa 1; faixas extras (audio) sao ignoradas.
 * Nao cobre: CHD com arquivo "pai" (parent/diff). Compilar com -DPH_NO_CHD tira o suporte (cd_load_chd devolve -1). */

#include "cd.h"

struct cd_chd;                                   /* opaco: cd_chd.c */

int  cd_load_chd(cd_t *cd, const char *path);    /* 0 = ok, -1 = erro (mensagem em stderr); preenche cd->chd e num_sectors */
int  cd_chd_read(cd_t *cd, uint32_t lba, uint8_t *out);   /* 2048 bytes de dados do setor; 1 = ok */
void cd_chd_close(cd_t *cd);                     /* libera cd->chd (aceita NULL) */

#endif
