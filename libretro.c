/* libretro.c - front-end libretro do Phoenix 3DO (Parte 18).
 * Liga o nucleo (machine/arm60/clio/madam/sport/vdlp) ao RetroArch: retro_init, retro_load_game, retro_run,
 * retro_get_system_av_info, video, audio e NVRAM.
 * Video: o VDLP (vdlp.c, portado do .exe) vive dentro do ph_machine_t (g_m.vdlp) e entrega RGB24 por faixas de
 * linhas sujas (gancho blit); aqui cada faixa vai para g_src (nativo) e video_compose() monta a saida (bordas, fundo
 * expandido, escala interna, filtro, XRGB8888/RGB565) em g_fb a cada quadro. O fim de
 * quadro e detectado no gancho clio.vdlp_line (FUN_004788b0), porque o gancho present so dispara quando alguma
 * linha mudou.
 *
 * O que NAO existe no nucleo ainda (ver PLANO_PHOENIX.md): DSP (fase 5), controles
 * (joybus no CLIO) e o rasterizador CEL geral (fase 4). O front-end ja chama tudo que existe; quando
 * esses blocos entrarem, basta preencher os ganchos em machine_boot(). */
#include "libretro.h"
#include "machine.h"
#include "vdlp.h"
#include "ph_options.h"
#include "ph_swi_hle.h"
#include "ph_file.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define PH_MKDIR(p) _mkdir(p)
#else
#define PH_MKDIR(p) mkdir((p), 0755)
#endif

#define LR_MAX_W     VDLP_MAX_WIDTH     /* 384 (PAL); NTSC usa 320 */
#define LR_MAX_H     VDLP_MAX_LINES     /* 288 (PAL); NTSC usa 240 */
/* Saida de video (opcoes de imagem): canvas = imagem + bordas, alargado a 16:9 se "Expandir fundo"; saida = canvas * escala interna (1..4) */
#define LR_MAX_SCALE 4
#define LR_MAX_CW    568                        /* PAL 384x288 + bordas 5% = 422x316; 16:9 sobre 316 linhas = 562 colunas */
#define LR_MAX_CH    320
#define LR_XRGB8888  0
#define LR_RGB565    1
#define LR_0RGB1555  2
#define AUDIO_RATE   44100.0
#define NTSC_LINES   263
#define PAL_LINES    313
#define SAMPLES_MAX  4096

static retro_environment_t        env_cb;
static retro_video_refresh_t      video_cb;
static retro_audio_sample_t       audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t         input_poll_cb;
static retro_input_state_t        input_state_cb;
static retro_log_printf_t         log_cb;

static ph_machine_t g_m;
static int          g_have_machine;
static void        *g_fb;                       /* buffer de SAIDA entregue ao frontend: (LR_MAX_CW*LR_MAX_SCALE) * (LR_MAX_CH*LR_MAX_SCALE) * 4 bytes; passo = largura de saida * bytes/pixel */
static uint32_t     g_src[LR_MAX_W * LR_MAX_H]; /* imagem nativa do VDLP em 0x00RRGGBB (passo LR_MAX_W); o gancho blit escreve aqui */
static uint32_t     g_src2[LR_MAX_W * 2 * LR_MAX_H * 2]; /* idem em 2x (HiRes CEL Rendering; passo = 2*LR_MAX_W) */
static uint32_t     g_cv[LR_MAX_CW * LR_MAX_CH * 4];/* canvas: imagem + bordas + fundo expandido (passo = largura do canvas * k); k = 2 no HiRes */
static int          g_keep_aspect = 1;          /* opcao phoenix_keep_aspect  (Keep screen aspect ratio) */
static int          g_expand_bg;                /* opcao phoenix_expand_bg    (Expand background) */
static int          g_borders;                  /* opcao phoenix_borders      (Show borders) */
#ifndef PH_COVER_LASTLINE
#define PH_COVER_LASTLINE 1   /* 1 = cobre o tracinho SO na fase 'The Roof' (deteccao automatica); 0 = nunca cobre */
#endif
static int g_roof_active;   /* 1 enquanto o quadro atual foi reconhecido como a fase The Roof */
static int g_cover_last = PH_COVER_LASTLINE;   /* os testes pixel a pixel desligam; opcao phoenix_roof_fix=off tambem */
static int          g_filter;                   /* opcao phoenix_image_filter (Image filter): 0 = off, 1 = linear, 2 = shader std */
static int          g_scale = 1;                /* opcao phoenix_fbo_scale    (FBO Scale): 1..4 */
static int          g_frame_ready;              /* o VDLP fechou o quadro visivel (FUN_004788b0) */
static unsigned     g_frames;                   /* quadros fechados desde o boot */
static int          g_pal;                      /* 1 = qualquer modo PAL (g_region != 0) */
static int          g_hd;                       /* opcao phoenix_region = ntsc_hd (NTSC 1024x768@60, 4:3): maquina NTSC normal, saida em 3,2x (so no layout de video) */
static int          g_region;                   /* opcao phoenix_region (Region / Mode): 0 = NTSC 320x240@60, 1 = PAL2 384x288@50, 2 = PAL1 320x288@50 (= VDLP_MODE_*) */
static int          g_fmt = LR_XRGB8888;        /* opcao phoenix_pixel_format (VDLP Pixel Format) */
static char         g_bios_choice[80] = "auto"; /* opcao phoenix_bios (BIOS rom1): nome do arquivo; "auto" = procura como antes */
static char         g_font_choice[80] = "auto"; /* opcao phoenix_font (Font rom2): arquivo, "disabled", ou "auto" (procura como antes) */
static int          g_cd_speed = 2;             /* opcao phoenix_cd_speed: 0 = unbounded, senao 1/2/4/8/16/24/32 (2x = padrao de fabrica) */
static int          g_cd_opera_extra;           /* opcao phoenix_cd_opera_extra_cmds: 0 (padrao) = so os comandos do .exe; 1 = tambem 01/04/07/0A/0E/0F/86/88/89/8E do opera-libretro */
static double       g_overclock = 1.0;          /* opcao phoenix_cpu_overclock */
static uint32_t     g_seed;                     /* opcao phoenix_random_seed (semente fixa) */
static int          g_seed_random;              /* ... ou "random" (baseada no relogio) */
static int          g_matrix_sw;                /* opcao phoenix_madam_matrix_engine = software */
static int          g_swi_hle;                  /* opcao phoenix_swi_hle (OperaOS SWI HLE) */
static int          g_kprint;                   /* opcao phoenix_kprint (Debug Output) */
static int          g_bypass_clut;              /* opcao phoenix_vdlp_bypass_clut */
static int          g_fill;                     /* L2/R2 em jogo: 0 = Screen: Normal, 1 = Screen: Extended (recorta as barras pretas do jogo e enche o quadro) */
static int          g_fill_valid;               /* g_fill_b* ja tem uma caixa de conteudo valida */
static int          g_fill_bx0, g_fill_by0, g_fill_bx1, g_fill_by1;   /* caixa estavel do conteudo desenhado, em pixels da imagem (inclusiva) */
static int          g_fill_shrink;              /* quadros seguidos em que o conteudo coube numa caixa menor */
static unsigned     g_fill_key[6];              /* cw, ch, x0, y0, sw, sh do ultimo quadro: se mudar, a caixa e recalculada */
static int          g_hires;                    /* opcao phoenix_high_resolution (HiRes CEL Rendering): CELs 3D em 2x + saida do VDLP em 2x; vale na hora */
static char         g_mem_cap[8] = "21";        /* opcao phoenix_mem_capacity: hexa, DRAM MB (nibble alto) e VRAM MB (baixo); 21 = stock */
static int          g_active_devices = 1;       /* opcao phoenix_active_devices: maximo de controles emulados (0..8) */
static int          g_hide_xhair;               /* opcao phoenix_hide_lightgun_crosshairs: esconde a mira desenhada na tela (lightgun / arcade lightgun) */
static int          g_nvram_shared;             /* opcao phoenix_nvram_storage = shared */
static int          g_nvram_ver;                /* opcao phoenix_nvram_version (0..9): "slot" de save */
static int          g_debug;                    /* opcao phoenix_debug: diagnostico no log (padrao desligado) */
static int          g_cel_legacy;               /* opcao phoenix_cel_engine: 0 = motor de CEL do Opera (padrao), 1 = motor antigo (cel.c) para comparar A/B */
static char         g_system_dir[1024];
static char         g_disc_path[1024];
static int16_t      g_audio[SAMPLES_MAX * 2];
static unsigned     g_audio_n;
static int          g_nvram_checked;            /* 0 = ainda nao conferiu se a NVRAM esta formatada (rearmado em retro_load_game) */
static uint8_t      g_nvram_keep[PH_NVRAM_BYTES];

static void logf_(int lv, const char *fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (log_cb) log_cb(lv, "[Phoenix] %s", buf); else fprintf(stderr, "[Phoenix] %s", buf);
}

static void cd_trace_cb(const char *m)   { logf_(RETRO_LOG_INFO, "CD> %s\n", m); }
static void clio_trace_cb(const char *m) { logf_(RETRO_LOG_INFO, "CLIO> %s\n", m); }

/* ------------------------------------------------------------------ opcoes */
/* As opcoes (nomes, valores, textos, categorias) estao em ph_options.h, no formato v2 do Opera. */
static int opt_get(const char *key, const char **out) {
    struct retro_variable v; v.key = key; v.value = NULL;
    if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value) { *out = v.value; return 1; }
    return 0;
}
static int ci_eq(const char *a, const char *b) {          /* comparacao sem diferenciar maiusculas (valores antigos: "PAL", "rgb565") */
    for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return 0; }
    return *a == *b;
}
static int opt_on(const char *key, int def) {             /* "enabled"/"on" = 1, "disabled"/"off" = 0; sem a opcao: def */
    const char *v; if (!opt_get(key, &v)) return def;
    return ci_eq(v, "enabled") || ci_eq(v, "on");
}
static int parse_region(const char *v) { return ci_eq(v, "pal1") ? VDLP_MODE_PAL1 : (ci_eq(v, "pal2") || ci_eq(v, "pal")) ? VDLP_MODE_PAL2 : VDLP_MODE_NTSC; }
static int parse_pixfmt(const char *v) { return ci_eq(v, "rgb565") ? LR_RGB565 : ci_eq(v, "0rgb1555") ? LR_0RGB1555 : LR_XRGB8888; }
static int parse_cd_speed(const char *v) {
    int n; if (ci_eq(v, "unbounded")) return 0;
    n = atoi(v); return (n == 1 || n == 2 || n == 4 || n == 8 || n == 16 || n == 24 || n == 32) ? n : 2;
}
/* opcoes de imagem: podem mudar com o jogo rodando (retro_run chama de novo quando o frontend avisa) */
static void read_video_options(void) {
    const char *v;
    if (opt_get("phoenix_keep_aspect", &v))  g_keep_aspect = strcmp(v, "off") != 0;
    if (opt_get("phoenix_expand_bg", &v))    g_expand_bg = !strcmp(v, "on");
    if (opt_get("phoenix_borders", &v))      g_borders = !strcmp(v, "on");
    if (opt_get("phoenix_screen_size", &v)) {   /* Screen Size: normal / extended (o mesmo estado dos botoes L2/R2) */
        const int f = !strcmp(v, "extended");
        if (f != g_fill) { g_fill = f; g_fill_valid = 0; g_fill_shrink = 0; }
    }
    if (opt_get("phoenix_roof_fix", &v)) g_cover_last = strcmp(v, "off") != 0;   /* on (padrao) = cobre so na fase The Roof; chave nova: o antigo phoenix_cover_last_line pode estar salvo como "off" no RetroArch */
    if (opt_get("phoenix_image_filter", &v)) g_filter = !strcmp(v, "linear") ? 1 : !strcmp(v, "shader_std") ? 2 : 0;
    if (opt_get("phoenix_fbo_scale", &v))    { int n = atoi(v); g_scale = n < 1 ? 1 : n > LR_MAX_SCALE ? LR_MAX_SCALE : n; }
}
/* opcoes que valem na hora, sem reiniciar (CD Speed, CPU Overclock, Bypass CLUT, SWI HLE, Debug Output, Active Input Devices, lightgun) */
static void read_live_options(void) {
    const char *v;
    if (opt_get("phoenix_cd_speed", &v))      g_cd_speed = parse_cd_speed(v);
    g_cd_opera_extra = opt_on("phoenix_cd_opera_extra_cmds", g_cd_opera_extra);
    if (opt_get("phoenix_cpu_overclock", &v)) { double f = atof(v); g_overclock = f > 0.0 ? f : 1.0; }
    g_bypass_clut = opt_on("phoenix_vdlp_bypass_clut", g_bypass_clut);
    g_hires       = opt_on("phoenix_high_resolution", g_hires);
    g_swi_hle     = opt_on("phoenix_swi_hle", g_swi_hle);
    g_kprint      = opt_on("phoenix_kprint", g_kprint);
    g_hide_xhair  = opt_on("phoenix_hide_lightgun_crosshairs", g_hide_xhair);
    if (opt_get("phoenix_active_devices", &v)) { int n = atoi(v); g_active_devices = n < 0 ? 0 : n > JOYBUS_PORTS ? JOYBUS_PORTS : n; }
}
static void read_options(void) {
    const char *v;
    read_video_options();
    read_live_options();
    if (opt_get("phoenix_region", &v))       g_region = parse_region(v);
    g_hd = g_region == 0 && opt_get("phoenix_region", &v) && ci_eq(v, "ntsc_hd");   /* "ntsc_hd" = NTSC (parse_region cai em NTSC) com saida 1024x768 */
    g_pal = g_region != 0;
    if (opt_get("phoenix_pixel_format", &v)) g_fmt = parse_pixfmt(v);
    g_cel_legacy = opt_get("phoenix_cel_engine", &v) && ci_eq(v, "legacy");
    g_debug      = opt_on("phoenix_debug", 0);
    if (opt_get("phoenix_bios", &v)) { strncpy(g_bios_choice, v, sizeof g_bios_choice - 1); g_bios_choice[sizeof g_bios_choice - 1] = 0; }
    if (opt_get("phoenix_font", &v)) { strncpy(g_font_choice, v, sizeof g_font_choice - 1); g_font_choice[sizeof g_font_choice - 1] = 0; }
    g_matrix_sw = opt_get("phoenix_madam_matrix_engine", &v) && ci_eq(v, "software");
    if (opt_get("phoenix_random_seed", &v)) {
        if (ci_eq(v, "random")) g_seed_random = 1; else { g_seed_random = 0; g_seed = (uint32_t)strtoul(v, NULL, 0); }
    }
    if (opt_get("phoenix_mem_capacity", &v)) { strncpy(g_mem_cap, v, sizeof g_mem_cap - 1); g_mem_cap[sizeof g_mem_cap - 1] = 0; }
    g_nvram_shared = opt_get("phoenix_nvram_storage", &v) && ci_eq(v, "shared");
    if (opt_get("phoenix_nvram_version", &v)) { int n = atoi(v); g_nvram_ver = n < 0 ? 0 : n > 9 ? 9 : n; }
}

