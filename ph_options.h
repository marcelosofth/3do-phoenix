/* ph_options.h - opcoes de nucleo do Phoenix no mesmo formato e com os mesmos nomes/textos do Opera (Configuracoes do nucleo:
 * System, Video, Input, Advanced). Inclui a deteccao de BIOS/Font em disco, no estilo do registro de BIOS do Opera
 * (libopera/opera_bios.c): cada arquivo e identificado pelo CRC32 e aparece com o nome da maquina; o que nao for reconhecido
 * aparece com o nome do arquivo. Incluido por libretro.c (tudo static, sem arquivo .c novo para o Makefile do Batocera).
 *
 * Os textos em ingles sao os do Opera (libretro_core_options.c, ISC License, (c) 2021 Antonio SJ Musumeci). Onde o nucleo do
 * Phoenix ainda nao implementa o recurso, a descricao comeca com "[Phoenix: ainda sem efeito]" - a opcao existe (mesma chave,
 * mesmos valores) para o menu ficar igual ao do Opera, mas o nucleo so avisa no log e segue com o comportamento de sempre. */
#ifndef PH_OPTIONS_H
#define PH_OPTIONS_H
#include "libretro.h"
#include "ph_file.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* pastas do diretorio "system" onde se procura BIOS/Font (a mesma lista de sempre do Phoenix) */
static const char *const PH_SUBDIRS[] = { "", "3do/", "BIOS/", "3do_phoenix/BIOS/", "Phoenix/" };
#define PH_N_SUBDIRS (sizeof PH_SUBDIRS / sizeof *PH_SUBDIRS)

/* ------------------------------------------------------------------ registro de BIOS / Font (CRC32 do Opera) */
typedef struct { const char *file; uint32_t crc; const char *label; } ph_romdb_t;
static const ph_romdb_t PH_BIOS_DB[] = {
    { "panafz1.bin",               0xc8c8ff89u, "Panasonic FZ-1 (U)" },
    { "panafz1j.bin",              0xd9493adcu, "Panasonic FZ-1 (J)" },
    { "panafz1j-norsa.bin",        0x82ce67c6u, "Panasonic FZ-1 (J) [No RSA]" },
    { "panafz10.bin",              0x58242ceeu, "Panasonic FZ-10 (U)" },
    { "panafz10-norsa.bin",        0x230e6febu, "Panasonic FZ-10 (U) [No RSA]" },
    { "panafz10e-anvil.bin",       0x2495c500u, "Panasonic FZ-10 (E) ANVIL" },
    { "panafz10e-anvil-norsa.bin", 0x9a186221u, "Panasonic FZ-10 (E) ANVIL [No RSA]" },
    { "goldstar.bin",              0xb6f5028bu, "Goldstar GDO-101M" },
    { "sanyotry.bin",              0xd5cbc509u, "Sanyo Try IMP-21J" },
    /* nomes que o Phoenix sempre procurou (o CRC decide o rotulo; sem CRC conhecido aparece o nome do arquivo) */
    { "fz1.bin", 0u, NULL }, { "fz10.bin", 0u, NULL },
    /* BIOS do 3DO Arcade (512 KB, sem font ROM): so entra no "auto" se nao houver nenhuma BIOS de console, por isso fica por ultimo */
    { "3do_arcade_saot.bin",       0xb832da9au, "3DO Arcade - SAOT" },
};
static const ph_romdb_t PH_FONT_DB[] = {
    { "panafz1-kanji.bin",           0xa8e9447cu, "Panasonic FZ-1 (J) Kanji ROM" },
    { "panafz1j-kanji.bin",          0x45f478b1u, "Panasonic FZ-1 (J) Kanji ROM" },
    { "panafz10ja-anvil-kanji.bin",  0xff7393deu, "Panasonic FZ-10 (J) ANVIL Kanji ROM" },
    { "kanji.bin", 0u, NULL }, { "fontrom.bin", 0u, NULL }, { "font.bin", 0u, NULL }, { "fonts.bin", 0u, NULL },
    { "kanjifont.bin", 0u, NULL }, { "3do_font.bin", 0u, NULL }, { "fz1-font.bin", 0u, NULL },
};
#define PH_N(a) (sizeof (a) / sizeof *(a))
/* FONT-ROMs incorporadas ao nucleo (ph_fontdata.h): sempre aparecem em Font (rom2), sem precisar de arquivo na pasta system.
 * Um arquivo de mesmo nome na pasta system tem prioridade (try_font tenta o disco antes); o embutido e o reserva. */
