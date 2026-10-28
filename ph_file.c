/* ph_file.c - ver ph_file.h */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "ph_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <wchar.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static const struct retro_vfs_interface *g_vfs;
static unsigned g_vfs_ver;

struct ph_file {
    struct retro_vfs_file_handle *vh;   /* VFS do frontend */
    FILE *fp;                           /* stdio */
};

void ph_file_init(retro_environment_t cb) {
    struct retro_vfs_interface_info vi;
    static const unsigned want[2] = { 3, 1 };   /* 3 traz mkdir; 1 basta para ler/gravar/renomear */
    g_vfs = NULL; g_vfs_ver = 0;
#if defined(_WIN32) && !defined(PH_USE_VFS)
    /* Windows: _wfopen com UTF-8->UTF-16 ja resolve os acentos; a VFS do RetroArch derrubava o frontend ao carregar jogo. */
    (void)cb; (void)vi; (void)want; return;
#endif
    if (!cb) return;
    for (int i = 0; i < 2; i++) {
        vi.required_interface_version = want[i];
        vi.iface = NULL;
        if (cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vi) && vi.iface && vi.iface->open && vi.iface->close
            && vi.iface->size && vi.iface->seek && vi.iface->read && vi.iface->write) {
            g_vfs = vi.iface; g_vfs_ver = vi.required_interface_version ? vi.required_interface_version : want[i];
            return;
        }
    }
}
int ph_file_using_vfs(void) { return g_vfs ? (int)g_vfs_ver : 0; }

#ifdef _WIN32
/* UTF-8 -> UTF-16; NULL se o texto nao for UTF-8 valido (ai o chamador usa o caminho ANSI, como antes) */
static wchar_t *u8w(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t *w;
    if (n <= 0) return NULL;
    w = (wchar_t *)malloc((size_t)n * sizeof *w);
    if (w && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) <= 0) { free(w); return NULL; }
    return w;
}
#endif

ph_file *ph_fopen(const char *path, const char *mode) {
    ph_file *f; int wr = mode && mode[0] == 'w';
    if (!path || !path[0]) return NULL;
    f = (ph_file *)calloc(1, sizeof *f);
    if (!f) return NULL;
    if (g_vfs) {
        f->vh = g_vfs->open(path, wr ? RETRO_VFS_FILE_ACCESS_WRITE : RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
        if (!f->vh) { free(f); return NULL; }
        return f;
    }
#ifdef _WIN32
    {
        wchar_t *wp = u8w(path);
        if (wp) { f->fp = _wfopen(wp, wr ? L"wb" : L"rb"); free(wp); }
        else    f->fp = fopen(path, wr ? "wb" : "rb");
    }
#else
    f->fp = fopen(path, wr ? "wb" : "rb");
#endif
    if (!f->fp) { free(f); return NULL; }
    return f;
}

void ph_fclose(ph_file *f) {
    if (!f) return;
    if (f->vh) g_vfs->close(f->vh);
    if (f->fp) fclose(f->fp);
    free(f);
}

int64_t ph_fsize(ph_file *f) {
    if (!f) return -1;
    if (f->vh) return g_vfs->size(f->vh);
    {
#ifdef _WIN32
        __int64 cur = _ftelli64(f->fp), sz;
        if (cur < 0 || _fseeki64(f->fp, 0, SEEK_END) != 0) return -1;
        sz = _ftelli64(f->fp);
        _fseeki64(f->fp, cur, SEEK_SET);
#else
        off_t cur = ftello(f->fp), sz;
        if (cur < 0 || fseeko(f->fp, 0, SEEK_END) != 0) return -1;
        sz = ftello(f->fp);
        fseeko(f->fp, cur, SEEK_SET);
#endif
        return (int64_t)sz;
    }
}

int ph_fseek(ph_file *f, int64_t off) {
    if (!f || off < 0) return -1;
    if (f->vh) return g_vfs->seek(f->vh, off, RETRO_VFS_SEEK_POSITION_START) < 0 ? -1 : 0;
#ifdef _WIN32
    return _fseeki64(f->fp, off, SEEK_SET) == 0 ? 0 : -1;
#else
    return fseeko(f->fp, (off_t)off, SEEK_SET) == 0 ? 0 : -1;
#endif
}

size_t ph_fread(void *dst, size_t n, ph_file *f) {
    if (!f || !n) return 0;
    if (f->vh) { int64_t r = g_vfs->read(f->vh, dst, (uint64_t)n); return r > 0 ? (size_t)r : 0; }
    return fread(dst, 1, n, f->fp);
}

size_t ph_fwrite(const void *src, size_t n, ph_file *f) {
    if (!f || !n) return 0;
    if (f->vh) { int64_t r = g_vfs->write(f->vh, src, (uint64_t)n); return r > 0 ? (size_t)r : 0; }
    return fwrite(src, 1, n, f->fp);
}

uint8_t *ph_read_all(const char *path, size_t max, size_t *n) {
    ph_file *f = ph_fopen(path, "rb");
    int64_t sz; uint8_t *b;
    if (!f) return NULL;
    sz = ph_fsize(f);
    if (sz <= 0 || (uint64_t)sz > (uint64_t)max) { ph_fclose(f); return NULL; }
    b = (uint8_t *)malloc((size_t)sz + 1);
    if (b && ph_fread(b, (size_t)sz, f) != (size_t)sz) { free(b); b = NULL; }
    ph_fclose(f);
    if (!b) return NULL;
    b[sz] = 0;
    if (n) *n = (size_t)sz;
    return b;
}

int ph_remove(const char *path) {
    if (!path) return -1;
    if (g_vfs && g_vfs->remove) return g_vfs->remove(path) == 0 ? 0 : -1;
#ifdef _WIN32
    { wchar_t *w = u8w(path); int r = w ? _wremove(w) : remove(path); free(w); return r == 0 ? 0 : -1; }
#else
    return remove(path) == 0 ? 0 : -1;
#endif
}

int ph_rename(const char *from, const char *to) {
    if (!from || !to) return -1;
    if (g_vfs && g_vfs->rename) return g_vfs->rename(from, to) == 0 ? 0 : -1;
#ifdef _WIN32
    { wchar_t *a = u8w(from), *b = u8w(to); int r = (a && b) ? _wrename(a, b) : rename(from, to); free(a); free(b); return r == 0 ? 0 : -1; }
#else
    return rename(from, to) == 0 ? 0 : -1;
#endif
}

int ph_mkdir(const char *dir) {
    if (!dir || !dir[0]) return -1;
    if (g_vfs && g_vfs_ver >= 3 && g_vfs->mkdir) { int r = g_vfs->mkdir(dir); return (r == 0 || r == -2) ? 0 : -1; }   /* -2 = ja existe */
    /* VFS v1/v2 nao tem mkdir: cai no stdio (a pasta de saves/system e do usuario, em desktop o mkdir direto funciona) */
#ifdef _WIN32
    { wchar_t *w = u8w(dir); int r = w ? _wmkdir(w) : _mkdir(dir); free(w); return (r == 0 || errno == EEXIST) ? 0 : -1; }
#else
    return (mkdir(dir, 0755) == 0 || errno == EEXIST) ? 0 : -1;
#endif
}