/* ------------------------------------------------------------------ BIOS */
static uint8_t *slurp(const char *path, size_t *n) { return ph_read_all(path, 0x400000, n); }   /* VFS/UTF-8, ver ph_file.h */
static int try_font(ph_machine_t *m, const char *dir, const char *name, int emb) {
    for (unsigned j = 0; j < PH_N_SUBDIRS; j++) {
        char q[1400]; snprintf(q, sizeof q, "%s/%s%s", dir, PH_SUBDIRS[j], name);
        size_t n = 0; uint8_t *fb = slurp(q, &n);
        if (!fb) continue;
        if (n == 0x100000 || n == 933636) { ph_load_font_rom(m, fb, n); logf_(RETRO_LOG_INFO, "FONT-ROM: %s\n", q); free(fb); return 1; }
        free(fb);
    }
    {   /* reserva: font ROM incorporada ao nucleo (ph_options.h), usada quando nao ha arquivo de mesmo nome na pasta system */
        const ph_embfont_t *e = emb ? ph_embfont_find(name) : NULL;
        if (e && (e->size == 0x100000 || e->size == 933636)) { ph_load_font_rom(m, e->data, e->size); logf_(RETRO_LOG_INFO, "FONT-ROM: %s (incorporada ao nucleo)\n", e->file); return 1; }
    }
    return 0;
}
/* FONT-ROM (kanji) no 2o banco: jogos japoneses que usam a fonte do sistema mostram texto em branco sem ela.
 * Opcao "Font (rom2)": "disabled" = nenhuma; nome de arquivo = essa; "auto" (opcao ausente) = a 1a que achar, como sempre. */
static void load_font(ph_machine_t *m, const char *dir) {
    if (!strcmp(g_font_choice, "disabled")) return;
    if (strcmp(g_font_choice, "auto")) { if (!try_font(m, dir, g_font_choice, 1)) logf_(RETRO_LOG_WARN, "Font (rom2) '%s' nao encontrada\n", g_font_choice); return; }
    for (unsigned k = 0; k < PH_N(PH_FONT_DB); k++) if (try_font(m, dir, PH_FONT_DB[k].file, 0)) return;      /* "auto": primeiro o que esta no disco... */
    for (unsigned k = 0; k < sizeof PH_EMBFONTS / sizeof *PH_EMBFONTS; k++) if (try_font(m, dir, PH_EMBFONTS[k].file, 1)) return;   /* ...depois as incorporadas */
}
/* Procura o CRC32 no registro de BIOS (o mesmo do Opera); devolve o nome da maquina ou NULL. */
static const char *bios_label_for_crc(uint32_t crc) {
    for (unsigned k = 0; k < PH_N(PH_BIOS_DB); k++) if (PH_BIOS_DB[k].crc && PH_BIOS_DB[k].crc == crc) return PH_BIOS_DB[k].label;
    return NULL;
}
static int try_bios(ph_machine_t *m, const char *dir, const char *name, int emb) {
    for (unsigned i = 0; i < PH_N_SUBDIRS; i++) {
        char p[1400]; snprintf(p, sizeof p, "%s/%s%s", dir, PH_SUBDIRS[i], name);
        size_t n = 0; uint8_t *b = slurp(p, &n);
        if (!b) continue;
        if (!ph_bios_size_ok(n)) { logf_(RETRO_LOG_WARN, "BIOS ignorada (tamanho %zu; aceito 512 KB arcade, 1 MB ou 2 MB): %s\n", n, p); free(b); continue; }
        uint32_t crc = ph_crc32(b, n); const char *lab = bios_label_for_crc(crc);
        ph_load_bios(m, b, n); free(b);
        if (lab) logf_(RETRO_LOG_INFO, "BIOS: %s (%zu bytes) = %s, CRC32 %08x\n", p, n, lab, crc);
        else     logf_(RETRO_LOG_INFO, "BIOS: %s (%zu bytes), CRC32 %08x (nao consta no registro do Opera)\n", p, n, crc);
        if (n == PH_BIOS_ARCADE_BYTES) logf_(RETRO_LOG_INFO, "BIOS arcade: 512 KB no inicio do banco de ROM, resto zerado, sem font ROM\n");
        if (n == 0x100000) load_font(m, dir);
        return 1;
    }
    {   /* reserva: BIOS incorporada ao nucleo (ph_options.h), usada quando nao ha arquivo de mesmo nome na pasta system */
        const ph_embfont_t *e = emb ? ph_embbios_find(name) : NULL;
        if (e && ph_bios_size_ok(e->size)) {
            const char *lab = bios_label_for_crc(e->crc);
            ph_load_bios(m, e->data, e->size);
            logf_(RETRO_LOG_INFO, "BIOS: %s (%zu bytes) = %s, incorporada ao nucleo, CRC32 %08x\n", e->file, e->size, lab ? lab : e->file, e->crc);
            if (e->size == PH_BIOS_ARCADE_BYTES) logf_(RETRO_LOG_INFO, "BIOS arcade: 512 KB no inicio do banco de ROM, resto zerado, sem font ROM\n");
            if (e->size == 0x100000) load_font(m, dir);
            return 1;
        }
    }
    return 0;
}
static int find_bios(ph_machine_t *m) {
    static const char *all[] = { "fz1.bin", "fz10.bin", "goldstar.bin", "sanyotry.bin", "panafz10.bin" };   /* fz1 primeiro: e a que funciona */
    if (strcmp(g_bios_choice, "auto")) {
        char n[96];
        if (strchr(g_bios_choice, '.')) snprintf(n, sizeof n, "%s", g_bios_choice); else snprintf(n, sizeof n, "%s.bin", g_bios_choice);   /* valores antigos: "fz1", "fz10"... */
        if (try_bios(m, g_system_dir, n, 1)) return 1;                      /* a escolhida: disco, senao a incorporada de mesmo nome */
        logf_(RETRO_LOG_WARN, "BIOS escolhida (%s) nao encontrada; procurando as outras\n", n);
    }
    for (unsigned i = 0; i < sizeof all / sizeof *all; i++) if (try_bios(m, g_system_dir, all[i], 0)) return 1;                       /* primeiro o disco... */
    for (unsigned i = 0; i < PH_N(PH_BIOS_DB); i++) if (try_bios(m, g_system_dir, PH_BIOS_DB[i].file, 0)) return 1;
    for (unsigned i = 0; i < sizeof PH_EMBBIOS / sizeof *PH_EMBBIOS; i++) if (try_bios(m, g_system_dir, PH_EMBBIOS[i].file, 1)) return 1;   /* ...depois as incorporadas (FZ-1 primeiro, arcade por ultimo) */
    return 0;
}

/* ------------------------------------------------------------------ ganchos do nucleo */
/* Os ganchos clio.vdlp_line_pending (-> vdlp_line) e madam.dma580_write (-> vdlp_set_head) ja vem ligados por ph_init()
 * (machine.c); aqui so entram o fim de quadro, o blit (RGB24 -> formato do frontend) e o audio. */
static void h_vdlp_frame(void *ud) { 
    ph_machine_t *m = ud; 
    vdlp_frame_end(&m->vdlp); 
    g_frame_ready = 1; 
    g_frames++; 
}
static void h_blit(void *ud, uint32_t id, int y0, int count, const uint8_t *rgb, int width) {
    (void)ud; (void)id;
    const int hr = g_have_machine && g_m.vdlp.hires;                      /* HiRes: faixas em linhas 2x, largura 2*nativa */
    const int maxw = hr ? LR_MAX_W * 2 : LR_MAX_W, maxh = hr ? LR_MAX_H * 2 : LR_MAX_H;
    uint32_t *base = hr ? g_src2 : g_src;
    if (width <= 0 || width > maxw || y0 < 0) return;
    if (y0 + count > maxh) count = maxh - y0;
    for (int y = 0; y < count; y++) {                       /* so guarda a imagem nativa; video_compose() faz layout, filtro e formato */
        const uint8_t *src = rgb + (size_t)y * (size_t)width * 3;
        uint32_t *dst = base + (size_t)(y0 + y) * (size_t)maxw;
        for (int x = 0; x < width; x++, src += 3) dst[x] = ((uint32_t)src[0] << 16) | ((uint32_t)src[1] << 8) | src[2];
    }
}


/* ---- Deteccao da fase 'The Roof' (Way of the Warrior), so pelo quadro (nao ha como o nucleo saber o nome da fase).
 * Assinatura da fase: ceu de por do sol (laranja/amarelo saturado) em boa parte da metade de cima da janela desenhada + chao de pedra
 * (cinza, pouco saturado) na linha de baixo. Nao exige o vazamento no quadro: quando a fase e reconhecida a ultima linha e coberta inteira,
 * igual ao v9 (o tracinho nem sempre aparece em todo quadro). Margem de PH_ROOF_HOLD quadros evita o conserto piscar quando um quadro
 * (flash de golpe, etc.) foge da assinatura. Outras fases/jogos nao tem ceu laranja + chao cinza, entao nada e alterado neles. */
#ifndef PH_ROOF_HOLD
#define PH_ROOF_HOLD 30
#endif
static int g_roof_hold;     /* quadros restantes de cobertura depois do ultimo quadro reconhecido */
static unsigned g_roof_dbg_gray, g_roof_dbg_n, g_roof_dbg_warm, g_roof_dbg_tot;   /* medidas da ultima deteccao (so para o log de diagnostico) */
static inline int lr_warm(uint32_t c)  { int r = (c >> 16) & 255, b = c & 255; return r >= 150 && b <= 120 && r - b >= 100; }   /* ceu laranja/amarelo */
static inline int lr_gray(uint32_t c)  { int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255; int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b); return mx <= 200 && mx - mn <= 40; }   /* pedra: cinza, escuro ou medio */
/* gl = indice (em linhas nativas) da ultima linha desenhada; devolve 1 se o quadro tem a assinatura da fase The Roof */
static int roof_stage_detect(const uint32_t *cv, unsigned cw, unsigned x0, unsigned y0, unsigned k, unsigned sw, int gl) {
    if (gl < 40) return 0;
    unsigned gray = 0, n = 0;                                                            /* chao: as 2 linhas logo acima da ultima desenhada */
    for (int d = 1; d <= 2; d++) {
        const uint32_t *fl = cv + (size_t)(y0 + (unsigned)(gl - d) * k) * cw + x0;
        for (unsigned x = 0; x < sw; x += k) { n++; if (lr_gray(fl[x])) gray++; }
    }
    g_roof_dbg_gray = gray; g_roof_dbg_n = n; g_roof_dbg_warm = 0; g_roof_dbg_tot = 0;
    if (gray * 100u < n * 40u) return 0;
    unsigned tot = 0, warm = 0;                                                          /* ceu: metade de cima da janela, 1 pixel a cada 4 colunas / 4 linhas */
    for (int g = 0; g < gl / 2; g += 4) {
        const uint32_t *r = cv + (size_t)(y0 + (unsigned)g * k) * cw + x0;
        for (unsigned x = 0; x < sw; x += 4 * k) { if (r[x] & 0xffffffu) { tot++; if (lr_warm(r[x])) warm++; } }
    }
    g_roof_dbg_warm = warm; g_roof_dbg_tot = tot;
    return tot >= 64 && warm * 100u >= tot * 25u;
}

/* ------------------------------------------------------------------ SAIDA DE VIDEO (opcoes de imagem do Phoenix)
 * Cadeia por quadro: g_src (nativo) -> canvas (bordas / fundo expandido com a cor de fundo da VDL) -> escala interna x1..x4
 * com filtro (nenhum = pixel repetido, linear = bilinear, shader std = shaders/std.frag do Phoenix portado) -> formato do frontend.
 * O Phoenix faz o filtro na GPU (OpenGL); aqui e tudo em software, porque o nucleo libretro nao tem contexto GL. */
typedef struct { unsigned w, h, bw, bh, cw, ch, x0, y0, s, k, ow, oh; float aspect; } lr_layout_t;   /* k = fator da imagem de origem (1; 2 no HiRes), s = fator de saida sobre o canvas nativo */

static void video_layout(lr_layout_t *L) {
    L->w = (g_region == VDLP_MODE_PAL2) ? 384 : 320; L->h = g_pal ? 288 : 240;   /* NTSC 320x240, PAL1 320x288, PAL2 384x288 */
    L->bw = g_borders ? L->w / 20 : 0; L->bh = g_borders ? L->h / 20 : 0;            /* "Show borders": margem de 5% com a cor de fundo */
    unsigned iw = L->w + 2 * L->bw, ih = L->h + 2 * L->bh;
    L->cw = iw; L->ch = ih;
    if (g_expand_bg) { unsigned wide = ((ih * 16 + 8) / 9) & ~1u; if (wide > L->cw) L->cw = wide; }   /* "Expand background": fundo ate 16:9 */
    L->x0 = (L->cw - iw) / 2 + L->bw; L->y0 = L->bh;
    L->k = (g_have_machine && g_m.vdlp.hires) ? 2u : 1u;
    /* HiRes: a origem ja e 2x, entao a saida e pelo menos 2x (FBO 1x e 2x = origem 1:1; 3x/4x reamostram a origem 2x) */
    L->s = (L->k == 2 && g_scale < 2) ? 2u : (unsigned)g_scale;
    if (g_fill && L->s < LR_MAX_SCALE) L->s = LR_MAX_SCALE;       /* Screen: Extended: saida em 4x para os pixels ficarem nitidos (ver fill_output) */
    L->ow = L->cw * L->s; L->oh = L->ch * L->s;
    if (g_hd && !g_pal) {   /* NTSC 1024x768 (4:3): canvas 320x240 x 3,2 (com bordas / fundo expandido o canvas cresce na mesma proporcao); s = 3 so como valor nominal (espessura da mira) */
        L->s = 3u; L->ow = L->cw * 16u / 5u; L->oh = L->ch * 16u / 5u;
    }
    /* "Keep screen aspect ratio": a imagem (com bordas) fica em 4:3; desligado, pixels quadrados (largura/altura do canvas) */
    float par = g_keep_aspect ? (4.0f / 3.0f) / ((float)iw / (float)ih) : 1.0f;
    L->aspect = (float)L->cw * par / (float)L->ch;
}

static inline uint32_t lr_out_pixel(uint32_t c) {
    if (g_fmt == LR_RGB565)   return (uint32_t)(((c >> 8) & 0xf800u) | ((c >> 5) & 0x07e0u) | ((c >> 3) & 0x001fu));
    if (g_fmt == LR_0RGB1555) return (uint32_t)(((c >> 9) & 0x7c00u) | ((c >> 6) & 0x03e0u) | ((c >> 3) & 0x001fu));
    return c;
}
static void lr_store_row(unsigned oy, const uint32_t *row, unsigned ow) {
    if (g_fmt != LR_XRGB8888) { uint16_t *d = (uint16_t *)g_fb + (size_t)oy * ow; for (unsigned x = 0; x < ow; x++) d[x] = (uint16_t)lr_out_pixel(row[x]); }
    else memcpy((uint32_t *)g_fb + (size_t)oy * ow, row, (size_t)ow * 4);
}