#include "ph_fontdata.h"
typedef struct { const char *file; const uint8_t *data; size_t size; uint32_t crc; const char *label; } ph_embfont_t;
static const ph_embfont_t PH_EMBFONTS[] = {
    { "panafz1-kanji.bin",          ph_fontdata_panafz1_kanji,         sizeof ph_fontdata_panafz1_kanji,         0xa8e9447cu, "Panasonic FZ-1 (J) Kanji ROM (Internal)" },
    { "panafz10ja-anvil-kanji.bin", ph_fontdata_panafz10ja_anvil_kanji, sizeof ph_fontdata_panafz10ja_anvil_kanji, 0xff7393deu, "Panasonic FZ-10 (J) ANVIL Kanji ROM (Internal)" },
};
static const ph_embfont_t *ph_embfont_find(const char *name) {
    for (unsigned i = 0; i < sizeof PH_EMBFONTS / sizeof *PH_EMBFONTS; i++) if (!strcmp(PH_EMBFONTS[i].file, name)) return &PH_EMBFONTS[i];
    return NULL;
}
/* BIOS incorporadas ao nucleo (ph_biosdata.h): sempre aparecem em BIOS (rom1). Mesmo esquema das fontes: arquivo de mesmo nome na pasta
 * system tem prioridade; o embutido e o reserva. Os vetores de ph_biosdata.h tem 1 byte a mais (NUL do literal): tamanho = sizeof - 1. */
#include "ph_biosdata.h"
#define PH_EMBBIOS_ROW(f, sym, c, lab) { f, ph_biosdata_##sym, sizeof ph_biosdata_##sym - 1, c, lab " (Internal)" }
static const ph_embfont_t PH_EMBBIOS[] = {      /* mesma struct das fontes; o rotulo e o do registro do Opera */
    PH_EMBBIOS_ROW("panafz1.bin",                 panafz1,               0xc8c8ff89u, "Panasonic FZ-1 (U)"),
    PH_EMBBIOS_ROW("panafz10.bin",                panafz10,              0x58242ceeu, "Panasonic FZ-10 (U)"),
    PH_EMBBIOS_ROW("panafz10-norsa.bin",          panafz10_norsa,        0x230e6febu, "Panasonic FZ-10 (U) [No RSA]"),
    PH_EMBBIOS_ROW("panafz10e-anvil.bin",         panafz10e_anvil,       0x2495c500u, "Panasonic FZ-10 (E) ANVIL"),
    PH_EMBBIOS_ROW("panafz10e-anvil-norsa.bin",   panafz10e_anvil_norsa, 0x9a186221u, "Panasonic FZ-10 (E) ANVIL [No RSA]"),
    PH_EMBBIOS_ROW("goldstar.bin",                goldstar,              0xb6f5028bu, "Goldstar GDO-101M"),
    PH_EMBBIOS_ROW("sanyotry.bin",                sanyotry,              0xd5cbc509u, "Sanyo Try IMP-21J"),
    PH_EMBBIOS_ROW("3do_arcade_saot.bin",         arcade_saot,           0xb832da9au, "3DO Arcade - SAOT"),
};
static const ph_embfont_t *ph_embbios_find(const char *name) {
    for (unsigned i = 0; i < sizeof PH_EMBBIOS / sizeof *PH_EMBBIOS; i++) if (!strcmp(PH_EMBBIOS[i].file, name)) return &PH_EMBBIOS[i];
    return NULL;
}
#define PH_BIOS_ARCADE_BYTES 0x80000u            /* BIOS do 3DO Arcade: 512 KB; o resto do banco de 1 MB fica zerado, como no Opera */
static inline int ph_bios_size_ok(size_t n) { return n == PH_BIOS_ARCADE_BYTES || n == 0x100000 || n == 0x200000; }

