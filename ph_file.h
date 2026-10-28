/* ph_file.h - acesso a arquivos do Phoenix: VFS do libretro quando o frontend oferece, stdio quando nao.
 *
 * Por que existe: no Windows o fopen() do C recebe o caminho na pagina de codigo ANSI, mas o libretro entrega os
 * caminhos em UTF-8 (game->path, system dir, save dir). Um caminho como "Jogos/Acao/..." com acento abre pelo VFS do
 * RetroArch e falha no fopen. O Opera resolve isso com a VFS (GET_VFS_INTERFACE) e fopen_utf8; aqui fica uma versao
 * enxuta (sem puxar file_stream.c/vfs_implementation.c do libretro-common):
 *   - com VFS: tudo passa pelo frontend (UTF-8, Android SAF, etc.);
 *   - sem VFS: stdio; no Windows converte UTF-8 -> UTF-16 e usa _wfopen/_wremove/_wrename/_wmkdir (cai no caminho
 *     ANSI so se o texto nao for UTF-8 valido); seek de 64 bits (_fseeki64/fseeko), sem o teto de 2 GB do `long`.
 * Todas as funcoes aceitam NULL onde faz sentido e nunca dependem de estado alem do ponteiro da VFS. */
#ifndef PH_FILE_H
#define PH_FILE_H

#include <stddef.h>
#include <stdint.h>
#include "libretro.h"

typedef struct ph_file ph_file;

/* Pede a VFS ao frontend (versao 3 e, se nao houver, 1). Chamar em retro_set_environment. cb == NULL volta ao stdio. */
void ph_file_init(retro_environment_t cb);
int  ph_file_using_vfs(void);                    /* 0 = stdio, senao a versao da VFS em uso */

/* mode: "rb" (leitura) ou "wb" (cria/zera e grava). Retorna NULL se nao abriu. */
ph_file *ph_fopen(const char *path, const char *mode);
void     ph_fclose(ph_file *f);
int64_t  ph_fsize(ph_file *f);                   /* tamanho em bytes, -1 em erro; nao mexe na posicao */
int      ph_fseek(ph_file *f, int64_t off);      /* a partir do inicio; 0 = ok, -1 = erro */
size_t   ph_fread(void *dst, size_t n, ph_file *f);        /* bytes lidos (curto = fim ou erro) */
size_t   ph_fwrite(const void *src, size_t n, ph_file *f); /* bytes gravados */

/* arquivo inteiro na memoria, com 1 byte NUL extra no fim (util para .cue). NULL se nao abre, esta vazio ou passa de max.
 * *n recebe o tamanho sem o NUL. Liberar com free(). */
uint8_t *ph_read_all(const char *path, size_t max, size_t *n);

int ph_remove(const char *path);                 /* 0 = ok */
int ph_rename(const char *from, const char *to); /* 0 = ok */
int ph_mkdir(const char *dir);                   /* 0 = criou ou ja existia */

#endif