/* ---- shaders/std.frag (Maxim L. Grishin, licenca zlib) em software. Mesmas contas do GLSL; o que so depende da celula de
 * origem (diferencas entre os 4 vizinhos e o fator da diagonal) e calculado uma vez por celula, nao por pixel de saida. */
typedef struct { float dx, dy; uint8_t a, b; } lr_cell_t;
static lr_cell_t g_cell[LR_MAX_CW * LR_MAX_CH * 4];

static inline float lr_cdist(uint32_t p, uint32_t q) {
    float dr = (float)((int)((p >> 16) & 255) - (int)((q >> 16) & 255)), dg = (float)((int)((p >> 8) & 255) - (int)((q >> 8) & 255)), db = (float)((int)(p & 255) - (int)(q & 255));
    return sqrtf(dr * dr + dg * dg + db * db) * (1.0f / 255.0f);
}
static void shader_prepare(unsigned cw, unsigned ch) {
    for (unsigned y = 0; y < ch; y++) for (unsigned x = 0; x < cw; x++) {
        unsigned h = (x + 1 < cw) ? 1 : 0, v = (y + 1 < ch) ? 1 : 0;
        uint32_t lu = g_cv[y * cw + x], ru = g_cv[y * cw + x + h], rd = g_cv[(y + v) * cw + x + h], ld = g_cv[(y + v) * cw + x];
        float llu = lr_cdist(lu, rd), lru = lr_cdist(ru, ld), hlu = lr_cdist(lu, ru), hld = lr_cdist(rd, ld);   /* vlu == hlu e vru == hld no GLSL */
        lr_cell_t *c = &g_cell[y * cw + x];
        c->a = (llu < 0.2f && (hlu < 0.2f || hlu < 0.2f || lru > 0.3f)) ? 1 : 0;
        c->b = (lru < 0.2f && (hld < 0.2f || hld < 0.2f || llu > 0.3f)) ? 1 : 0;
        c->dx = c->dy = 1.0f;
        if (c->a || c->b) { float a = llu + 0.1f, b = lru + 0.1f, n = sqrtf(a * a + b * b); c->dx = 1.1f - a / n; c->dy = 1.1f - b / n; }
    }
}
static inline uint32_t lr_pack(float r, float g, float b) {
    int ri = (int)(r + 0.5f), gi = (int)(g + 0.5f), bi = (int)(b + 0.5f);
    ri = ri < 0 ? 0 : ri > 255 ? 255 : ri; gi = gi < 0 ? 0 : gi > 255 ? 255 : gi; bi = bi < 0 ? 0 : bi > 255 ? 255 : bi;
    return ((uint32_t)ri << 16) | ((uint32_t)gi << 8) | (uint32_t)bi;
}
static inline float lr_len2(float x, float y) { return sqrtf(x * x + y * y); }
static inline float lr_fall(float d) { return d >= 1.0f ? 0.0f : 1.0f - d; }

/* coordenada de textura -> celula + fracao (tpos = tc*tamanho - 0.5, com piso em 0), como no GLSL */
static inline void lr_tpos(unsigned o, unsigned num, unsigned den, unsigned n, unsigned *p, float *f) {   /* o = pixel de saida; num/den = amostras da origem por pixel de saida (origem/saida, qualquer razao: 1/s, k/s ou 5/16 no NTSC 1024x768) */
    float t = ((float)o + 0.5f) * (float)num / (float)den - 0.5f;
    if (t < 0.0f) t = 0.0f;
    unsigned q = (unsigned)t; if (q > n - 1) q = n - 1;
    *p = q; *f = t - (float)q;
}

/* ---- Screen: Extended (botoes L2 / R2). Alguns jogos desenham numa janela menor que o quadro e deixam o resto preto.
 * Acha-se a caixa do que foi realmente desenhado (pixel nao preto) e ela e escalada ate cobrir o canvas inteiro.
 * ESCALONAMENTO DE PIXELS (sem zoom borrado): a caixa e amostrada direto na resolucao de SAIDA, vizinho mais proximo, sem
 * interpolacao (o filtro de imagem nao entra), e a saida sobe para pelo menos 4x o canvas (video_layout). Assim cada pixel do jogo
 * vira um bloco de ~4x4 pixels nitidos (a diferenca de tamanho entre blocos vizinhos e de 1 pixel de saida) e o RetroArch/shader
 * faz so uma ampliacao pequena por cima, em vez de esticar 320x240 inteiro com suavizacao.
 * A caixa so CRESCE na hora (se surge conteudo fora dela) e so ENCOLHE depois de 90 quadros seguidos menores (~1,5 s),
 * para nao pular em fade/cena escura. Quadro quase todo preto (< 25% em largura ou altura) nao muda a caixa. */
static int fill_apply(unsigned cw, unsigned ch, unsigned x0, unsigned y0, unsigned sw, unsigned sh) {
    if (sw == 0 || sh == 0 || x0 + sw > cw || y0 + sh > ch || (size_t)cw * ch > (size_t)LR_MAX_CW * LR_MAX_CH * 4u) return 0;
    {   const unsigned key[6] = { cw, ch, x0, y0, sw, sh };
        if (memcmp(key, g_fill_key, sizeof key)) { memcpy(g_fill_key, key, sizeof key); g_fill_valid = 0; g_fill_shrink = 0; } }
    /* 1) caixa do conteudo deste quadro */
    int mx0 = (int)sw, my0 = (int)sh, mx1 = -1, my1 = -1;
    for (unsigned y = 0; y < sh; y++) {
        const uint32_t *p = g_cv + (size_t)(y0 + y) * cw + x0; int hit = 0;
        for (unsigned x = 0; x < sw; x++) if (p[x] & 0xf8f8f8u) { if ((int)x < mx0) mx0 = (int)x; if ((int)x > mx1) mx1 = (int)x; hit = 1; }
        if (hit) { if ((int)y < my0) my0 = (int)y; my1 = (int)y; }
    }
    /* 2) caixa estavel */
    if (mx1 >= 0 && (unsigned)(mx1 - mx0 + 1) * 4u >= sw && (unsigned)(my1 - my0 + 1) * 4u >= sh) {
        if (!g_fill_valid) { g_fill_bx0 = mx0; g_fill_by0 = my0; g_fill_bx1 = mx1; g_fill_by1 = my1; g_fill_valid = 1; g_fill_shrink = 0; }
        else if (mx0 < g_fill_bx0 || my0 < g_fill_by0 || mx1 > g_fill_bx1 || my1 > g_fill_by1) {   /* cresce na hora */
            if (mx0 < g_fill_bx0) g_fill_bx0 = mx0;
            if (my0 < g_fill_by0) g_fill_by0 = my0;
            if (mx1 > g_fill_bx1) g_fill_bx1 = mx1;
            if (my1 > g_fill_by1) g_fill_by1 = my1;
            g_fill_shrink = 0;
        } else if (mx0 > g_fill_bx0 || my0 > g_fill_by0 || mx1 < g_fill_bx1 || my1 < g_fill_by1) {   /* encolhe so depois de 90 quadros */
            if (++g_fill_shrink >= 90) { g_fill_bx0 = mx0; g_fill_by0 = my0; g_fill_bx1 = mx1; g_fill_by1 = my1; g_fill_shrink = 0; }
        } else g_fill_shrink = 0;
    }
    return g_fill_valid;
}

/* escreve a saida (ow x oh) amostrando a caixa do conteudo, pixel a pixel (vizinho mais proximo), sem interpolacao */
static void fill_output(unsigned cw, unsigned x0, unsigned y0, unsigned ow, unsigned oh) {
    static uint32_t row[LR_MAX_CW * LR_MAX_SCALE];
    static unsigned px[LR_MAX_CW * LR_MAX_SCALE];
    const unsigned cx = x0 + (unsigned)g_fill_bx0, cy = y0 + (unsigned)g_fill_by0;
    const unsigned bw = (unsigned)(g_fill_bx1 - g_fill_bx0 + 1), bh = (unsigned)(g_fill_by1 - g_fill_by0 + 1);
    if (ow == 0 || oh == 0 || ow > LR_MAX_CW * LR_MAX_SCALE) return;
    for (unsigned x = 0; x < ow; x++) px[x] = cx + (unsigned)(((uint64_t)(2u * x + 1u) * bw) / (2u * (uint64_t)ow));
    for (unsigned y = 0; y < oh; y++) {
        const uint32_t *src = g_cv + (size_t)(cy + (unsigned)(((uint64_t)(2u * y + 1u) * bh) / (2u * (uint64_t)oh))) * cw;
        for (unsigned x = 0; x < ow; x++) row[x] = src[px[x]];
        lr_store_row(y, row, ow);
    }
}

static void video_compose(const lr_layout_t *L, unsigned w, unsigned h) {
    if (!g_fb) return;
    const unsigned k = L->k, ow = L->ow, oh = L->oh;
    const unsigned cw = L->cw * k, ch = L->ch * k;        /* canvas na resolucao da origem (k = 2 no HiRes) */
    const unsigned x0 = L->x0 * k, y0 = L->y0 * k, sw = w * k, sh = h * k, sstride = LR_MAX_W * k;
    const uint32_t *srcbuf = (k == 2) ? g_src2 : g_src;
    static uint32_t row[LR_MAX_CW * LR_MAX_SCALE];
    static unsigned px[LR_MAX_CW * LR_MAX_SCALE]; static float fx[LR_MAX_CW * LR_MAX_SCALE];
    /* 1) canvas nativo: fundo da VDL nas margens (so preenche se ha margem) */
    if (cw != sw || ch != sh) {
        uint32_t bg = g_have_machine ? g_m.vdlp.bg : 0;
        uint32_t bgc = ((bg & 0xffu) << 16) | (bg & 0xff00u) | ((bg >> 16) & 0xffu);
        for (unsigned i = 0; i < cw * ch; i++) g_cv[i] = bgc;
    }
    for (unsigned y = 0; y < sh && y0 + y < ch; y++) memcpy(g_cv + (size_t)(y0 + y) * cw + x0, srcbuf + (size_t)y * sstride, (size_t)sw * 4);
    /* Tracinhos de ceu na ULTIMA linha DESENHADA, SOMENTE na fase 'The Roof' de Way of the Warrior (aparecem com ou sem HiRes). O jogo desenha numa
     * janela de 228 linhas (6..233) e deixa o resto do buffer preto; na ultima linha da janela alguns trechos ficam sem o chao e o ceu do fundo vaza.
     * So cobre quando o quadro tem a assinatura da fase (roof_stage_detect), copiando a linha de cima por cima da ultima desenhada (igual ao v9). Em qualquer outra fase ou jogo o quadro sai exatamente como o jogo desenha. */
    if (g_cover_last && h >= 2 && y0 + sh <= ch) {
        int gl = -1;                                          /* ultimo grupo de k linhas (1 linha nativa) que nao e todo preto */
        for (int g = (int)h - 1; g >= 0 && gl < 0; g--)
            for (unsigned i = 0; i < k && gl < 0; i++) {
                const uint32_t *p = g_cv + (size_t)(y0 + (unsigned)g * k + i) * cw + x0;
                for (unsigned xx = 0; xx < sw; xx++) if (p[xx] & 0xffffffu) { gl = g; break; }
            }
        int was = g_roof_active;
        if (gl >= 1 && roof_stage_detect(g_cv, cw, x0, y0, k, sw, gl)) g_roof_hold = PH_ROOF_HOLD;
        else if (g_roof_hold > 0) g_roof_hold--;
        g_roof_active = g_roof_hold > 0 && gl >= 1;
        if (g_debug && !g_roof_active && gl >= 1 && g_frames % 120 == 0 && g_roof_dbg_n)   /* diagnostico: so com "Diagnostico no log", 1x por 2 s */
            logf_(RETRO_LOG_INFO, "ROOFDBG ultima_linha=%d hires=%u chao_cinza=%u/%u ceu_quente=%u/%u (precisa chao>=40%% e ceu>=25%%) -> fase nao reconhecida\n", gl, k == 2 ? 1u : 0u, g_roof_dbg_gray, g_roof_dbg_n, g_roof_dbg_warm, g_roof_dbg_tot);
        if (g_roof_active != was) logf_(RETRO_LOG_INFO, "The Roof %s (ultima linha desenhada=%d, hires=%u)\n", g_roof_active ? "DETECTADA: cobrindo tracinho de ceu" : "nao detectada: quadro sem alteracao", gl, k == 2 ? 1u : 0u);
        if (g_roof_active)                                    /* igual ao v9: a ultima linha desenhada vira copia da de cima */
            for (unsigned i = 0; i < k; i++)
                memcpy(g_cv + (size_t)(y0 + (unsigned)gl * k + i) * cw + x0, g_cv + (size_t)(y0 + (unsigned)(gl - 1) * k + i) * cw + x0, (size_t)sw * 4);
    } else { g_roof_active = 0; g_roof_hold = 0; }
    if (g_fill && fill_apply(cw, ch, x0, y0, sw, sh)) { fill_output(cw, x0, y0, ow, oh); return; }   /* L2/R2: Screen: Extended (pula escala/filtro normais) */
    /* 2) escala interna + filtro */
    if (ow == cw && oh == ch && g_filter == 0) { for (unsigned y = 0; y < ch; y++) lr_store_row(y, g_cv + (size_t)y * cw, cw); return; }
    if (g_filter == 2) shader_prepare(cw, ch);
    for (unsigned x = 0; x < ow; x++) lr_tpos(x, cw, ow, cw, &px[x], &fx[x]);
    for (unsigned oy = 0; oy < oh; oy++) {
        if (g_filter == 0) { const uint32_t *src = g_cv + (size_t)((uint64_t)oy * ch / oh) * cw; for (unsigned x = 0; x < ow; x++) row[x] = src[(uint64_t)x * cw / ow]; lr_store_row(oy, row, ow); continue; }
        unsigned py; float fy; lr_tpos(oy, ch, oh, ch, &py, &fy);
        const unsigned v = (py + 1 < ch) ? 1 : 0;
        for (unsigned x = 0; x < ow; x++) {
            const unsigned cx = px[x], h1 = (cx + 1 < cw) ? 1 : 0; const float fxx = fx[x];
            const uint32_t lu = g_cv[py * cw + cx], ru = g_cv[py * cw + cx + h1], rd = g_cv[(py + v) * cw + cx + h1], ld = g_cv[(py + v) * cw + cx];
            float d1, d2, d3, d4;
            if (g_filter == 1) {                              /* bilinear (GL_LINEAR com o mesmo centro de texel) */
                d1 = (1.0f - fxx) * (1.0f - fy); d2 = fxx * (1.0f - fy); d3 = fxx * fy; d4 = (1.0f - fxx) * fy;
                if (!h1) { d1 += d2; d4 += d3; d2 = d3 = 0.0f; }
                if (!v)  { d1 += d4; d2 += d3; d3 = d4 = 0.0f; }
            } else {                                          /* shader std */
                const float hf = (float)h1, vf = (float)v;
                d1 = lr_fall(lr_len2(fxx, fy)); d2 = lr_fall(lr_len2(fxx - hf, fy)); d3 = lr_fall(lr_len2(fxx - hf, fy - vf)); d4 = lr_fall(lr_len2(fxx, fy - vf));
                const lr_cell_t *c = &g_cell[py * cw + cx];
                const float lu_dist = fxx + fy, ru_dist = 1.0f - fxx + fy;
                if ((c->a && ru_dist >= 0.6f && ru_dist < 1.4f) || (c->b && lu_dist >= 0.6f && lu_dist < 1.4f)) { d1 *= c->dx; d2 *= c->dy; d3 *= c->dx; d4 *= c->dy; }
            }
            const float sum = d1 + d2 + d3 + d4;
            if (sum <= 1e-6f) { row[x] = lu; continue; }
            const float k = 1.0f / sum;
            row[x] = lr_pack((float)((lu >> 16) & 255) * d1 * k + (float)((ru >> 16) & 255) * d2 * k + (float)((rd >> 16) & 255) * d3 * k + (float)((ld >> 16) & 255) * d4 * k,
                             (float)((lu >> 8) & 255) * d1 * k + (float)((ru >> 8) & 255) * d2 * k + (float)((rd >> 8) & 255) * d3 * k + (float)((ld >> 8) & 255) * d4 * k,
                             (float)(lu & 255) * d1 * k + (float)(ru & 255) * d2 * k + (float)(rd & 255) * d3 * k + (float)(ld & 255) * d4 * k);
        }
        lr_store_row(oy, row, ow);
    }
}