static uint32_t ph_crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffffu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u))); }
    return ~c;
}
static uint8_t *ph_slurp(const char *path, size_t *n) { return ph_read_all(path, 0x400000, n); }   /* VFS/UTF-8, ver ph_file.h */

/* opcoes dinamicas: lista de ROMs achadas (valor = nome do arquivo, rotulo = nome da maquina) */
#define PH_MAX_ROMS 16
typedef struct { char file[64]; char label[96]; uint32_t crc; } ph_rom_t;
static ph_rom_t g_bios_list[PH_MAX_ROMS], g_font_list[PH_MAX_ROMS];
static unsigned g_n_bios, g_n_font;

static int ph_rom_known(const ph_rom_t *l, unsigned n, uint32_t crc) { for (unsigned i = 0; i < n; i++) if (l[i].crc == crc) return 1; return 0; }
static void ph_scan_roms(const char *sysdir, const ph_romdb_t *db, unsigned ndb, int is_font, ph_rom_t *out, unsigned *nout) {
    *nout = 0;
    for (unsigned i = 0; i < ndb && *nout < PH_MAX_ROMS; i++) {
        for (unsigned s = 0; s < PH_N_SUBDIRS; s++) {
            char p[1400]; snprintf(p, sizeof p, "%s/%s%s", sysdir, PH_SUBDIRS[s], db[i].file);
            size_t n = 0; uint8_t *b = ph_slurp(p, &n);
            if (!b) continue;
            int ok = is_font ? (n == 0x100000 || n == 933636) : ph_bios_size_ok(n);
            uint32_t crc = ph_crc32(b, n); free(b);
            if (!ok) continue;
            if (ph_rom_known(out, *nout, crc)) break;                /* mesmo conteudo ja listado com outro nome */
            ph_rom_t *r = &out[(*nout)++];
            snprintf(r->file, sizeof r->file, "%s", db[i].file); r->crc = crc;
            const char *lab = NULL;
            const ph_romdb_t *all = db;
            for (unsigned k = 0; k < ndb; k++) if (all[k].crc && all[k].crc == crc) { lab = all[k].label; break; }
            if (!lab) lab = db[i].label;                              /* nome conhecido, CRC nao (ex.: goldstar.bin) */
            if (lab) snprintf(r->label, sizeof r->label, "%s", lab);
            else snprintf(r->label, sizeof r->label, "%s", db[i].file);   /* nao reconhecida: mostra o arquivo */
            break;
        }
    }
}

/* Junta as ROMs incorporadas a lista achada no disco. Mesmo CRC no disco: a entrada do disco vira a embutida (mesmo conteudo, aparece uma vez,
 * como "(Internal)"); senao a embutida e acrescentada. Valores salvos de outros nomes de arquivo continuam carregando (find_bios/load_font). */
static void ph_merge_embedded(const ph_embfont_t *e, unsigned ne, ph_rom_t *l, unsigned *n) {
    for (unsigned i = 0; i < ne; i++) {
        unsigned k = 0; while (k < *n && l[k].crc != e[i].crc) k++;
        if (k == *n) { if (*n >= PH_MAX_ROMS) continue; (*n)++; }
        snprintf(l[k].file, sizeof l[k].file, "%s", e[i].file); snprintf(l[k].label, sizeof l[k].label, "%s", e[i].label); l[k].crc = e[i].crc;
    }
}

/* ------------------------------------------------------------------ tabela de opcoes
 * valores: "valor=rotulo|valor=rotulo|..." (sem '=' o rotulo e o proprio valor); o 1o do "def" e o padrao.
 * Categorias: as do Opera (o Phoenix nao tem opcoes de Audio ainda: o DSP e codigo novo). */
