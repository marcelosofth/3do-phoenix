/* phaudit.c - runner headless de AUDITORIA em lote do nucleo Phoenix.
 *
 *   phaudit [opcoes] <BIOS> <imagem|pasta> [mais imagens/pastas...]
 *     -n <quadros>   quadros por jogo (padrao 1800 = 30 s de jogo)
 *     -o <pasta>     saida: auditoria.csv, <jogo>.txt e <jogo>.bmp (padrao ./auditoria)
 *     -s             aperta START por 6 quadros a cada 3 s (passa de telas de titulo)
 *     -a             navega menus: a cada 3 s aperta ESQUERDA, depois A e depois C (passa de "Play Anyway / Make Room")
 *     -f <BIOS2>     (reservado) 2o banco/font ROM: nao usado ainda
 *
 * Para cada .iso/.img/.bin/.cue/.chd: boot a frio com a BIOS, roda N quadros, grava captura da tela (.bmp), um relatorio
 * (.txt, contadores de TODOS os blocos do nucleo) e uma linha no CSV com as FLAGS de problema. Uma unica execucao cobre a
 * biblioteca inteira: em vez de testar jogo a jogo, ordene o CSV por FLAGS e ataque cada causa uma vez.
 * Codigo de saida: 0. Windows (MinGW) e Linux. */
#include "machine.h"
#include "ph_audit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0777)
#endif

static retro_input_state_t dummy_state;
static unsigned g_frame_no, g_start_pad, g_menu_pad;
static int16_t pad_state(unsigned port, unsigned dev, unsigned idx, unsigned id) {
    (void)idx;
    if (port == 0 && dev == RETRO_DEVICE_JOYPAD && id == RETRO_DEVICE_ID_JOYPAD_START && g_start_pad) return (g_frame_no % 180) < 6;
    if (port == 0 && dev == RETRO_DEVICE_JOYPAD && g_menu_pad) {
        unsigned t = g_frame_no % 180;
        if (id == RETRO_DEVICE_ID_JOYPAD_LEFT) return t < 6;                       /* seleciona o botao da esquerda */
        if (id == RETRO_DEVICE_ID_JOYPAD_Y)    return t >= 30 && t < 36;           /* 3DO A */
        if (id == RETRO_DEVICE_ID_JOYPAD_A)    return t >= 60 && t < 66;           /* 3DO C */
    }
    return 0;
}
static void pad_poll(void) {}
static unsigned g_frames_closed;
static void on_frame(void *ud) { ph_machine_t *m = ud; vdlp_frame_end(&m->vdlp); g_frames_closed++; }

static uint8_t *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 0x400000) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)sz); if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
    fclose(f); *n = (size_t)sz; return b;
}
static void write_bmp(const char *path, const vdlp_t *v) {
    FILE *f = fopen(path, "wb"); if (!f) return;
    int w = v->width, h = v->lines; uint32_t row = ((uint32_t)w * 3 + 3) & ~3u, sz = 54 + row * (uint32_t)h;
    uint8_t hd[54] = { 'B', 'M' }; memcpy(hd + 2, &sz, 4); hd[10] = 54; hd[14] = 40; memcpy(hd + 18, &w, 4); memcpy(hd + 22, &h, 4); hd[26] = 1; hd[28] = 24;
    uint32_t im = row * (uint32_t)h; memcpy(hd + 34, &im, 4);
    fwrite(hd, 1, 54, f);
    uint8_t *line = calloc(1, row);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) { const uint8_t *p = v->rgb + ((size_t)y * (size_t)w + (size_t)x) * 3; line[x * 3] = p[2]; line[x * 3 + 1] = p[1]; line[x * 3 + 2] = p[0]; }
        fwrite(line, 1, row, f);
    }
    free(line); fclose(f);
}
static const char *base_of(const char *p) { const char *s = strrchr(p, '/'), *b = strrchr(p, '\\'); if (b && (!s || b > s)) s = b; return s ? s + 1 : p; }
static int is_image(const char *n) {
    const char *e = strrchr(n, '.'); if (!e) return 0;
    return !strcasecmp(e, ".iso") || !strcasecmp(e, ".chd") || !strcasecmp(e, ".cue") || !strcasecmp(e, ".bin") || !strcasecmp(e, ".img");
}

static const uint8_t *g_bios; static size_t g_bios_n;
static int g_frames = 1800, g_press, g_menu;
static const char *g_out = "auditoria";
static FILE *g_csv;