/* ------------------------------------------------------------------ DIAGNOSTICO (travamento apos carregar)
 * KDBG: texto de debug do kernel 3DO (SWI 0x1000e, 1 char por chamada); STAT: uma linha por segundo com PC, IRQs,
 * CEL (desenhados / nao suportados), CD e histograma dos SWIs. Tudo vai para o log do RetroArch. */
#define SWI_SLOTS 64
static struct { uint32_t imm, n, pc, r0, r1, r2, lr; } g_swi[SWI_SLOTS];
static unsigned g_swi_other, g_a7_logged;
/* HLE TEMPORARIO (ate existir DSP real): a inicializacao de audio do jogo grava um valor no knob PlayControl
 * (SWI 0x40000, r0=item, r1=valor) e espera le-lo de volta pela SWI 0x40007 (r1=9). Sem DSP nada volta e o jogo gira
 * para sempre. Aqui devolvemos o ultimo valor gravado no item que recebeu um valor >= 0x8000 (0 enquanto nenhum recebeu). */
static int g_pc_valid; static uint32_t g_pc_item, g_pc_val;
/* HLE TEMPORARIO 4: o jogo (0x10af90-0x10b030) zera/liga o PlayControl de um item NOVO (valores 0 e 1) e le de volta ate bater.
 * Como o item novo ainda nao recebeu valor >= 0x8000, o eco antigo devolvia o valor do item velho e o laco nunca saia.
 * Se o ultimo valor gravado foi 0 ou 1 e foi para outro item, devolvemos esse valor. */
static int g_lw_valid; static uint32_t g_lw_item, g_lw_val;
/* HLE TEMPORARIO 2: depois da inicializacao o jogo fica em SWI 0x4001e (r0=item 0x1005e), esperando o valor lido MUDAR
 * (contador de tempo do audio; o DSP ainda nao existe). Devolvemos um contador que avanca a cada amostra de audio. */
static uint32_t g_aud_ticks;
/* DIAGNOSTICO 22/10 (jogo volta para a intro ao iniciar a partida): cauda das ultimas SWIs, texto do kernel e codigo ao redor do PC */
#define SWI_RING 48
static struct { uint32_t imm, pc, r0, r1, r2; } g_ring[SWI_RING];
static unsigned g_ring_n, g_kdbg_n;
static void kdbg_text(uint32_t r0) {                  /* SWI 0x1000e: o argumento (caractere OU ponteiro para texto) vem em r1; r0 e o ponteiro do stream (constante) */
    static char line[200]; static unsigned ln;
    if (g_kdbg_n >= 600) return;
    if (r0 < 0x100u) {
        if (r0 == '\n' || ln >= sizeof line - 1) { line[ln] = 0; g_kdbg_n++; if (ln) logf_(RETRO_LOG_INFO, "KDBG: %s\n", line); ln = 0; }
        else if (r0 >= 0x20u) line[ln++] = (char)r0;
    } else if (r0 < 0x2fff00u) {
        char t[97]; unsigned k;
        for (k = 0; k < 96; k++) { uint8_t ch = ((const uint8_t *)g_m.ram)[(r0 + k) ^ 3u]; if (ch == 0) break; t[k] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '.'; }
        t[k] = 0; g_kdbg_n++;
        if (k >= 3) logf_(RETRO_LOG_INFO, "KDBG: @%08x '%s'\n", r0, t);
    }
}
static int diag_swi(void *u, uint32_t imm) {
    (void)u;
    unsigned i;
    g_ring[g_ring_n % SWI_RING].imm = imm; g_ring[g_ring_n % SWI_RING].pc = g_m.cpu.r[15]; g_ring[g_ring_n % SWI_RING].r0 = g_m.cpu.r[0];
    g_ring[g_ring_n % SWI_RING].r1 = g_m.cpu.r[1]; g_ring[g_ring_n % SWI_RING].r2 = g_m.cpu.r[2]; g_ring_n++;
    if (imm == 0x1000eu) kdbg_text(g_m.cpu.r[1]);   /* r1, nao r0: r0 e o stream */
    for (i = 0; i < SWI_SLOTS && g_swi[i].n && g_swi[i].imm != imm; i++) {}
    if (i == SWI_SLOTS) { g_swi_other++; return 0; }
    g_swi[i].imm = imm; g_swi[i].n++; g_swi[i].pc = g_m.cpu.r[15];
    g_swi[i].r0 = g_m.cpu.r[0]; g_swi[i].r1 = g_m.cpu.r[1]; g_swi[i].r2 = g_m.cpu.r[2]; g_swi[i].lr = g_m.cpu.r[14];
    if (imm == 0x40000) {
        uint32_t it = g_m.cpu.r[0], v = g_m.cpu.r[1];
        if (v >= 0x8000u && v < 0x10000u) { g_pc_valid = 1; g_pc_item = it; }
        if (g_pc_valid && it == g_pc_item) g_pc_val = v;
        g_lw_item = it; g_lw_val = v; g_lw_valid = 1;
    }
    if (g_debug && imm == 0x40007 && g_a7_logged < 3 && (g_swi[i].n == 1 || g_swi[i].n == 5000)) {   /* audio folio, funcao 7: le registradores de hardware */
        uint32_t a = g_m.cpu.r[0] & 0x3ffffcu; char b[400]; int n = 0;
        for (int k = 0; k < 16 && a + 4u * (unsigned)k < g_m.ram_bytes; k++) n += snprintf(b + n, sizeof b - (size_t)n, " %08x", g_m.ram[(a >> 2) + (unsigned)k]);
        g_a7_logged++;
        logf_(RETRO_LOG_INFO, "A7 args@%06x:%s | clio3c00:", a, b);
        for (int k = 0; k < 16; k++) logf_(RETRO_LOG_INFO, " %08x", ph_read32(&g_m, 0x03403c00u + 4u * (unsigned)k));
        logf_(RETRO_LOG_INFO, "\n | madam400:");
        for (int k = 0; k < 33; k++) logf_(RETRO_LOG_INFO, " %08x", ph_read32(&g_m, 0x03300400u + 4u * (unsigned)k));
        logf_(RETRO_LOG_INFO, "\n");
    }
    return 0;
}
static int g_dumped;
static int g_rl_left_boot, g_rl_fired;   /* usados por diag_second (guarda do dump fixo) e check_relaunch, abaixo */
static int dump_file(const char *p, const void *d, size_t n) {   /* grava um bloco inteiro pelo VFS; 1 = ok */
    ph_file *f = ph_fopen(p, "wb"); size_t w;
    if (!f) return 0;
    w = ph_fwrite(d, n, f); ph_fclose(f);
    return w == n;
}
static void diag_dump(void) {
    g_dumped = 1;                  /* RAM (3 MB, big-endian) + registradores do CLIO, para analise offline */
    char p[1500]; uint8_t *tmp = malloc(g_m.ram_bytes);
    if (!tmp) return;
    for (unsigned i = 0; i < g_m.ram_bytes / 4; i++) { uint32_t w = g_m.ram[i]; tmp[4*i] = (uint8_t)(w >> 24); tmp[4*i+1] = (uint8_t)(w >> 16); tmp[4*i+2] = (uint8_t)(w >> 8); tmp[4*i+3] = (uint8_t)w; }
    snprintf(p, sizeof p, "%s/phoenix_dump_ram.bin", g_system_dir);
    if (dump_file(p, tmp, g_m.ram_bytes)) logf_(RETRO_LOG_INFO, "DUMP: RAM gravada em %s\n", p);
    else logf_(RETRO_LOG_ERROR, "DUMP: nao consegui gravar %s\n", p);
    snprintf(p, sizeof p, "%s/phoenix_dump_clio.bin", g_system_dir);
    dump_file(p, g_m.clio.R, 0x10000);
    snprintf(p, sizeof p, "%s/phoenix_dump_madam.bin", g_system_dir);   /* regs[0x200] = registradores 0x000..0x7fc (0x580 = cabeca da VDL, 0x008 = MCTL) */
    dump_file(p, g_m.madam.regs, 4u * 0x200u);
    if (g_m.hires) for (int q = 0; q < 3; q++) if (g_m.hr[q]) {   /* planos extras do HiRes (1 MB cada, big-endian como o ram) */
        for (unsigned i = 0; i < g_m.vram_bytes / 4; i++) { uint32_t w = g_m.hr[q][i]; tmp[4*i] = (uint8_t)(w >> 24); tmp[4*i+1] = (uint8_t)(w >> 16); tmp[4*i+2] = (uint8_t)(w >> 8); tmp[4*i+3] = (uint8_t)w; }
        snprintf(p, sizeof p, "%s/phoenix_dump_hr%d.bin", g_system_dir, q);
        dump_file(p, tmp, g_m.vram_bytes);
    }
    free(tmp);
}
/* diag_snapshot: nucleo comum do que antes so saia 1x/s dentro de diag_second (REG, SWITAIL, CODEPC).
 * Extraido para poder ser chamado tambem IMEDIATAMENTE pelo gatilho de evento (ver check_relaunch),
 * em vez de esperar ate 1s pelo proximo tick de diag_second e correr o risco de perder o instante exato. */