#define NOEFFECT "[Phoenix: ainda sem efeito] "   /* aviso no menu: so existe 2 MB DRAM + 1 MB VRAM, como o phoenix.exe (a opcao fica igual a do Opera) */
typedef struct { const char *key, *desc, *info, *cat, *vals, *def; } ph_optdef_t;

#define V_ONOFF "disabled|enabled"
static const ph_optdef_t PH_OPTS[] = {
    /* ---- System */
    { "phoenix_bios", "BIOS (rom1)",
      "Select system BIOS. The BIOS ROMs built into the core are always listed, plus any others present in the system directory. Changes take effect at core start/restart.",
      "system", NULL /* dinamico */, NULL },
    { "phoenix_font", "Font (rom2)",
      "Select 'font rom'. Required by some Japanese games, otherwise optional. The font ROMs built into the core are always listed, plus any others present in the system directory. Changes take affect at core start/restart.",
      "system", NULL /* dinamico */, NULL },
    { "phoenix_cd_speed", "CD Speed",
      "Select the effective CD-ROM read speed. Warning: can expose timing issues in some titles. Particularly above 16x.",
      "system", "unbounded|1x= 1x - ~150KB/s|2x= 2x - ~300KB/s (stock)|4x= 4x - ~600KB/s|8x= 8x - ~1200KB/s|16x=16x - 2400KB/s|24x=24x - 3600KB/s|32x=32x - 4800KB/s", "2x" },
    { "phoenix_region", "Region / Mode",
      "Select the resolution and field rate. NTSC 1024x768 runs the normal NTSC 60 Hz machine but delivers a 4:3 1024x768 picture (3.2x scale; the internal scale option is ignored). NOTE: some EU games require a EU ROM. Changes take effect at core start/restart.",
      "system", "ntsc=NTSC 320x240@60|pal1=PAL1 320x288@50|pal2=PAL2 384x288@50|ntsc_hd=NTSC 1024x768@60", "ntsc" },
    { "phoenix_nvram_storage", "NVRAM Storage",
      "Selects whether NVRAM saves should be created per-game or as a single NVRAM file shared between all games. Changes take effect at core start/restart.",
      "system", "per game=Per Game|shared=Shared", "per game" },
    { "phoenix_nvram_version", "NVRAM Version",
      "Select the NVRAM version. Each version is a separate save slot. Changes take effect at core start/restart.",
      "system", "0|1|2|3|4|5|6|7|8|9", "0" },
    /* ---- Video */
    { "phoenix_pixel_format", "VDLP Pixel Format",
      "Select the pixel format to request from the runtime and convert to from the internal 16bpp format. Changes take affect at core start.",
      "video", "0RGB1555|RGB565|XRGB8888", "XRGB8888" },
    { "phoenix_vdlp_bypass_clut", "VDLP Bypass CLUT",
      "Force 3DO VDLP to bypass the game's CLUT (color lookup table). May result in incorrect colors but faster rendering.",
      "video", V_ONOFF, "disabled" },
    { "phoenix_high_resolution", "HiRes CEL Rendering (640x480)",
      "Enabling this option causes CELs to be rendered at 2x the resolution into a 2x output framebuffer, increasing the fidelity of 3D models. Will not affect 2D games. Has a significant performance impact. Applies immediately.",
      "video", V_ONOFF, "disabled" },
    { "phoenix_keep_aspect", "Keep Screen Aspect Ratio (4:3)", "Keep screen aspect ratio. Applies immediately.", "video", "on|off", "on" },
    { "phoenix_expand_bg", "Expand Background (16:9 Widescreen)", "Expand background. Applies immediately.", "video", "off|on", "off" },
    { "phoenix_borders", "Show Borders", "Show borders. Applies immediately.", "video", "off|on", "off" },
    { "phoenix_roof_fix", "Fix Way Of The Warrior (Stage The Roof)",
      "Enabled (default): covers the stray sky line that leaks into the last row, only in the 'The Roof' stage (rooftop with dragons) of Way of the Warrior; the core recognizes the stage from the frame and does not change any other stage or game. Disabled: never covers. Applies immediately.",
      "video", "on|off", "on" },
    { "phoenix_image_filter", "Image Filter", "Image filter (software). Applies immediately.", "video", "off|linear|shader_std", "off" },
    { "phoenix_fbo_scale", "Internal Scale (FBO)", "Internal scale. Applies immediately.", "video", "1x|2x|3x|4x", "1x" },
    { "phoenix_screen_size", "Screen Size",
      "Normal: the picture exactly as the game draws it. Extended: crops the black bars the game leaves around its picture and scales the rest to fill the whole frame with sharp (non-blurred) pixels; the internal scale is raised to 4x while active. Can also be toggled in-game with L2 or R2. Applies immediately.",
      "video", "normal=Normal|extended=Extended", "normal" },
    /* ---- Input */
    { "phoenix_active_devices", "Active Input Devices",
      "Sets the maximum number of emulated input devices. Certain games, particularly when using >16x CDROM speed and have more than 1 emulated controller, causes content to ignore gamepad input or crash. Setting 'Active Input Devices' to 1 can be workaround for this issue on some titles.",
      "input", "0|1|2|3|4|5|6|7|8", "1" },
    { "phoenix_hide_lightgun_crosshairs", "Hide Lightgun Crosshairs",
      "Hides lightgun crosshairs for all players. Applies immediately.",
      "input", V_ONOFF, "disabled" },
    /* ---- Advanced */
    { "phoenix_cpu_overclock", "CPU Overclock",
      "Increase clock speed of the emulated 3DO's 12.5MHz ARM60 CPU. Dramatically improves frame rates in many games, but increases performance requirements and in some cases has no effect (or may cause glitches).",
      "advanced", "0.5x ( 6.25Mhz)|0.6x ( 7.50Mhz)|0.7x ( 8.75Mhz)|0.8x (10.00Mhz)|0.9x (11.25Mhz)|1.0x (12.50Mhz)|1.1x (13.75Mhz)|1.2x (15.00Mhz)|1.5x (18.75Mhz)|1.6x (20.00Mhz)|1.8x (22.50Mhz)|2.0x (25.00Mhz)|3.0x (37.50Mhz)|4.0x (50.00Mhz)",
      "1.0x (12.50Mhz)" },
    { "phoenix_mem_capacity", "Memory Capacity",
      "Select the amount of DRAM and VRAM the system has. Intended for compatible homebrew; non-stock capacities require compatible BIOS and software. Changes take effect at core start/restart. !EXPERIMENTAL!",
      "advanced", "21=2MB DRAM; 1MB VRAM (stock)|22=2MB DRAM; 2MB VRAM|41=4MB DRAM; 1MB VRAM|42=4MB DRAM; 2MB VRAM|81=8MB DRAM; 1MB VRAM|82=8MB DRAM; 2MB VRAM|E2=14MB DRAM; 2MB VRAM|F1=15MB DRAM; 1MB VRAM", "21" },
    { "phoenix_random_seed", "Random Number Generator Seed",
      "Select the initial seed for the emulated 3DO random number generators. Fixed seeds provide repeatable behavior for testing. Changes take effect after fully reinitializing the core.",
      "advanced", "random=Random (time-based)|0x00000000|0x00000001|0xdeadbeef|0xcafebabe|0xfeedface|0xbaadf00d|0x8badf00d|0xdeadc0de|0xc001d00d|0x0badf00d|0x74726170|0x65786974", "0x00000000" },
    { "phoenix_madam_matrix_engine", "MADAM Matrix Engine",
      "'MADAM' is the 3DO's graphics accelerator. It contains a custom maths co-processor, used by the 3DO's CPU to offload matrix operations. This corresponds to running in 'Hardware' - but the 3DO could also utilise a built-in ARM 'Software' version of the MADAM matrix engine. 'Hardware' mode is the default, but it has been observed that some games run faster when forcing 'Software' mode. Changes take affect at core start/restart.",
      "advanced", "hardware=Hardware|software=Software", "hardware" },
    { "phoenix_cd_opera_extra_cmds", "CD: Extra Commands (Opera)",
      "Disabled (default): the CD-ROM drive only handles the commands the original phoenix.exe handles (confirmed "
      "by disassembling the executable's jump table). Enabled: also responds to SEEK (01), DIAGNOSTICS (04), "
      "INJECT_DISC (07), RESET (0A), PLAY_AUDIO_MSF/TRACK (0E/0F), READ_HEADER (86), READ_UPC (88), READ_ISRC (89) "
      "and READ_DEVICE_DRIVER (8E), the same way opera-libretro does -- no known game depends on this; only turn "
      "it on if a real case comes up. Applies immediately.",
      "advanced", V_ONOFF, "disabled" },
    { "phoenix_swi_hle", "OperaOS SWI HLE",
      "3DO games are built on top of Portfolio OS. Opera has high level emulation of certain OS functions. This HLE may improve performance in on certain games.",
      "advanced", V_ONOFF, "disabled" },
    { "phoenix_kprint", "Debug Output",
      "Print 3DO debug port output to stderr. Only really useful to 3DO homebrew developers.",
      "advanced", V_ONOFF, "disabled" },
    { "phoenix_cel_engine", "Motor de CEL (reiniciar)", "CEL engine: Opera's (default) or the old cel.c, for A/B comparison. Changes take effect at core start/restart.", "advanced", "opera=Opera|legacy=Phoenix (Not work HiRes Cel Rendering)", "opera" },
    { "phoenix_debug", "Diagnostico no log (reiniciar)", "Phoenix diagnostics in the log (traces, SWI ring, CD). Changes take effect at core start/restart.", "advanced", "off|on", "off" },
};
#define PH_N_OPTS PH_N(PH_OPTS)