static void audit_one(const char *img) {
    static ph_machine_t M; static uint64_t pcsum;
    char name[512], p[1200]; snprintf(name, sizeof name, "%s", base_of(img));
    for (char *c = name; *c; c++) if (*c == '/' || *c == '\\' || *c == ':' || *c == ' ') *c = '_';
    printf("== %s ... ", img); fflush(stdout);
    if (ph_init(&M) != 0) { puts("falha ao alocar"); return; }
    ph_load_bios(&M, g_bios, g_bios_n);
    if (ph_cd_insert(&M, img) != 0) { puts("imagem ilegivel/formato nao suportado"); fprintf(g_csv, "\"%s\",0,\"IMAGE_LOAD_FAILED\"\n", img); ph_free(&M); return; }
    M.clio.R[0x28 >> 2] = 1;
    if (M.cpu.r[15] == 0) { M.cpu.r[15] = 0x03000000u; M.vram_flag = 1; }
    M.clio.vdlp_line = on_frame;
    g_start_pad = (unsigned)g_press; g_menu_pad = (unsigned)g_menu; dummy_state = pad_state;
    joybus_set_callbacks(&M.joybus, pad_poll, pad_state);
    ph_audit_run_t R; memset(&R, 0, sizeof R); R.pc_lo = 0xffffffffu;
    static uint8_t prev[384 * 288 * 3]; size_t rgbn = (size_t)M.vdlp.width * M.vdlp.lines * 3; memset(prev, 0, sizeof prev);
    uint32_t pcmin = 0xffffffffu, pcmax = 0; g_frames_closed = 0; g_frame_no = 0;
    const uint32_t frame_cyc = 263 * 792, cap = frame_cyc * 22 / 10;
    for (int fr = 0; fr < g_frames; fr++) {
        g_frame_no = (unsigned)fr; joybus_poll(&M.joybus);
        if (fr == 90) { ph_audit_reset_boot(&M); celexec_reset_window(&M.cel); }   /* diagnostico do rasterizador: desde o quadro 90 */
        uint32_t spent = 0; unsigned before = g_frames_closed;
        while (g_frames_closed == before && spent < cap) { uint32_t t0 = M.total_cycles; ph_run(&M, 1024); spent += M.total_cycles - t0; if (M.cpu.r[15] < pcmin) pcmin = M.cpu.r[15]; if (M.cpu.r[15] > pcmax) pcmax = M.cpu.r[15]; }
        if (fr % 300 == 299) { R.pc_lo = pcmin; R.pc_hi = pcmax; pcmin = 0xffffffffu; pcmax = 0; }   /* janela de ~5 s */
        if (fr % 30 == 29) { if (memcmp(prev, M.vdlp.rgb, rgbn)) { R.last_change_frame = (unsigned)fr; memcpy(prev, M.vdlp.rgb, rgbn); } }
    }
    (void)pcsum;
    R.frames = g_frames_closed; if (R.pc_hi == 0) { R.pc_lo = pcmin; R.pc_hi = pcmax; }
    ph_audit_finish(&M, &R);
    char shot[1300]; snprintf(shot, sizeof shot, "%s/%s.bmp", g_out, name); write_bmp(shot, &M.vdlp);
    snprintf(p, sizeof p, "%s/%s.txt", g_out, name);
    FILE *t = fopen(p, "w"); if (t) { fprintf(t, "%s\n", img); ph_audit_report(&M, &R, t); fclose(t); }
    ph_audit_csv_row(&M, &R, img, shot, g_csv); fflush(g_csv);
    printf("%s\n", R.flags);
    ph_free(&M);
}
static void walk(const char *path, int depth) {
    struct stat st; if (stat(path, &st) != 0) { fprintf(stderr, "nao encontrei %s\n", path); return; }
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path); if (!d || depth > 4) return;
        struct dirent *e; char sub[1200];
        while ((e = readdir(d))) { if (e->d_name[0] == '.') continue; snprintf(sub, sizeof sub, "%s/%s", path, e->d_name); walk(sub, depth + 1); }
        closedir(d);
    } else if (is_image(path)) {
        /* .bin ao lado de um .cue do mesmo nome e coberto pelo .cue: evita auditar duas vezes */
        const char *e = strrchr(path, '.');
        if (e && !strcasecmp(e, ".bin")) { char c[1200]; snprintf(c, sizeof c, "%.*s.cue", (int)(e - path), path); struct stat s2; if (stat(c, &s2) == 0) return; }
        audit_one(path);
    }
}
int main(int argc, char **argv) {
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc) g_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) g_out = argv[++i];
        else if (!strcmp(argv[i], "-s")) g_press = 1;
        else if (!strcmp(argv[i], "-a")) g_menu = 1;
        else if (!strcmp(argv[i], "-f") && i + 1 < argc) i++;
        else { fprintf(stderr, "opcao desconhecida: %s\n", argv[i]); return 2; }
    }
    if (argc - i < 2) { fprintf(stderr, "uso: phaudit [-n quadros] [-o pasta] [-s] [-a] <BIOS> <imagem|pasta>...\n"); return 2; }
    g_bios = slurp(argv[i], &g_bios_n);
    if (!g_bios || (g_bios_n != 0x100000 && g_bios_n != 0x200000)) { fprintf(stderr, "BIOS invalida: %s\n", argv[i]); return 2; }
    MKDIR(g_out);
    char cp[1200]; snprintf(cp, sizeof cp, "%s/auditoria.csv", g_out);
    g_csv = fopen(cp, "w"); if (!g_csv) { fprintf(stderr, "nao consegui criar %s\n", cp); return 2; }
    ph_audit_csv_header(g_csv);
    for (i++; i < argc; i++) walk(argv[i], 0);
    fclose(g_csv);
    printf("\nCSV: %s\n", cp);
    return 0;
}