static void diag_snapshot(const char *tag) {
    logf_(RETRO_LOG_INFO, "REG %s r0-r14: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", tag,
          g_m.cpu.r[0], g_m.cpu.r[1], g_m.cpu.r[2], g_m.cpu.r[3], g_m.cpu.r[4], g_m.cpu.r[5], g_m.cpu.r[6], g_m.cpu.r[7], g_m.cpu.r[8],
          g_m.cpu.r[9], g_m.cpu.r[10], g_m.cpu.r[11], g_m.cpu.r[12], g_m.cpu.r[13], g_m.cpu.r[14]);
    {   /* ultimas SWIs (da mais antiga para a mais nova) e codigo ao redor do PC: da para desmontar offline (armdis.py) */
        unsigned tot = g_ring_n < SWI_RING ? g_ring_n : SWI_RING, st = g_ring_n - tot;
        for (unsigned k = 0; k < tot; k += 8) {
            char ln[512]; int n = 0;
            for (unsigned j = k; j < tot && j < k + 8; j++) { const unsigned e = (st + j) % SWI_RING;
                n += snprintf(ln + n, sizeof ln - (size_t)n, " %06x@%x(%x,%x,%x)", g_ring[e].imm, g_ring[e].pc, g_ring[e].r0, g_ring[e].r1, g_ring[e].r2); }
            logf_(RETRO_LOG_INFO, "SWITAIL %s #%u:%s\n", tag, k / 8, ln);
        }
        uint32_t pc = g_m.cpu.r[15] & ~3u;
        if (pc >= 0x40u && pc < 0x2fff00u) {
            char ln[600]; int n = 0; const uint32_t a0 = pc - 0x40u;
            for (uint32_t k = 0; k < 32; k++) n += snprintf(ln + n, sizeof ln - (size_t)n, " %08x", g_m.ram[(a0 >> 2) + k]);
            logf_(RETRO_LOG_INFO, "CODEPC %s %06x:%s\n", tag, a0, ln);
        }
    }
}
static void diag_second(unsigned sec) {
    if (!g_debug) return;
    char tag[16]; snprintf(tag, sizeof tag, "t=%us", sec);
    logf_(RETRO_LOG_INFO, "STAT t=%us PC=%08x CPSR=%08x IRQ0=%08x EN0=%08x IRQ1=%08x EN1=%08x cel(drawn=%u unsup=%u poly=%u) cd(S=%02x lba=%d) otherSWI=%u math(n3=%u z0=%u ovf=%u ovf_z=%u)\n",
          sec, g_m.cpu.r[15], g_m.cpu.cpsr, g_m.clio.R[0x40 >> 2], g_m.clio.R[0x48 >> 2], g_m.clio.R[0x60 >> 2], g_m.clio.R[0x68 >> 2],
          g_m.cel.drawn, g_m.cel.unsupported, g_m.cel.poly, g_m.cd.S, g_m.cd.lba, g_swi_other,
          g_m.madam.st_math_n3, g_m.madam.st_math_z0, g_m.madam.st_math_ovf, g_m.madam.st_math_ovf_z);
    {   /* PARTE19h: relogio do audio. Esperado: steps=44100/s, passes~=steps, ticks(0xee)~=240/s (44100/184) */
        static uint32_t p_steps, p_passes, p_ee;
        logf_(RETRO_LOG_INFO, "AUDIO t=%us dsp_steps=%u dsp_passes=%u dsp_ee_ticks=%u (por segundo) folio_tempo=%u cyc_total=%u\n", sec,
              ph_dsp_steps - p_steps, ph_dsp_passes - p_passes, ph_dsp_ee - p_ee,
              (g_m.ram[0x7bf10u >> 2] >= 0x1000u && g_m.ram[0x7bf10u >> 2] < 0x2ffe00u) ? g_m.ram[(g_m.ram[0x7bf10u >> 2] + 0x9cu) >> 2] : 0u,
              (unsigned)g_m.total_cycles);
        p_steps = ph_dsp_steps; p_passes = ph_dsp_passes; p_ee = ph_dsp_ee;
        {   /* vigia as escritas no contador de tempo do audio folio */
            static uint32_t p_tk; static int tk_printed;
            uint32_t kb = g_m.ram[0x7bf10u >> 2];
            if (!g_m.tk_addr && kb >= 0x1000u && kb < 0x2ffe00u && !(kb & 3u) && g_m.ram[(kb + 0x9cu) >> 2] != 0) g_m.tk_addr = kb + 0x9cu;
            logf_(RETRO_LOG_INFO, "AUDIO-TK t=%us escritas_no_contador=%u por_segundo (addr=%08x amostras_por_tick=%u)\n", sec, g_m.tk_n - p_tk, g_m.tk_addr,
                  g_m.tk_addr ? g_m.ram[(g_m.tk_addr - 0x9cu + 0xe0u) >> 2] : 0u);
            p_tk = g_m.tk_n;
            if (g_m.tk_cnt >= 24 && !tk_printed) {
                tk_printed = 1;
                for (unsigned i = 0; i < 24; i++)
                    logf_(RETRO_LOG_INFO, "AUDIO-TK #%u pc=%08x lr=%08x antigo=%u novo=%u delta=%d\n", i, g_m.tkring[i].pc, g_m.tkring[i].lr,
                          g_m.tkring[i].old, g_m.tkring[i].v, (int)(g_m.tkring[i].v - g_m.tkring[i].old));
            }
        }
    }
    logf_(RETRO_LOG_INFO, "VDLP t=%us head=%06x cur=%06x curbmp=%06x bg=%06x outctl=%08x MCTL=%08x ctl_seen=%08x ctl_bits=%08x vazias=%u hires=%u cel(chains=%u drawn=%u) frames=%u\n", sec,
          g_m.vdlp.headvdl, g_m.vdlp.curvdl, g_m.vdlp.curbmp, g_m.vdlp.bg, g_m.vdlp.outctl, g_m.madam.regs[2], g_m.vdlp.stat_ctl_seen, g_m.vdlp.stat_ctl_bits,
          g_m.vdlp.stat_empty_vdl, g_m.vdlp.stat_hires_lines, g_m.cel.chains, g_m.cel.drawn, g_frames);
    for (unsigned i = 0; i < SWI_SLOTS && g_swi[i].n; i++)
        logf_(RETRO_LOG_INFO, "SWI t=%us imm=%06x n=%u lastpc=%08x lr=%08x r0=%08x r1=%08x r2=%08x\n", sec, g_swi[i].imm, g_swi[i].n, g_swi[i].pc, g_swi[i].lr, g_swi[i].r0, g_swi[i].r1, g_swi[i].r2);
    if (sec >= 25 && sec % 3 == 0) {          /* formatos de CEL desenhados nos ultimos 3 s (para achar o que sai errado) */
        for (unsigned q = 0; q < 48 && g_m.cel.hist[q].n; q++)
            logf_(RETRO_LOG_INFO, "CELFMT t=%us n=%u path=%u flags=%08x pre0=%08x pre1=%08x ppmp=%08x pixc=%08x clip=%08x wr=%06x/%x rd=%06x/%x src=%06x\n", sec, g_m.cel.hist[q].n, g_m.cel.hist[q].path,
                  g_m.cel.hist[q].flags, g_m.cel.hist[q].pre0, g_m.cel.hist[q].pre1, g_m.cel.hist[q].ppmp, g_m.cel.hist[q].pixc,
                  g_m.cel.hist[q].clip, g_m.cel.hist[q].wrb, g_m.cel.hist[q].wrs, g_m.cel.hist[q].rdb, g_m.cel.hist[q].rds, g_m.cel.hist[q].src);
        logf_(RETRO_LOG_INFO, "CELFMT t=%us outros=%u\n", sec, g_m.cel.hist_other);
        memset(g_m.cel.hist, 0, sizeof g_m.cel.hist); g_m.cel.hist_other = 0;
    }
    logf_(RETRO_LOG_INFO, "CELINV t=%us cels=%u sem_pixel=%u (culled=%u fora_da_tela=%u so_transparente=%u degenerado=%u outro=%u)\n", sec,
          g_m.cel.cels_total, g_m.cel.inv_total, g_m.cel.inv_culled, g_m.cel.inv_offscreen, g_m.cel.inv_transp, g_m.cel.inv_degen, g_m.cel.inv_other);
    for (unsigned q = 0; q < g_m.cel.ninv; q++)
        logf_(RETRO_LOG_INFO, "CELINV t=%us ex%u path=%u fl=%08x pre0=%08x pre1=%08x wh=%dx%d pos=%08x,%08x hd=%08x,%08x vd=%08x,%08x hdd=%08x,%08x clip=%08x tri=%u culled=%u\n", sec, q,
              g_m.cel.inv[q].path, g_m.cel.inv[q].flags, g_m.cel.inv[q].pre0, g_m.cel.inv[q].pre1, g_m.cel.inv[q].w, g_m.cel.inv[q].h, g_m.cel.inv[q].xpos, g_m.cel.inv[q].ypos,
              g_m.cel.inv[q].hdx, g_m.cel.inv[q].hdy, g_m.cel.inv[q].vdx, g_m.cel.inv[q].vdy, g_m.cel.inv[q].hddx, g_m.cel.inv[q].hddy, g_m.cel.inv[q].clip, g_m.cel.inv[q].tri, g_m.cel.inv[q].tri_culled);
    g_m.cel.cels_total = g_m.cel.inv_total = g_m.cel.inv_culled = g_m.cel.inv_offscreen = g_m.cel.inv_transp = g_m.cel.inv_degen = g_m.cel.inv_other = 0; g_m.cel.ninv = 0;
    diag_snapshot(tag);
    memset(g_swi, 0, sizeof g_swi); g_swi_other = 0; if (sec % 5 == 0) g_a7_logged = 0;
    if (sec == 52) {               /* codigo do laco da SWI 0x40007 (carregado depois do 20o segundo) em hexa no proprio log */
        static const uint32_t ini[2] = { 0x10af40u, 0x10d180u }, qtd[2] = { 64u, 24u };
        for (int r = 0; r < 2; r++) {
            logf_(RETRO_LOG_INFO, "CODE %06x:", ini[r]);
            for (uint32_t k = 0; k < qtd[r]; k++) logf_(RETRO_LOG_INFO, " %08x", g_m.ram[(ini[r] >> 2) + k]);
            logf_(RETRO_LOG_INFO, "\n");
        }
        if (!g_rl_fired) diag_dump();   /* se o relancamento ja foi pego por check_relaunch, nao sobrescreve o dump bom com um do meio do jogo */
    }
}
/* DIAGNOSTICO 22/10 rodada 2: o dump fixo em sec==52 so pega o estado certo se o relancamento acontecer bem
 * naquele segundo -- na sessao anterior o jogo foi bem mais longe e o dump saiu no meio do jogo normal, sem
 * valor. Troca por um gatilho por EVENTO: dispara exatamente quando a maquina volta pro estado de boot/reset.
 *
 * CORRECAO 22/10 rodada 3: a 1a versao do gatilho (if (frame > 200 && pc < 0x3000)) deu FALSO POSITIVO logo
 * no frame 201, ainda dentro do boot normal -- o log mostrou PC voltando pra ~0x2500-0x2600 varias vezes
 * durante o jogo JA EM ANDAMENTO (ex.: t=11s, lba=1229, nada errado), porque essa faixa baixa tem rotina(s)
 * de driver (CD) chamada(s) via SWI a partir de codigo de jogo normal, nao e exclusiva do boot. PC<endereco
 * e um sinal ruim: qualquer rotina de baixo nivel reusa esses enderecos a vida toda.
 * Os dois sinais trocados abaixo SO aparecem, nos dois logs ja vistos, no boot/reset de verdade, nunca durante
 * o jogo: cd.S==0x80 (status inicial do CLIO, visto so em "CD> CDT poll_read 80"/"poll_write v=80" nos
 * primeiros ~10 quadros) e PC==0x03000000 (vetor de reset da CPU, visto literalmente so no frame=1). */
static void check_relaunch(unsigned frame) {
    if (!g_rl_left_boot) { if (frame > 20 && g_m.cd.S != 0x80u) g_rl_left_boot = 1; return; }
    if (g_rl_fired) return;
    uint32_t pc = g_m.cpu.r[15];
    if (g_m.cd.S == 0x80u || pc == 0x03000000u) {
        g_rl_fired = 1;
        logf_(RETRO_LOG_INFO, "RELANCAMENTO DETECTADO frame=%u PC=%08x cd.S=%02x cd.lba=%d (gatilho: %s)\n",
              frame, pc, g_m.cd.S, g_m.cd.lba, g_m.cd.S == 0x80u ? "cd.S==0x80" : "PC==0x03000000 (vetor de reset)");
        diag_snapshot("RELOAD");
        diag_dump();
    }
}
/* DIAGNOSTICO (rodada nova): procura uma "linha" no HiRes onde o plano 0 (VRAM, o que o modo 1x mostra)
 * diverge dos 3 planos extras (pixh[0..2]) POR MUITOS PIXELS SEGUIDOS, com os 3 planos extras iguais entre si
 * -- assinatura de "esse trecho nunca foi replicado pros planos do HiRes" (alguem escreveu so no plano 0 depois
 * que os planos extras pararam de ser atualizados), diferente de uma borda normal de sprite rotacionado (que
 * so afeta subamostras espalhadas, nao uma faixa horizontal inteira). So roda com hires ligado; loga no maximo
 * umas poucas vezes por "achado" (nao a cada quadro) pra nao inundar o log. */
static unsigned g_hgap_logged;
static unsigned g_hgap_frame_n;
/* HIRESGAP v2. Assinatura real do defeito (dump 25/10): linha 2x PAR (planos 0 e 1) != linha 2x IMPAR (planos 2 e 3),
 * isto e, p == h0 e h1 == h2 mas p != h1, com diferenca de brilho grande (>= 5 niveis RGB555 somados) para ignorar o
 * padrao alternado legitimo das listras (1 nivel). Corrida minima de 16 px. Varre todo quadro; no maximo 3 achados por quadro. */
static int hg_lum(uint16_t p) { return ((p >> 10) & 31) + ((p >> 5) & 31) + (p & 31); }
static void check_hires_gap(unsigned frame) {
    if (!g_m.vdlp.hires || g_hgap_logged >= 400) return;
    g_hgap_frame_n = 0;
    for (int y = 0; y < g_m.vdlp.lines && g_hgap_frame_n < 3; y++) {
        vdlp_line_t *L = &g_m.vdlp.line[y];
        int run = 0, x0 = 0;
        for (int x = 0; x <= g_m.vdlp.width; x++) {
            int mismatch = 0;
            if (x < g_m.vdlp.width) {
                uint16_t p = L->pix[x], h0 = L->pixh[0][x], h1 = L->pixh[1][x], h2 = L->pixh[2][x];
                mismatch = (p == h0) && (h1 == h2) && (p != h1) && abs(hg_lum(p) - hg_lum(h1)) >= 5;
            }
            if (mismatch) { if (run == 0) x0 = x; run++; }
            else {
                if (run >= 16) {
                    logf_(RETRO_LOG_INFO, "HIRESGAP2 frame=%u y=%d x=%d..%d (%d px) par=%04x impar=%04x clutdma=%08x\n",
                          frame, y, x0, x0 + run - 1, run, (unsigned)L->pix[x0], (unsigned)L->pixh[1][x0], L->clutdma);
                    opera_cel_probe_row(2 * y);   /* passa a vigiar os quads 3D que cruzam essa linha 2x (THINCEL kind=1) */
                    g_hgap_logged++; if (++g_hgap_frame_n >= 3) break;
                }
                run = 0;
            }
        }
    }
}
/* Drena o anel de quads finos (cel_opera.c) e loga; limita o volume (so os primeiros 300). */
static void drain_thin_cels(unsigned frame) {
    static unsigned total[3]; cel_thin_t t[16]; unsigned n;
    while ((n = opera_cel_thin_pop(t, 16)) != 0) for (unsigned i = 0; i < n; i++) {
        if (total[t[i].kind % 3]++ >= (t[i].kind == 2 ? 600u : 300u)) continue;
        if (t[i].kind == 2) { logf_(RETRO_LOG_INFO, "WATCH frame=%u pc=%08x ccb=%06x flags=%08x dst=%06x via=%s X2=%d Y2=%d pix=%04x\n", frame, g_m.cpu.r[15], t[i].ccb, t[i].flags, t[i].regctl3, t[i].x[1] == 1 ? "arb2x" : "1x", t[i].x[0], t[i].y[0], (unsigned)t[i].x[2]); continue; }
        logf_(RETRO_LOG_INFO, "THINCEL%s frame=%u pc=%08x ccb=%06x flags=%08x dst=%06x w=%d h=%d A=(%d,%d) B=(%d,%d) C=(%d,%d) D=(%d,%d)\n",
              t[i].kind ? "-PROBE" : "", frame, g_m.cpu.r[15], t[i].ccb, t[i].flags, t[i].regctl3, t[i].w, t[i].h,
              t[i].x[0], t[i].y[0], t[i].x[1], t[i].y[1], t[i].x[2], t[i].y[2], t[i].x[3], t[i].y[3]);
    }
}
/* v6: relatorio de cobertura por linha 2x (cel_opera.c). Buraco = faixa de 32 px 2x com 0 escritas entre duas linhas vizinhas cobertas. */
static void drain_cov(unsigned frame) {
    static unsigned total; cel_hole_t h[16]; unsigned n = opera_cel_cov_report(h, 16);
    for (unsigned i = 0; i < n && total < 120; i++, total++) {
        logf_(RETRO_LOG_INFO, "COVHOLE frame=%u ccb=%s%06x dst=%06x row2x=%d (%s) faixas=%d primeira_x2=%d quads_vizinhos=%d\n", frame,
              h[i].ccb ? "" : "TODOS/", h[i].ccb, h[i].dst, h[i].row, (h[i].row & 1) ? "impar" : "par", h[i].nbk, h[i].bk0 * 32, h[i].nq);
        for (int k = 0; k < h[i].nq; k++)
            logf_(RETRO_LOG_INFO, "COVQ   A=(%d,%d) B=(%d,%d) C=(%d,%d) D=(%d,%d)\n", h[i].q[k][0], h[i].q[k][4], h[i].q[k][1], h[i].q[k][5], h[i].q[k][2], h[i].q[k][6], h[i].q[k][3], h[i].q[k][7]);
    }
}
static void h_audio_out(void *ud, uint32_t s) {                                                 /* L = 16 bits altos, R = baixos (suposicao: sem DSP ainda) */
    g_aud_ticks++;
    (void)ud;
    if (g_audio_n < SAMPLES_MAX) { g_audio[2 * g_audio_n] = (int16_t)(s >> 16); g_audio[2 * g_audio_n + 1] = (int16_t)s; g_audio_n++; }
}