/* categorias (o Opera tem tambem "audio" - so com DSP em thread - que o Phoenix nao tem) */
static struct retro_core_option_v2_category g_cats[] = {
    { "system",   "System 3DO",       NULL },
    { "video",    "Video Graphic",    NULL },
    { "input",    "Input Command",    NULL },
    { "advanced", "Advanced Options", NULL },
    { NULL, NULL, NULL },
};

static struct retro_core_option_v2_definition g_defs[PH_N_OPTS + 1];
static char        g_valtxt[PH_N_OPTS][1600];            /* copia editavel de cada lista de valores */
static struct retro_variable g_legacy[PH_N_OPTS + 1];
static char        g_legacy_txt[PH_N_OPTS][2200];
static char        g_default_bios[64], g_default_font[64];

/* preenche values[] a partir de "v=rotulo|v=rotulo"; devolve o numero de valores */
static unsigned ph_parse_values(char *txt, struct retro_core_option_value *out) {
    unsigned n = 0; char *p = txt;
    while (p && n < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1) {
        char *bar = strchr(p, '|'); if (bar) *bar = 0;
        char *eq = strchr(p, '=');
        if (eq) { *eq = 0; out[n].value = p; out[n].label = eq + 1; } else { out[n].value = p; out[n].label = NULL; }
        n++; p = bar ? bar + 1 : NULL;
    }
    out[n].value = NULL; out[n].label = NULL;
    return n;
}
static void ph_dyn_values(char *dst, size_t cap, const ph_rom_t *l, unsigned n, int with_disabled) {
    dst[0] = 0; size_t o = 0;
    if (with_disabled) o += (size_t)snprintf(dst + o, cap - o, "disabled");
    for (unsigned i = 0; i < n && o + 200 < cap; i++) o += (size_t)snprintf(dst + o, cap - o, "%s%s=%s", o ? "|" : "", l[i].file, l[i].label);
    if (!o) snprintf(dst, cap, "disabled");
}