static void machine_free(void) { if (g_have_machine) { ph_free(&g_m); g_have_machine = 0; } }

/* ------------------------------------------------------------------ controles (PBUS) */
/* Dispositivo escolhido por porta no frontend (RetroArch: Controles > Tipo de dispositivo). Os mesmos 6 tipos do Opera (joybus.c):
 * joypad, flightstick, mouse, lightgun, arcade lightgun e trackball Orbatak; 8 portas. Padrao: 3DO Joypad no P1, desligado nas demais.
 * Guardado aqui porque o frontend pode chamar retro_set_controller_port_device antes do retro_load_game (que zera a maquina). */
static unsigned g_port_dev[JOYBUS_PORTS] = { JOYBUS_DEV_JOYPAD, RETRO_DEVICE_NONE, RETRO_DEVICE_NONE, RETRO_DEVICE_NONE, RETRO_DEVICE_NONE, RETRO_DEVICE_NONE, RETRO_DEVICE_NONE, RETRO_DEVICE_NONE };

/* "Active Input Devices" (0..8), limitado pelo maximo de usuarios do frontend (como o lr_input_get_active_devices do Opera) */
static int active_devices(void) {
    int n = g_active_devices;
    unsigned fe = 0;
    if (n > JOYBUS_PORTS) n = JOYBUS_PORTS;
    if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS, &fe) && fe > 0 && (int)fe < n) n = (int)fe;
    return n;
}

static void apply_port_devices(void) {
    if (!g_have_machine) return;
    const int act = active_devices();
    for (unsigned p = 0; p < JOYBUS_PORTS; p++) {
        joybus_set_device(&g_m.joybus, p, g_port_dev[p]);
        joybus_set_connected(&g_m.joybus, p, g_port_dev[p] != RETRO_DEVICE_NONE && (int)p < act);   /* Active Input Devices: maximo de controles */
    }
}

/* Descricoes dos botoes no menu de controles do RetroArch (mapeamento igual ao do Opera: ver joybus.c) */
typedef struct { unsigned device, index, id; const char *txt; } ph_desc_t;
static const ph_desc_t PH_DESC_JOYPAD[] = {
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "D-Pad Up" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "D-Pad Down" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "D-Pad Left" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "D-Pad Right" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "A" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "B" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "C" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "L (Left Shoulder)" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "R (Right Shoulder)" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "P (Play/Pause)" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "X (Stop)" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "P (Play/Pause)" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "Screen: Normal / Extended" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "Screen: Normal / Extended" },
};
static const ph_desc_t PH_DESC_FLIGHTSTICK[] = {
    { RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, "Horizontal (X)" },
    { RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, "Vertical (Y)" },
    { RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Depth (Z)" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "D-Pad Left" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "D-Pad Up" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "D-Pad Down" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "D-Pad Right" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "A" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "B" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "C" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "L" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "R" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "Fire" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "X (Stop)" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "P (Play/Pause)" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "P (Play/Pause)" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "Screen: Normal / Extended" },
};
static const ph_desc_t PH_DESC_MOUSE[] = {
    { RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X,      "Horizontal Axis" },
    { RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y,      "Vertical Axis" },
    { RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT,   "Left Button" },
    { RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE, "Middle Button" },
    { RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT,  "Right Button" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,   "Screen: Normal / Extended" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,   "Screen: Normal / Extended" },
};
static const ph_desc_t PH_DESC_LIGHTGUN[] = {
    { RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X, "X Coord" },
    { RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y, "Y Coord" },
    { RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER,  "Trigger" },
    { RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SELECT,   "Option" },
    { RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_RELOAD,   "Reload" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,           "Screen: Normal / Extended" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,           "Screen: Normal / Extended" },
};
static const ph_desc_t PH_DESC_ARCADE_LIGHTGUN[] = {
    { RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X, "X Coord" },
    { RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y, "Y Coord" },
    { RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER,  "Trigger" },
    { RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_AUX_A,    "Service" },
    { RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SELECT,   "Coins" },
    { RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_START,    "Start" },
    { RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_RELOAD,   "Holster" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,           "Screen: Normal / Extended" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,           "Screen: Normal / Extended" },
};
static const ph_desc_t PH_DESC_ORBATAK[] = {
    { RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Trackball (Horizontal)" },
    { RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Trackball (Vertical)" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Start (P1)" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Start (P2)" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "Coin (P1)" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "Coin (P2)" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "Service" },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "Screen: Normal / Extended" },
};
#define PH_ND(a) (sizeof(a) / sizeof((a)[0]))

static void set_input_descriptors(void) {
    if (!env_cb) return;
    struct retro_input_descriptor d[JOYBUS_PORTS * 17 + 1];
    unsigned n = 0;
    const int act = active_devices();
    for (unsigned p = 0; p < JOYBUS_PORTS; p++) {
        const ph_desc_t *t; unsigned cnt;
        if (g_port_dev[p] == RETRO_DEVICE_NONE || (int)p >= act) continue;
        switch (g_port_dev[p]) {
        case JOYBUS_DEV_FLIGHTSTICK:     t = PH_DESC_FLIGHTSTICK;     cnt = (unsigned)PH_ND(PH_DESC_FLIGHTSTICK); break;
        case JOYBUS_DEV_MOUSE:           t = PH_DESC_MOUSE;           cnt = (unsigned)PH_ND(PH_DESC_MOUSE); break;
        case JOYBUS_DEV_LIGHTGUN:        t = PH_DESC_LIGHTGUN;        cnt = (unsigned)PH_ND(PH_DESC_LIGHTGUN); break;
        case JOYBUS_DEV_ARCADE_LIGHTGUN: t = PH_DESC_ARCADE_LIGHTGUN; cnt = (unsigned)PH_ND(PH_DESC_ARCADE_LIGHTGUN); break;
        case JOYBUS_DEV_ORBATAK:         t = PH_DESC_ORBATAK;         cnt = (unsigned)PH_ND(PH_DESC_ORBATAK); break;
        default:                         t = PH_DESC_JOYPAD;          cnt = (unsigned)PH_ND(PH_DESC_JOYPAD); break;
        }
        for (unsigned i = 0; i < cnt; i++) {
            d[n].port = p; d[n].device = t[i].device; d[n].index = t[i].index; d[n].id = t[i].id; d[n].description = t[i].txt; n++;
        }
    }
    d[n].description = NULL; d[n].port = d[n].device = d[n].index = d[n].id = 0;
    env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, d);
}

/* Mira desenhada na tela para lightgun / arcade lightgun (como o lr_input_crosshair.c do Opera): cruz de 1 pixel de espessura no
 * pixel apontado + 1 pixel para cada lado; cor por porta. Desenhada direto no quadro de saida, depois da composicao. "Hide Lightgun Crosshairs" esconde. */
static void draw_crosshairs(const lr_layout_t *L) {
    static const uint32_t COLORS[JOYBUS_PORTS] = { 0x000000FFu, 0x00FF0000u, 0x0000FF00u, 0x00FFFFFFu, 0x00FF00FFu, 0x00FFFF00u, 0x0000FFFFu, 0x00000001u };
    if (g_hide_xhair || !g_have_machine || !g_fb || L->ow == 0 || L->oh == 0) return;
    const int w = (int)L->ow, h = (int)L->oh, t = (int)L->s;          /* t: espessura/braco em pixels de saida (acompanha a escala interna) */
    for (unsigned p = 0; p < JOYBUS_PORTS; p++) {
        int32_t sx, sy;
        if (!joybus_crosshair(&g_m.joybus, p, &sx, &sy)) continue;
        const uint32_t c = lr_out_pixel(COLORS[p]);
        int x = (int)((((int64_t)sx + 32768) * w) / 65536), y = (int)((((int64_t)sy + 32768) * h) / 65536);
        if (x < 0) x = 0; else if (x >= w) x = w - 1;
        if (y < 0) y = 0; else if (y >= h) y = h - 1;
        for (int dy = -t; dy <= t; dy++) for (int dx = -t; dx <= t; dx++) {
            if (dx != 0 && dy != 0) continue;                           /* so a cruz */
            const int px = x + dx, py = y + dy;
            if (px < 0 || px >= w || py < 0 || py >= h) continue;
            if (g_fmt != LR_XRGB8888) ((uint16_t *)g_fb)[(size_t)py * (size_t)w + (size_t)px] = (uint16_t)c;
            else                      ((uint32_t *)g_fb)[(size_t)py * (size_t)w + (size_t)px] = c;
        }
    }
}

/* Janela da imagem do 3DO dentro da tela inteira (bordas / fundo expandido), para a mira da lightgun acertar o alvo certo */
static void update_input_view(void) {
    if (!g_have_machine) return;
    lr_layout_t L; video_layout(&L);
    if (L.cw == 0 || L.ch == 0) return;
    joybus_set_view(&g_m.joybus,
                    (int32_t)(((int64_t)L.x0 * 65536) / L.cw) - 32768,
                    (int32_t)(((int64_t)L.y0 * 65536) / L.ch) - 32768,
                    (int32_t)(((int64_t)(L.x0 + L.w) * 65536) / L.cw) - 32768,
                    (int32_t)(((int64_t)(L.y0 + L.h) * 65536) / L.ch) - 32768);
}


/* ------------------------------------------------------------------ SWIs: OperaOS SWI HLE + Debug Output (kprint) */
/* Debug Output: texto do kernel (SWI 0x1000e: caractere OU ponteiro para texto em r1) linha a linha no log/stderr, sem o limite do diagnostico */
static void kprint_swi(uint32_t a) {
    static char line[256]; static unsigned ln;
    if (a < 0x100u) {
        if (a == '\n' || ln >= sizeof line - 1) { line[ln] = 0; if (ln) logf_(RETRO_LOG_INFO, "KPRINT: %s\n", line); ln = 0; }
        else if (a >= 0x20u) line[ln++] = (char)a;
    } else if (a < 0x2fff00u) {
        char t[161]; unsigned k;
        for (k = 0; k < 160; k++) { uint8_t ch = ((const uint8_t *)g_m.ram)[(a + k) ^ 3u]; if (ch == 0) break; t[k] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : (ch == '\n' ? '\n' : '.'); }
        t[k] = 0;
        while (k && (t[k - 1] == '\n')) t[--k] = 0;
        if (k) logf_(RETRO_LOG_INFO, "KPRINT: %s\n", t);
    }
}
static int h_swi(void *u, uint32_t imm) {
    int handled = 0;
    if (g_swi_hle && ph_swi_hle((ph_machine_t *)u, imm)) handled = 1;       /* SWI 0x5xxxx em C (so se a opcao esta ligada) */
    if (!handled && g_debug) handled = diag_swi(u, imm);                    /* diagnostico + HLEs temporarios (so com "Diagnostico no log") */
    if (!handled && g_kprint && imm == 0x1000eu) kprint_swi(g_m.cpu.r[1]);  /* nao consome a SWI: o kernel segue normalmente */
    return handled;
}

/* ------------------------------------------------------------------ opcoes aplicadas na maquina */
static void apply_cd_options(void) {              /* 166666 ciclos/setor = 1x de 75 setores/s; "2x" (83333) e o padrao de fabrica; 0 = sem limite */
    if (!g_have_machine) return;
    g_m.cd.rate = g_cd_speed ? (uint32_t)(166666 / g_cd_speed) : 0u;
    g_m.cd.opera_extra = (uint32_t)g_cd_opera_extra;
}
static void apply_live_options(void) {
    if (!g_have_machine) return;
    apply_cd_options();
    ph_set_cpu_overclock(&g_m, g_overclock);
    vdlp_set_bypass_clut(&g_m.vdlp, g_bypass_clut);
    if (ph_set_hires(&g_m, g_hires) != 0) { logf_(RETRO_LOG_ERROR, "HiRes CEL Rendering: sem memoria para os planos 2x; desligado\n"); g_hires = 0; }
    apply_port_devices();
    set_input_descriptors();
}

/* ------------------------------------------------------------------ NVRAM (NVRAM Storage / NVRAM Version)
 * Com a pasta de saves do frontend, o nucleo guarda a NVRAM em arquivo proprio (como o Opera):
 *   por jogo:       <saves>/phoenix/per_game/<jogo>.<versao>.srm
 *   compartilhada:  <saves>/phoenix/shared/nvram.<versao>.srm
 * "NVRAM Version" e o numero do slot de save. Um .srm antigo do frontend (<saves>/<jogo>.srm) e lido se o slot 0 por jogo ainda nao existe.
 * Sem pasta de saves o nucleo expoe a NVRAM ao frontend (RETRO_MEMORY_SAVE_RAM), como antes. */
static char    g_save_dir[1024], g_game_name[256], g_nvram_file[1400];
static int     g_nvram_self;
static uint8_t g_nvram_disk[PH_NVRAM_BYTES];      /* o que esta gravado no arquivo (para so regravar quando mudar) */
static unsigned g_nvram_tick;

static void ph_mkdirs(const char *path) {
    char t[1400]; size_t n = strlen(path); if (n >= sizeof t) return;
    memcpy(t, path, n + 1);
    for (size_t i = 1; i < n; i++) if (t[i] == '/' || t[i] == '\\') { char c = t[i]; t[i] = 0; ph_mkdir(t); t[i] = c; }
}
static void nvram_set_game_name(const char *path) {
    const char *b = path ? path : "", *p;
    for (p = b; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    snprintf(g_game_name, sizeof g_game_name, "%s", *b ? b : "no_game");
    char *dot = strrchr(g_game_name, '.'); if (dot && dot != g_game_name) *dot = 0;
}
static void nvram_build_path(char *out, size_t cap) {
    if (g_nvram_shared) snprintf(out, cap, "%s/phoenix/shared/nvram.%d.srm", g_save_dir, g_nvram_ver);
    else                snprintf(out, cap, "%s/phoenix/per_game/%s.%d.srm", g_save_dir, g_game_name, g_nvram_ver);
}
static int nvram_read_file(const char *path, uint8_t *dst) {
    ph_file *f = ph_fopen(path, "rb"); if (!f) return 0;
    size_t n = ph_fread(dst, PH_NVRAM_BYTES, f); ph_fclose(f);
    return n == PH_NVRAM_BYTES;
}
/* carrega o slot escolhido para g_nvram_keep (zerada se nao existe) */
static void nvram_load(void) {
    const char *gd = NULL;
    g_nvram_self = 0;
    if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &gd) && gd && gd[0]) { snprintf(g_save_dir, sizeof g_save_dir, "%s", gd); g_nvram_self = 1; }
    if (!g_nvram_self) return;                  /* sem pasta de saves: a NVRAM segue exposta ao frontend e g_nvram_keep nao e mexida */
    memset(g_nvram_keep, 0, sizeof g_nvram_keep);
    nvram_build_path(g_nvram_file, sizeof g_nvram_file);
    if (!nvram_read_file(g_nvram_file, g_nvram_keep) && !g_nvram_shared && g_nvram_ver == 0) {
        char old[1400]; snprintf(old, sizeof old, "%s/%s.srm", g_save_dir, g_game_name);          /* migracao do .srm que o frontend guardava */
        if (nvram_read_file(old, g_nvram_keep)) logf_(RETRO_LOG_INFO, "NVRAM: importada de %s\n", old);
    }
    memcpy(g_nvram_disk, g_nvram_keep, PH_NVRAM_BYTES);
    logf_(RETRO_LOG_INFO, "NVRAM: %s (%s, versao %d)\n", g_nvram_file, g_nvram_shared ? "compartilhada" : "por jogo", g_nvram_ver);
}
static void nvram_flush(void) {                      /* grava se mudou desde a ultima gravacao */
    if (!g_nvram_self) return;
    const uint8_t *cur = g_have_machine ? g_m.nvram : g_nvram_keep;
    if (!memcmp(cur, g_nvram_disk, PH_NVRAM_BYTES)) return;
    ph_mkdirs(g_nvram_file);
    char tmp[1450]; snprintf(tmp, sizeof tmp, "%s.tmp", g_nvram_file);
    ph_file *f = ph_fopen(tmp, "wb");
    if (!f) { logf_(RETRO_LOG_ERROR, "NVRAM: nao consegui gravar %s\n", g_nvram_file); return; }
    size_t n = ph_fwrite(cur, PH_NVRAM_BYTES, f); ph_fclose(f);
    if (n != PH_NVRAM_BYTES) { ph_remove(tmp); return; }
    ph_remove(g_nvram_file);
    if (ph_rename(tmp, g_nvram_file) != 0) { logf_(RETRO_LOG_ERROR, "NVRAM: falha ao renomear %s\n", tmp); return; }
    memcpy(g_nvram_disk, cur, PH_NVRAM_BYTES);
}

static int machine_boot(void) {
    machine_free();
    {   /* "Memory Capacity": valor em hexa, nibble alto = DRAM em MB, baixo = VRAM em MB (0x21 = stock, 0xE2 = 14+2...) */
        unsigned cap = (unsigned)strtoul(g_mem_cap, NULL, 16), dr = cap >> 4, vr = cap & 15u;
        if (!ph_mem_cfg_valid(dr, vr)) { logf_(RETRO_LOG_WARN, "Memory Capacity '%s' invalida; usando 2MB DRAM + 1MB VRAM (stock)\n", g_mem_cap); dr = 2; vr = 1; }
        if (ph_init_mem(&g_m, dr, vr) != 0) { logf_(RETRO_LOG_ERROR, "falha ao alocar RAM/BIOS (%uMB DRAM + %uMB VRAM)\n", dr, vr); return 0; }
        if (dr != 2 || vr != 1) logf_(RETRO_LOG_INFO, "Memory Capacity: %uMB DRAM + %uMB VRAM (nao e stock: precisa de BIOS/software compativel)\n", dr, vr);
    }
    g_have_machine = 1;
    g_m.cel.legacy = g_cel_legacy;
    logf_(RETRO_LOG_INFO, "motor de CEL: %s\n", g_cel_legacy ? "legado (cel.c)" : "Opera (cel_opera.c)");
    if (!find_bios(&g_m)) {
        logf_(RETRO_LOG_ERROR, "BIOS 3DO nao encontrada em '%s' (procurei fz10.bin, fz1.bin, goldstar.bin, sanyotry.bin, panafz10.bin; tambem em 3do/, BIOS/, 3do_phoenix/BIOS/)\n", g_system_dir);
        machine_free(); return 0;
    }
    memcpy(g_m.nvram, g_nvram_keep, PH_NVRAM_BYTES);
    cd_trace_hook = g_debug ? cd_trace_cb : NULL;
    clio_trace_hook = g_debug ? clio_trace_cb : NULL;
    if (g_disc_path[0]) {                       /* CD no XBUS ID 0 (PARTE20): a BIOS enxerga o disco pelos registradores 0x03400500.. do CLIO */
        if (ph_cd_insert(&g_m, g_disc_path) != 0) { logf_(RETRO_LOG_ERROR, "nao consegui ler a imagem: %s\n", g_disc_path); machine_free(); return 0; }
        logf_(RETRO_LOG_INFO, "CD: %s (%u setores de %d bytes)\n", g_disc_path, (unsigned)g_m.cd.num_sectors, CD_SECTOR_SIZE);
    }
    if (g_region && ph_set_region(&g_m, g_region) != 0) { logf_(RETRO_LOG_ERROR, "falha ao criar o VDLP PAL\n"); machine_free(); return 0; }   /* refaz o VDLP (PAL2 384x288 / PAL1 320x288) e poe o CLIO em PAL */
    g_m.clio.vdlp_line = h_vdlp_frame;          /* depois de ph_set_region: fim de quadro (FUN_004788b0) + marca o quadro pronto */
    g_m.clio.audio_out = h_audio_out;
    g_m.vdlp.blit      = h_blit;
    /* Joybus callbacks */
    if (g_m.joybus_set_callbacks) g_m.joybus_set_callbacks(&g_m.joybus, input_poll_cb, input_state_cb);
    apply_port_devices();
    set_input_descriptors();
    memset(g_fb, 0, (size_t)LR_MAX_CW * LR_MAX_SCALE * LR_MAX_CH * LR_MAX_SCALE * 4);
    memset(g_src, 0, sizeof g_src); memset(g_src2, 0, sizeof g_src2);
    g_frame_ready = 0; g_frames = 0;
    g_m.clio.R[0x28 >> 2] = 0x01;              /* CSTAT: bit PON (power-on). A BIOS testa (CSTAT & 0x43) == 1 para o boot a frio; com 0 ela cai num laco de erro (achado da Parte 18) */
    /* PC=0x03000000 e vram_flag=1 devem sair prontos de ph_init() (REA em FUN_004a7f60, rodada 19b). O machine.c que veio no
     * 3do_phoenix.zip ainda NAO tem isso (PC=0, vram_flag=0): se for o caso, o front-end completa o reset aqui, sem alterar o nucleo. */
    if (g_m.cpu.r[15] == 0) { g_m.cpu.r[15] = 0x03000000u; g_m.vram_flag = 1; }
    g_audio_n = 0;
    g_m.cpu.trace = g_debug;
    g_m.cpu.swi_hook = h_swi;                  /* SWI HLE / Debug Output / diagnostico: cada um checa a propria opcao */
    /* opcoes do menu (System / Advanced / Video / Input) */
    ph_set_matrix_software(&g_m, g_matrix_sw);
    ph_set_random_seed(&g_m, g_seed_random ? (uint32_t)time(NULL) * 2654435761u : g_seed);
    apply_live_options();                      /* CD Speed, CPU Overclock, Bypass CLUT, Active Input Devices */
    logf_(RETRO_LOG_INFO, "opcoes: regiao=%s cd=%s cd_extra_opera=%s overclock=%.2fx matriz=%s swi_hle=%s kprint=%s bypass_clut=%s hires=%s devices=%d build=v11 ultima_linha=%s topfix=%d\n",
          g_region == VDLP_MODE_PAL2 ? "PAL2 384x288" : g_region == VDLP_MODE_PAL1 ? "PAL1 320x288" : g_hd ? "NTSC 1024x768" : "NTSC 320x240",
          g_cd_speed ? "limitado" : "unbounded", g_cd_opera_extra ? "on" : "off", g_overclock, g_matrix_sw ? "software" : "hardware", g_swi_hle ? "on" : "off", g_kprint ? "on" : "off", g_bypass_clut ? "on" : "off", g_hires ? "on" : "off", g_active_devices, g_cover_last ? "so-The-Roof" : "desligada", opera_cel_topfix());
    return 1;
}

/* ------------------------------------------------------------------ API libretro */
RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_set_environment(retro_environment_t cb) {
    env_cb = cb;
    bool yes = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &yes);
    ph_file_init(cb);                    /* VFS do frontend (caminhos UTF-8); antes de ph_options_register, que le a pasta system */
    {   /* opcoes no formato v2 do Opera; BIOS/Font listadas a partir do diretorio system */
        const char *sd = NULL; char dir[1024];
        if (cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sd) && sd) snprintf(dir, sizeof dir, "%s", sd); else snprintf(dir, sizeof dir, ".");
        ph_options_register(cb, dir);
    }
    struct retro_log_callback lg;
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &lg)) log_cb = lg.log;
    {   /* tipos de controle por porta: os mesmos 6 do Opera, 8 portas */
        static const struct retro_controller_description pad[] = {
            { "3DO Joypad",        JOYBUS_DEV_JOYPAD },
            { "3DO Flightstick",   JOYBUS_DEV_FLIGHTSTICK },
            { "3DO Mouse",         JOYBUS_DEV_MOUSE },
            { "3DO Lightgun",      JOYBUS_DEV_LIGHTGUN },
            { "Arcade Lightgun",   JOYBUS_DEV_ARCADE_LIGHTGUN },
            { "Orbatak Trackball", JOYBUS_DEV_ORBATAK },
        };
        static const struct retro_controller_info ports[JOYBUS_PORTS + 1] = {
            { pad, 6 }, { pad, 6 }, { pad, 6 }, { pad, 6 }, { pad, 6 }, { pad, 6 }, { pad, 6 }, { pad, 6 }, { NULL, 0 }
        };
        cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);
    }
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)       { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)         { audio_cb = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb)             { input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb)           { input_state_cb = cb; if (g_have_machine) joybus_set_callbacks(&g_m.joybus, input_poll_cb, input_state_cb); }

RETRO_API void retro_init(void) {
    const char *sd = NULL;
    g_system_dir[0] = 0;
    if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sd) && sd) { strncpy(g_system_dir, sd, sizeof g_system_dir - 1); g_system_dir[sizeof g_system_dir - 1] = 0; }
    else strcpy(g_system_dir, ".");
    if (!g_fb) g_fb = calloc((size_t)LR_MAX_CW * LR_MAX_SCALE * LR_MAX_CH * LR_MAX_SCALE, 4);
    memset(g_nvram_keep, 0, sizeof g_nvram_keep);
    g_have_machine = 0;
    machine_set_debug_log(logf_);
}
RETRO_API void retro_deinit(void) {
    machine_free();
    free(g_fb); g_fb = NULL;
}

RETRO_API void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof *info);
    info->library_name     = "Phoenix 3DO";
    info->library_version  = "3.0";
    info->valid_extensions = "iso|chd|cue|bin|img";
    info->need_fullpath    = true;
    info->block_extract    = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info) {
    memset(info, 0, sizeof *info);
    lr_layout_t L; video_layout(&L);                           /* NTSC 320x240, PAL 384x288 (vdlp.c) + bordas / fundo expandido / escala interna */
    info->geometry.base_width   = L.ow;
    info->geometry.base_height  = L.oh;
    info->geometry.max_width    = LR_MAX_CW * LR_MAX_SCALE;   /* teto fixo: as opcoes de imagem mudam a geometria com o jogo rodando */
    info->geometry.max_height   = LR_MAX_CH * LR_MAX_SCALE;
    info->geometry.aspect_ratio = L.aspect;
    /* ARM60 a 12,5 MHz; linha = 0x15a8/7 = 792 ciclos (NTSC, 263 linhas) ou 0x15d7/7 (PAL, 313 linhas): clio.c, FUN_00490890 */
    info->timing.fps         = g_pal ? 12500000.0 / (PAL_LINES * (5591.0 / 7.0)) : 12500000.0 / (NTSC_LINES * 792.0);
    info->timing.sample_rate = AUDIO_RATE;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) {
    if (port >= JOYBUS_PORTS) return;
    g_port_dev[port] = joybus_normalize_device(device);       /* desconhecido = joypad, como o Opera */
    if (g_have_machine) apply_port_devices();
    set_input_descriptors();
}

RETRO_API void retro_reset(void) {
    if (g_have_machine) memcpy(g_nvram_keep, g_m.nvram, PH_NVRAM_BYTES);
    nvram_flush();                              /* grava o slot atual antes de (talvez) trocar NVRAM Storage / Version */
    read_options();
    nvram_load();
    machine_boot();
}

RETRO_API bool retro_load_game(const struct retro_game_info *game) {
    g_nvram_checked = 0;
    g_fill = 0; g_fill_valid = 0; g_fill_shrink = 0;      /* padrao Normal; read_options (abaixo) aplica o "Screen Size" salvo no menu */
    read_options();
    {   /* tenta o formato pedido; se o frontend recusar, cai para XRGB8888, depois RGB565, depois 0RGB1555 */
        static const enum retro_pixel_format pfs[3] = { RETRO_PIXEL_FORMAT_0RGB1555, RETRO_PIXEL_FORMAT_RGB565, RETRO_PIXEL_FORMAT_XRGB8888 };   /* indexado por LR_* */
        static const int lrs[3] = { LR_0RGB1555, LR_RGB565, LR_XRGB8888 };
        static const int order[3][3] = { { 2, 1, 0 }, { 1, 2, 0 }, { 0, 2, 1 } };      /* por LR_* pedido: LR_XRGB8888(0), LR_RGB565(1), LR_0RGB1555(2) */
        int ok = 0;
        for (int k = 0; k < 3 && !ok; k++) {
            int idx = order[g_fmt][k]; enum retro_pixel_format pf = pfs[idx];
            if (env_cb && env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pf)) { g_fmt = lrs[idx]; ok = 1; }
        }
        if (!ok) { logf_(RETRO_LOG_ERROR, "o frontend nao aceita XRGB8888, RGB565 nem 0RGB1555\n"); return false; }
    }
    g_disc_path[0] = 0;
    if (game && game->path && game->path[0]) {
        ph_file *f = ph_fopen(game->path, "rb");
        if (!f) { logf_(RETRO_LOG_ERROR, "nao consegui abrir a imagem: %s\n", game->path); return false; }
        ph_fclose(f);
        strncpy(g_disc_path, game->path, sizeof g_disc_path - 1);
    }
    if (!g_fb) g_fb = calloc((size_t)LR_MAX_CW * LR_MAX_SCALE * LR_MAX_CH * LR_MAX_SCALE, 4);
    nvram_set_game_name(g_disc_path);
    nvram_load();
    return machine_boot() ? true : false;
}

RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) { (void)type; (void)info; (void)num; return false; }
RETRO_API void retro_unload_game(void) {
    if (g_have_machine) { memcpy(g_nvram_keep, g_m.nvram, PH_NVRAM_BYTES); if (g_debug && !g_dumped) diag_dump(); }
    nvram_flush();
    machine_free();
}

static void flush_audio(void) {
    if (g_audio_n) {
        if (audio_batch_cb) audio_batch_cb(g_audio, g_audio_n);
        else if (audio_cb) for (unsigned i = 0; i < g_audio_n; i++) audio_cb(g_audio[2 * i], g_audio[2 * i + 1]);
    }
    g_audio_n = 0;
}