/* monta e registra as opcoes. v2 se o frontend suporta; senao o formato antigo (SET_VARIABLES) */
static void ph_options_register(retro_environment_t cb, const char *sysdir) {
    ph_scan_roms(sysdir, PH_BIOS_DB, PH_N(PH_BIOS_DB), 0, g_bios_list, &g_n_bios);
    ph_merge_embedded(PH_EMBBIOS, sizeof PH_EMBBIOS / sizeof *PH_EMBBIOS, g_bios_list, &g_n_bios);
    ph_scan_roms(sysdir, PH_FONT_DB, PH_N(PH_FONT_DB), 1, g_font_list, &g_n_font);
    unsigned n_font_disk = g_n_font;                                   /* achadas na pasta system (definem o padrao, como sempre) */
    ph_merge_embedded(PH_EMBFONTS, sizeof PH_EMBFONTS / sizeof *PH_EMBFONTS, g_font_list, &g_n_font);
    /* BIOS padrao: fz1.bin (a que funciona) se estiver na pasta; senao a primeira achada */
    g_default_bios[0] = 0;
    for (unsigned i = 0; i < g_n_bios; i++) if (!strcmp(g_bios_list[i].file, "fz1.bin") || !strcmp(g_bios_list[i].file, "panafz1.bin")) { snprintf(g_default_bios, sizeof g_default_bios, "%.63s", g_bios_list[i].file); break; }
    if (!g_default_bios[0] && g_n_bios) snprintf(g_default_bios, sizeof g_default_bios, "%.63s", g_bios_list[0].file);
    /* Font padrao: a primeira achada na pasta system (o Phoenix sempre carregou a font ROM sozinho quando existia); sem nenhuma: disabled
     * (as embutidas ficam so disponiveis no menu, como no Opera: o padrao la tambem e desligado) */
    snprintf(g_default_font, sizeof g_default_font, "%.63s", n_font_disk ? g_font_list[0].file : "disabled");

    for (unsigned i = 0; i < PH_N_OPTS; i++) {
        const ph_optdef_t *o = &PH_OPTS[i];
        struct retro_core_option_v2_definition *d = &g_defs[i];
        memset(d, 0, sizeof *d);
        d->key = o->key; d->desc = o->desc; d->info = o->info; d->category_key = o->cat;
        if (!strcmp(o->key, "phoenix_bios")) ph_dyn_values(g_valtxt[i], sizeof g_valtxt[i], g_bios_list, g_n_bios, 0);
        else if (!strcmp(o->key, "phoenix_font")) ph_dyn_values(g_valtxt[i], sizeof g_valtxt[i], g_font_list, g_n_font, 1);
        else snprintf(g_valtxt[i], sizeof g_valtxt[i], "%s", o->vals);
        ph_parse_values(g_valtxt[i], d->values);
        d->default_value = !strcmp(o->key, "phoenix_bios") ? (g_default_bios[0] ? g_default_bios : NULL)
                         : !strcmp(o->key, "phoenix_font") ? g_default_font : o->def;
        /* "BIOS" sem nenhuma ROM: a lista ficaria vazia; cai no "auto" antigo (o nucleo avisa no log que nao achou) */
        if (!d->values[0].value) { d->values[0].value = "auto"; d->values[0].label = "(nenhuma BIOS encontrada)"; d->values[1].value = NULL; d->default_value = "auto"; }
    }
    memset(&g_defs[PH_N_OPTS], 0, sizeof g_defs[0]);
    struct retro_core_options_v2 v2 = { g_cats, g_defs };
    if (cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &v2)) return;
    /* frontend antigo: "Descricao; padrao|outro|..." (o padrao vai primeiro) */
    for (unsigned i = 0; i < PH_N_OPTS; i++) {
        const struct retro_core_option_v2_definition *d = &g_defs[i];
        char *t = g_legacy_txt[i]; size_t cap = sizeof g_legacy_txt[i], o = 0;
        o += (size_t)snprintf(t + o, cap - o, "%s; %s", d->desc, d->default_value ? d->default_value : d->values[0].value);
        for (unsigned k = 0; d->values[k].value && o + 80 < cap; k++)
            if (!d->default_value || strcmp(d->values[k].value, d->default_value)) o += (size_t)snprintf(t + o, cap - o, "|%s", d->values[k].value);
        g_legacy[i].key = d->key; g_legacy[i].value = t;
    }
    g_legacy[PH_N_OPTS].key = NULL; g_legacy[PH_N_OPTS].value = NULL;
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, g_legacy);
}
#endif