/* DIAGNOSTICO PARTE19b: vigia o VDL. Quando o ponteiro do VDL sai da RAM (curvdl>=0x300000) congela o anel e despeja as ultimas escritas na area do VDL. */
static void vdl_watch_check(unsigned frame) {
    static int dumped = 0;
    if (g_m.bn && !g_m.bprinted && (g_m.bn >= 24 || (g_m.vn && (frame % 30) == 0))) {
        g_m.bprinted = 1;
        logf_(RETRO_LOG_INFO, "BADREAD frame=%u total=%u; primeiras leituras em regiao nao mapeada (distintas por PC/endereco):\n", frame, g_m.bcount);
        for (unsigned i = 0; i < g_m.bn; i++)
            logf_(RETRO_LOG_INFO, "BADREAD #%u pc=%08x lr=%08x addr=%08x (n=%u)\n", i, g_m.bring[i].pc, g_m.bring[i].lr, g_m.bring[i].a, g_m.bring[i].cnt);
    }
    if (g_m.vn && !g_m.vprinted && (g_m.vn >= 24 || (frame % 30) == 0)) {
        g_m.vprinted = 1;
        logf_(RETRO_LOG_INFO, "GARBAGE frame=%u primeiras %u escritas de d6d66728/cd5d0000:\n", frame, g_m.vn);
        for (unsigned i = 0; i < g_m.vn; i++)
            logf_(RETRO_LOG_INFO, "GARBAGE #%u pc=%08x lr=%08x addr=%06x val=%08x r0=%08x r1=%08x r2=%08x r3=%08x\n", i, g_m.vring[i].pc, g_m.vring[i].lr, g_m.vring[i].a, g_m.vring[i].v, g_m.vring[i].r0, g_m.vring[i].r1, g_m.vring[i].r2, g_m.vring[i].r3);
    }
    if (dumped) return;
    if (g_m.vdlp.headvdl && g_m.vdlp.headvdl < g_m.ram_bytes - 0x100u && (g_m.vdlp.curvdl < g_m.ram_bytes)) { g_m.wlo = g_m.vdlp.headvdl; g_m.whi = g_m.vdlp.headvdl + 0x80u; }
    if (g_m.vdlp.curvdl >= g_m.ram_bytes && g_m.whi) {
        unsigned n = g_m.wn < 32 ? g_m.wn : 32, st = g_m.wn < 32 ? 0 : (g_m.wn & 31);
        dumped = 1; g_m.wfreeze = 1;
        logf_(RETRO_LOG_INFO, "VDLWATCH frame=%u VDL fora da RAM: head=%06x cur=%06x; ultimas %u escritas em [%06x,%06x):\n", frame, g_m.vdlp.headvdl, g_m.vdlp.curvdl, n, g_m.wlo, g_m.whi);
        for (unsigned i = 0; i < n; i++) { const unsigned e = (st + i) & 31;
            logf_(RETRO_LOG_INFO, "VDLWATCH #%u pc=%08x lr=%08x addr=%06x val=%08x sz=%u\n", i, g_m.wring[e].pc, g_m.wring[e].lr, g_m.wring[e].a, g_m.wring[e].v, g_m.wring[e].sz); }
        logf_(RETRO_LOG_INFO, "VDLWATCH words em head: %08x %08x %08x %08x %08x %08x %08x %08x\n", g_m.ram[g_m.wlo>>2], g_m.ram[(g_m.wlo>>2)+1], g_m.ram[(g_m.wlo>>2)+2], g_m.ram[(g_m.wlo>>2)+3], g_m.ram[(g_m.wlo>>2)+4], g_m.ram[(g_m.wlo>>2)+5], g_m.ram[(g_m.wlo>>2)+6], g_m.ram[(g_m.wlo>>2)+7]);
    }
}
/* L2 ou R2 (qualquer porta ativa): alterna Screen: Normal <-> Screen: Extended. R2 e do jogo no Flightstick (Fire) e no
 * Orbatak (Service); nessas portas so o L2 vale. So reage na borda de subida (segurar o botao nao fica alternando). */
static void fill_hotkey(void) {
    static int prev;
    static const char *const MSG[2] = { "Screen: Normal", "Screen: Extended" };
    int now = 0;
    if (input_state_cb && g_have_machine) {
        const int act = active_devices();
        for (unsigned p = 0; p < JOYBUS_PORTS && !now; p++) {
            if (g_port_dev[p] == RETRO_DEVICE_NONE || (int)p >= act) continue;
            const int r2_free = g_port_dev[p] != JOYBUS_DEV_FLIGHTSTICK && g_port_dev[p] != JOYBUS_DEV_ORBATAK;
            if (input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2) ||
                (r2_free && input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2))) now = 1;
        }
    }
    if (now && !prev) {
        g_fill = !g_fill; g_fill_valid = 0; g_fill_shrink = 0;
        if (env_cb) {
            struct retro_message m; struct retro_system_av_info av;
            struct retro_variable var;
            m.msg = MSG[g_fill]; m.frames = 120; env_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &m);
            var.key = "phoenix_screen_size"; var.value = g_fill ? "extended" : "normal"; env_cb(RETRO_ENVIRONMENT_SET_VARIABLE, &var);   /* o menu acompanha o botao */
            retro_get_system_av_info(&av); env_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &av.geometry);
        }
    }
    prev = now;
}
RETRO_API void retro_run(void) {
    static unsigned frame_dbg = 0;
    if (!g_nvram_checked && g_have_machine) {   /* antes do 1o quadro (e depois do frontend carregar o .srm): NVRAM em branco/lixo -> formata, senao o BIOS mostra "NVRAM Full" */
        g_nvram_checked = 1;
        if (!getenv("PH_SKIPFMT") && !ph_nvram_formatted(g_m.nvram)) { ph_nvram_format(g_m.nvram); logf_(RETRO_LOG_INFO, "NVRAM em branco: formatada (volume 3DO vazio)\n"); }
    }
    {   /* opcoes de imagem alteradas no menu: relê e avisa o frontend da nova geometria (sem reiniciar) */
        bool upd = false;
        if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &upd) && upd) {
            read_video_options();
            read_live_options();                    /* CD Speed, CPU Overclock, Bypass CLUT, SWI HLE, Debug Output, Active Input Devices valem na hora */
            apply_live_options();
            struct retro_system_av_info av; retro_get_system_av_info(&av);
            if (env_cb) env_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &av.geometry);
        }
    }
    if (input_poll_cb) input_poll_cb();
    fill_hotkey();
    /* O RetroArch pode definir input_state/input_poll DEPOIS do retro_load_game: a copia guardada no boot ficava NULL e o pacote PBUS
     * nunca mudava. Repassa os callbacks atuais ao joybus a cada quadro. */
    if (g_have_machine) { joybus_set_callbacks(&g_m.joybus, input_poll_cb, input_state_cb); update_input_view(); }
    if (g_m.joybus_poll) g_m.joybus_poll(&g_m.joybus);
    {   /* diagnostico bruto: estado de TODOS os botoes do RetroPad (portas 1 e 2) + teclado, quando muda, e a cada 600 quadros */
        static unsigned lastraw[2] = {0, 0}, lastk = 0, nraw = 0;
        unsigned raw[2] = {0, 0}, k = 0;
        if (input_state_cb) {
            for (unsigned pt = 0; pt < 2; pt++) for (unsigned id = 0; id < 16; id++)
                if (input_state_cb(pt, RETRO_DEVICE_JOYPAD, 0, id)) raw[pt] |= 1u << id;
            k = (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_RETURN) ? 1u : 0) | (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_UP) ? 2u : 0) |
                (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_z) ? 4u : 0) | (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_x) ? 8u : 0);
        }
        if (g_debug && g_have_machine && (((raw[0] != lastraw[0] || raw[1] != lastraw[1] || k != lastk) && nraw < 300) || frame_dbg % 600 == 0)) {
            lastraw[0] = raw[0]; lastraw[1] = raw[1]; lastk = k; nraw++;
            logf_(RETRO_LOG_INFO, "[pad] RAW retropad P1=%04x P2=%04x kbd(Enter,Up,Z,X)=%x state_cb=%s poll_cb=%s frame=%u",
                  raw[0], raw[1], k, input_state_cb ? "ok" : "NULL", input_poll_cb ? "ok" : "NULL", frame_dbg);
        }
    }
    {   /* diagnostico: registra no log do RetroArch cada mudanca do pacote PBUS (so quando algum botao muda; limitado a 300 linhas) */
        static uint8_t last[16] = {0}; static unsigned nlog = 0;
        if (g_debug && g_have_machine && memcmp(last, g_m.joybus.buf, 16) && nlog < 300) {
            memcpy(last, g_m.joybus.buf, 16); nlog++;
            logf_(RETRO_LOG_INFO, "[pad] PBUS: %02x %02x %02x %02x %02x %02x %02x %02x (dev P1=%x P2=%x) frame=%u",
                  last[0], last[1], last[2], last[3], last[4], last[5], last[6], last[7], g_port_dev[0], g_port_dev[1], frame_dbg);
        }
    }
    
    if (g_have_machine) {
        frame_dbg++;
        if (g_debug && (frame_dbg <= 10 || frame_dbg % 60 == 0)) {
            logf_(RETRO_LOG_INFO, "retro_run: frame=%u PC=%08x IRQ0=%08x EN0=%08x cdS=%02x cdR=%02x lba=%d remain=%d",
                  frame_dbg, g_m.cpu.r[15], g_m.clio.R[0x40 >> 2], g_m.clio.R[0x48 >> 2],
                  g_m.cd.S, g_m.cd.R, g_m.cd.lba, g_m.cd.remain);
        }
    }
    
    unsigned w = (g_region == VDLP_MODE_PAL2) ? 384 : 320, h = g_pal ? 288 : 240;
    if (g_have_machine) {
        w = (unsigned)g_m.vdlp.width; h = (unsigned)g_m.vdlp.lines;
        /* roda ate o VDLP fechar o quadro visivel; teto de 2,2 quadros de ciclos para nao travar se a CPU/linhas pararem */
        uint32_t frame_cyc = g_pal ? (uint32_t)(PAL_LINES * 799) : (uint32_t)(NTSC_LINES * 792);
        uint32_t cap = (uint32_t)((double)frame_cyc * 2.2 * (g_overclock > 1.0 ? g_overclock : 1.0)), spent = 0;   /* com overclock a CPU gasta mais ciclos por quadro */
        g_frame_ready = 0;
        while (!g_frame_ready && spent < cap) {
            uint32_t t0 = g_m.total_cycles;
            ph_run(&g_m, 1024);
            spent += g_m.total_cycles - t0;
        }
    }
    if (g_debug && g_have_machine) vdl_watch_check(frame_dbg);
    if (g_debug && g_have_machine) check_relaunch(frame_dbg);     /* checa TODO quadro, nao so 1x/s: pega o instante exato do relancamento */
    opera_cel_cov_enable(g_debug && g_have_machine && g_m.hires);
    if (g_debug && g_have_machine) { check_hires_gap(frame_dbg); drain_thin_cels(frame_dbg); drain_cov(frame_dbg); }    /* HIRESGAP: faixa que so aparece no HiRes (plano 0 != planos 1..3) */
    if (video_cb) {
        lr_layout_t L; video_layout(&L);
        if (g_have_machine) { video_compose(&L, w, h); draw_crosshairs(&L); }
        size_t bpp = (g_fmt != LR_XRGB8888) ? 2 : 4;
        video_cb(g_have_machine ? g_fb : NULL, L.ow, L.oh, L.ow * bpp);
    }
    flush_audio();
    if (g_have_machine && ++g_nvram_tick % 600 == 0) nvram_flush();     /* a cada ~10 s grava a NVRAM se mudou */
    if (g_debug && g_have_machine) {         /* diagnostico: uma linha STAT por segundo (ate 240) */
        static unsigned nrun, nlog;
        if (++nrun % 60 == 0 && nlog < 240) { nlog++; diag_second(nrun / 60); }
    }
}

RETRO_API size_t retro_serialize_size(void)                       { return g_have_machine ? ph_state_size(&g_m) : 0; }
RETRO_API bool   retro_serialize(void *data, size_t size)         { return g_have_machine && ph_state_save(&g_m, data, size) == 0; }
RETRO_API bool   retro_unserialize(const void *data, size_t size) {
    if (!g_have_machine || ph_state_load(&g_m, data, size) != 0) return false;
    g_audio_n = 0; g_frame_ready = 0;                        /* os ganchos do front-end sobrevivem ao load (ph_state_load religa o host) */
    apply_live_options();                                    /* as opcoes do menu valem sobre o que veio no estado */
    return true;
}
RETRO_API void   retro_cheat_reset(void) {}
RETRO_API void   retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
RETRO_API unsigned retro_get_region(void)                         { return g_pal ? RETRO_REGION_PAL : RETRO_REGION_NTSC; }

/* RAM exposta ao frontend (conquistas/RetroAchievements, cheats por endereco, depuradores), no mesmo leiaute do Opera:
 *   SYSTEM_RAM = DRAM de 2 MB (enderecos 3DO 0x000000..0x1fffff); VIDEO_RAM = VRAM de 1 MB (plano 0, logo depois da DRAM na memoria do nucleo).
 * Cada palavra de 32 bits guarda o valor big-endian do 3DO como inteiro nativo, entao o byte do endereco A esta em ((uint8_t *)ptr)[A ^ 3]
 * (host little-endian), exatamente como o DRAM[addr ^ 3] do Opera: as tabelas de enderecos feitas para o Opera valem aqui. "Memory Capacity"
 * muda o tamanho (DRAM e VRAM seguem a opcao). Antes de carregar o jogo nao ha maquina: devolve NULL / 0. */
RETRO_API void *retro_get_memory_data(unsigned id) {
    if (id == RETRO_MEMORY_SAVE_RAM && !g_nvram_self) return g_have_machine ? (void *)g_m.nvram : (void *)g_nvram_keep;   /* com arquivo proprio (NVRAM Storage/Version) o frontend nao grava .srm */
    if (g_have_machine && g_m.ram) {
        if (id == RETRO_MEMORY_SYSTEM_RAM) return g_m.ram;
        if (id == RETRO_MEMORY_VIDEO_RAM)  return (uint8_t *)g_m.ram + g_m.dram_bytes;
    }
    return NULL;
}
RETRO_API size_t retro_get_memory_size(unsigned id) {
    if (id == RETRO_MEMORY_SAVE_RAM && !g_nvram_self) return PH_NVRAM_BYTES;
    if (g_have_machine && g_m.ram) {
        if (id == RETRO_MEMORY_SYSTEM_RAM) return g_m.dram_bytes;
        if (id == RETRO_MEMORY_VIDEO_RAM)  return g_m.vram_bytes;
    }
    return 0;
}
