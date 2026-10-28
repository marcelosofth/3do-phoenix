/* test_libretro.c - exercita o front-end como o RetroArch faria (inclui libretro.c para ver os estaticos).
 * Uso: ./test_libretro [diretorio_system]   (sem argumento usa uma BIOS sintetica "B ." num diretorio temporario)
 * Com diretorio_system (ex.: .) procura fz10.bin etc. em ./BIOS e roda 120 quadros so para ver que nao trava. */
#include "libretro.c"
#include <unistd.h>
#include <errno.h>
/* system() no Windows usa cmd.exe: "mkdir -p" e "rm -rf" nao existem la; usa comandos nativos e diretorio relativo */
/* nomes de teste: no Linux com acento UTF-8; no Windows so ASCII (o fopen/system do proprio teste usam ANSI; o acento la e teste manual) */
#ifdef _WIN32
#define T_ACAO "Acao"
#define T_OTIMO "Otimo"
#else
#define T_ACAO "Ação"
#define T_OTIMO "Ótimo"
#endif
#ifdef _WIN32
#define SYSDIR_FMT   "ph_sys_%d"
#define CMD_MKDIR    "mkdir \"%s\""
#define CMD_MKDIRQ   "mkdir \"%s\""
#define CMD_RESET    "rmdir /s /q \"%s\" 2>nul & mkdir \"%s\""
#define CMD_RMDIR    "rmdir /s /q \"%s\""
#else
#define SYSDIR_FMT   "/tmp/ph_sys_%d"
#define CMD_MKDIR    "mkdir -p %s"
#define CMD_MKDIRQ   "mkdir -p \"%s\""
#define CMD_RESET    "rm -rf %s && mkdir -p %s"
#define CMD_RMDIR    "rm -rf %s"
#endif

#define MCTL_CLUTXEN  0x2000u      /* MADAM MCTL (regs[2]); o VDLP do .exe nao testa estes bits, so a BIOS os liga */
#define MCTL_VSCTXEN  0x4000u
#define VR            0x200000u    /* base da VRAM no mapa do ARM (VRAM = RAM + 2 MB) */

static int fails, total;
#define CHK(c, msg) do { total++; if (!(c)) { fails++; printf("FALHOU: %s (linha %d)\n", msg, __LINE__); } } while (0)

static char sysdir[512];
static unsigned vw, vh, frames_out; static size_t vpitch; static const void *vdata;
static unsigned long audio_total;
static int pixfmt_offer = RETRO_PIXEL_FORMAT_XRGB8888, refuse_xrgb;
static const char *opt_region = "NTSC", *opt_fmt = "xrgb8888", *opt_bios = "auto";
static const char *opt_keep = "on", *opt_expand = "off", *opt_borders = "off", *opt_filter = "off", *opt_scale = "1x", *opt_hires = "disabled";
static char savedir[300];
static struct retro_core_options_v2 *v2_seen;
static const char *ov_key[32], *ov_val[32]; static int n_ov;
static void ov(const char *k, const char *v) { for (int i = 0; i < n_ov; i++) if (!strcmp(ov_key[i], k)) { ov_val[i] = v; return; } ov_key[n_ov] = k; ov_val[n_ov++] = v; }
static const struct retro_core_option_v2_definition *defof(const char *k) { for (int i = 0; v2_seen && v2_seen->definitions[i].key; i++) if (!strcmp(v2_seen->definitions[i].key, k)) return &v2_seen->definitions[i]; return NULL; }
static bool var_update; static struct retro_game_geometry last_geom; static int geom_calls;


/* No Windows o nucleo NAO usa o VFS do frontend (ph_file_init; religa com -DPH_USE_VFS): la os testes esperam stdio com _wfopen */
#if defined(_WIN32) && !defined(PH_USE_VFS)
#define T_VFS_ON 0
#else
#define T_VFS_ON 1
#endif
/* ---- VFS falso: delega ao stdio e conta as chamadas, para provar que o nucleo abre tudo pelo frontend ---- */
static unsigned vfs_max_ver;                       /* 0 = frontend sem VFS; senao a versao mais alta que ele oferece */
static int vfs_open_n, vfs_write_open_n, vfs_rename_n, vfs_remove_n, vfs_mkdir_n;
static struct retro_vfs_file_handle *m_open(const char *p, unsigned mode, unsigned hints) {
    FILE *f; (void)hints; vfs_open_n++; if (mode & RETRO_VFS_FILE_ACCESS_WRITE) vfs_write_open_n++;
    f = fopen(p, (mode & RETRO_VFS_FILE_ACCESS_WRITE) ? "wb" : "rb");
    return (struct retro_vfs_file_handle *)f;
}
static int m_close(struct retro_vfs_file_handle *h) { return fclose((FILE *)h); }
static int64_t m_size(struct retro_vfs_file_handle *h) { FILE *f = (FILE *)h; long c = ftell(f), e; fseek(f, 0, SEEK_END); e = ftell(f); fseek(f, c, SEEK_SET); return e; }
static int64_t m_seek(struct retro_vfs_file_handle *h, int64_t o, int w) { return fseek((FILE *)h, (long)o, w == RETRO_VFS_SEEK_POSITION_START ? SEEK_SET : w == RETRO_VFS_SEEK_POSITION_END ? SEEK_END : SEEK_CUR) == 0 ? ftell((FILE *)h) : -1; }
static int64_t m_read(struct retro_vfs_file_handle *h, void *d, uint64_t n) { return (int64_t)fread(d, 1, (size_t)n, (FILE *)h); }
static int64_t m_write(struct retro_vfs_file_handle *h, const void *d, uint64_t n) { return (int64_t)fwrite(d, 1, (size_t)n, (FILE *)h); }
static int m_remove(const char *p) { vfs_remove_n++; return remove(p) == 0 ? 0 : -1; }
static int m_rename(const char *a, const char *b) { vfs_rename_n++; return rename(a, b) == 0 ? 0 : -1; }
static int m_mkdir(const char *d) {
    int r; vfs_mkdir_n++;
#ifdef _WIN32
    r = _mkdir(d);
#else
    r = mkdir(d, 0755);
#endif
    return r == 0 ? 0 : (errno == EEXIST ? -2 : -1);
}
static struct retro_vfs_interface mock_vfs;
static void vfs_reset_counters(void) { vfs_open_n = vfs_write_open_n = vfs_rename_n = vfs_remove_n = vfs_mkdir_n = 0; }

static bool env(unsigned cmd, void *data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = sysdir; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixfmt_offer = *(int *)data;
        if (refuse_xrgb && pixfmt_offer == RETRO_PIXEL_FORMAT_XRGB8888) return false;
        return pixfmt_offer == RETRO_PIXEL_FORMAT_XRGB8888 || pixfmt_offer == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: case RETRO_ENVIRONMENT_SET_VARIABLES: return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: if (!savedir[0]) return false; *(const char **)data = savedir; return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {   /* o frontend copia durante a chamada: a struct do core vive na pilha dele (so as tabelas dentro sao estaticas) */
        static struct retro_core_options_v2 v2_copy; v2_copy = *(struct retro_core_options_v2 *)data; v2_seen = &v2_copy; return true; }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        for (int i = 0; i < n_ov; i++) if (!strcmp(v->key, ov_key[i])) { v->value = ov_val[i]; return true; }
        if (!strcmp(v->key, "phoenix_region")) { v->value = opt_region; return true; }
        if (!strcmp(v->key, "phoenix_pixel_format")) { v->value = opt_fmt; return true; }
        if (!strcmp(v->key, "phoenix_bios")) { v->value = opt_bios; return true; }
        if (!strcmp(v->key, "phoenix_keep_aspect")) { v->value = opt_keep; return true; }
        if (!strcmp(v->key, "phoenix_expand_bg")) { v->value = opt_expand; return true; }
        if (!strcmp(v->key, "phoenix_borders")) { v->value = opt_borders; return true; }
        if (!strcmp(v->key, "phoenix_image_filter")) { v->value = opt_filter; return true; }
        if (!strcmp(v->key, "phoenix_fbo_scale")) { v->value = opt_scale; return true; }
        if (!strcmp(v->key, "phoenix_high_resolution")) { v->value = opt_hires; return true; }
        return false; }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = var_update; var_update = false; return true;
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE: {
        struct retro_vfs_interface_info *vi = data;
        if (!vfs_max_ver || vi->required_interface_version > vfs_max_ver) return false;
        mock_vfs.open = m_open; mock_vfs.close = m_close; mock_vfs.size = m_size; mock_vfs.seek = m_seek; mock_vfs.read = m_read;
        mock_vfs.write = m_write; mock_vfs.remove = m_remove; mock_vfs.rename = m_rename;
        mock_vfs.mkdir = vfs_max_ver >= 3 ? m_mkdir : NULL;
        vi->required_interface_version = vfs_max_ver; vi->iface = &mock_vfs; return true; }
    case RETRO_ENVIRONMENT_SET_GEOMETRY: last_geom = *(struct retro_game_geometry *)data; geom_calls++; return true;
    }
    return false;
}
static void vid(const void *d, unsigned w, unsigned h, size_t p) { vdata = d; vw = w; vh = h; vpitch = p; frames_out++; }
static size_t aud(const int16_t *d, size_t n) { (void)d; audio_total += n; return n; }
static void poll(void) {}
static int16_t inp(unsigned a, unsigned b, unsigned c, unsigned d) { (void)a; (void)b; (void)c; (void)d; return 0; }

static void setup(void) {
    retro_set_environment(env); retro_set_video_refresh(vid); retro_set_audio_sample_batch(aud);
    retro_set_input_poll(poll); retro_set_input_state(inp);
}
static void write_bios(const char *dir) {
    char p[600]; snprintf(p, sizeof p, "%s/fz10.bin", dir);
    FILE *f = fopen(p, "wb"); uint8_t *b = calloc(1, 0x100000);
    b[0] = 0xEA; b[1] = 0xFF; b[2] = 0xFF; b[3] = 0xFE;           /* B . (branch para si mesmo) */
    fwrite(b, 1, 0x100000, f); fclose(f); free(b);
}

int main(int argc, char **argv) {
    g_cover_last = 0;   /* os testes abaixo comparam pixels exatos; a cobertura da ultima linha tem teste proprio no fim */
    if (argc > 1) {
        strncpy(sysdir, argv[1], sizeof sysdir - 1);
        { static char buf[1024]; const char *e = getenv("PH_OPTS"); if (e) { snprintf(buf, sizeof buf, "%s", e); for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) { char *q = strchr(t, '='); if (q) { *q = 0; ov(t, q + 1); } } } }
        setup(); retro_init();
        CHK(retro_load_game(NULL), "BIOS real carregou");
        if (!g_have_machine) { printf("BIOS real nao encontrada em %s\n", sysdir); return 1; }
        uint32_t maxhead = 0; int mctl_set = 0, nonblack = 0;
        int nframes = argc > 3 ? atoi(argv[3]) : 120;
        for (int i = 0; i < nframes; i++) { retro_run(); if (g_m.vdlp.headvdl > maxhead) maxhead = g_m.vdlp.headvdl; if (g_m.madam.regs[2] & MCTL_VSCTXEN) mctl_set = 1; }
        for (unsigned i = 0; i < 320u * 240u; i++) if (((uint32_t *)g_fb)[i] & 0xffffff) { nonblack = 1; break; }
        printf("BIOS real: %d quadros, PC=%08x ciclos=%u, cabeca VDL=%08x, MCTL=%08x (VSCTXEN %s), pixel nao-preto: %s, frames VDLP=%u\n",
               nframes, g_m.cpu.r[15], g_m.total_cycles, maxhead, g_m.madam.regs[2], mctl_set ? "sim" : "nao", nonblack ? "sim" : "nao", g_frames);
        CHK(frames_out == (unsigned)nframes && g_frames + 10 >= (unsigned)nframes, "N chamadas de retro_run produziram ~N quadros do VDLP");
        if (argc > 2) {                                   /* argv[2] = arquivo .ppm de saida */
            FILE *o = fopen(argv[2], "wb");
            if (o) { fprintf(o, "P6\n%u %u\n255\n", vw, vh);
                for (unsigned i = 0; i < vw * vh; i++) { uint32_t c = ((const uint32_t *)vdata)[i]; fputc((c >> 16) & 255, o); fputc((c >> 8) & 255, o); fputc(c & 255, o); }
                fclose(o); }
        }
        retro_unload_game(); retro_deinit();
        printf("test_libretro (BIOS real): %d verificacoes, %d falhas\n", total, fails);
        return fails ? 1 : 0;
    }

    /* ---- BIOS sintetica ---- */
    snprintf(sysdir, sizeof sysdir, SYSDIR_FMT, (int)getpid());
    char cmd[1200]; snprintf(cmd, sizeof cmd, CMD_MKDIR, sysdir); if (system(cmd)) return 2;
    write_bios(sysdir);

    struct retro_system_info si; retro_get_system_info(&si);
    CHK(!strcmp(si.library_name, "Phoenix 3DO") && si.need_fullpath && strstr(si.valid_extensions, "iso"), "retro_get_system_info");
    CHK(retro_api_version() == RETRO_API_VERSION, "retro_api_version");

    setup(); retro_init();
    CHK(g_fb != NULL, "retro_init alocou o framebuffer");
    CHK(retro_load_game(NULL), "retro_load_game sem jogo (BIOS sintetica em system/)");
    CHK(g_have_machine && g_m.cpu.r[15] == 0x03000000u, "maquina iniciada com PC na BIOS");
    CHK(g_m.madam.dma580_write && g_m.clio.vdlp_line && g_m.clio.audio_out, "ganchos ligados");
    CHK(pixfmt_offer == RETRO_PIXEL_FORMAT_XRGB8888, "pediu XRGB8888");

    struct retro_system_av_info av; retro_get_system_av_info(&av);
    CHK(av.geometry.base_width == 320 && av.geometry.base_height == 240 && av.geometry.max_height == 1280 && av.geometry.max_width == 2272, "geometria NTSC 320x240 (teto 2272x1280 = canvas 16:9 x escala 4)");
    CHK(av.timing.fps > 59.9 && av.timing.fps < 60.1 && av.timing.sample_rate == 44100.0, "NTSC ~60 fps, audio 44100 Hz");
    CHK(av.geometry.aspect_ratio > 1.33f && av.geometry.aspect_ratio < 1.34f, "4:3");

    /* programa o VDLP pelo MESMO caminho que a BIOS usaria: pokes no barramento do MADAM */
    /* VDL e bitmap na VRAM (ram + 2 MB). Formato lido por vdl_read() (FUN_004740d0): ctl = linhas | n_palavras<<9 |
     * bit16 (le CURRENTBMP) | bit21 (EnDMA) | largura 320 (indice 0); depois CURRENTBMP, PREVIOUSBMP, proxima VDL e as palavras de cor.
     * Cada palavra do bitmap guarda 2 pixels: metade alta = linha par, baixa = linha impar (par de linhas = 320 palavras). */
    uint32_t bmp = VR + 0x20000, vdl = VR + 0x100;
    for (int y = 0; y < 130; y++) for (int x = 0; x < 320; x++) g_m.ram[(bmp >> 2) + y * 320 + x] = ((uint32_t)(5 << 10) << 16) | (uint32_t)(1 << 5);
    uint32_t e1[] = { 240u | (2u << 9) | (1u << 16) | (1u << 21), bmp, 0, 0, 0x05000000u | 0x00ff0000u, (1u << 24) | 0x0000ff00u };
    for (int i = 0; i < 6; i++) g_m.ram[(vdl >> 2) + i] = e1[i];
    ph_write32(&g_m, 0x03300008, MCTL_CLUTXEN | MCTL_VSCTXEN);
    ph_write32(&g_m, 0x03300580, vdl);
    CHK(g_m.vdlp.headvdl == vdl, "poke MADAM 0x580 chegou no VDLP (gancho dma580_write)");

    for (int i = 0; i < 3; i++) retro_run();
    CHK(frames_out == 3 && vw == 320 && vh == 240 && vpitch == 320 * 4, "video_cb: 320x240, pitch 1280, 3 quadros");
    const uint32_t *px = vdata;
    CHK(px == (const uint32_t *)g_fb, "video_cb recebeu o buffer do VDLP");
    CHK(px[0] == 0xff0000u && px[320] == 0x00ff00u && px[239 * 320 + 319] == 0x00ff00u, "pixels: linha par vermelha, impar verde (via CLIO->VDLP real)");
    CHK(g_frames == 3, "o VDLP fechou exatamente 1 quadro por retro_run (3 quadros)");
    CHK(audio_total > 3 * 700 && audio_total < 3 * 800, "~735 amostras de audio por quadro");
    CHK(g_m.total_cycles > 3u * 200000u && g_m.total_cycles < 3u * 212000u, "~208k ciclos por quadro NTSC");
    /* cobertura do tracinho de ceu SO na fase The Roof: um quadro qualquer (listras vermelha/verde) NAO e alterado, mesmo com a opcao ligada */
    g_cover_last = 1; retro_run(); px = vdata;
    CHK(px[239 * 320] == 0x00ff00u && px[238 * 320] == 0xff0000u && px[239 * 320 + 319] == 0x00ff00u, "fora da The Roof: ultima linha como o jogo desenha (sem cobertura)");
    g_cover_last = 0; retro_run();
    CHK(((const uint32_t *)vdata)[239 * 320] == 0x00ff00u, "desligado: ultima linha como o jogo desenha (verde)");
    /* quadro sintetico da The Roof: janela 6..233, resto preto; ceu laranja em cima, chao de pedra cinza embaixo; a ultima linha (233) esta "suja"
     * (cor propria + trechos de ceu vazando). Reconhecida a fase, a linha 233 inteira vira copia da 232 (igual ao v9). */
    {   lr_layout_t Lc; video_layout(&Lc);
        const uint32_t sky = 0xf84c02u, stone = 0x1c1c1cu, leakc = 0xeaae28u, dirty = 0x303030u;
        const uint32_t *fb = (const uint32_t *)g_fb;
        /* variantes: 0 = Roof (chao escuro), 1 = Roof com chao mais claro (pedra medio-cinza), 2 = mesma forma com ceu azul (outra fase),
         * 3 = Roof com a opcao off, 4 = ceu laranja mas chao colorido (nao e pedra cinza) */
        for (int variant = 0; variant < 5; variant++) {
            uint32_t fl = (variant == 1) ? 0x707070u : (variant == 4) ? 0x205080u : stone;
            for (unsigned y = 0; y < 240; y++) for (unsigned x = 0; x < 320; x++) {
                uint32_t c = 0;
                if (y >= 6 && y < 234) c = (y < 170) ? (variant == 2 ? 0x2040c0u : sky) : fl;
                if (y == 233) c = (x >= 178 && x < 198) ? (variant == 2 ? 0x4060e0u : leakc) : (variant == 2 ? 0x101018u : dirty);
                g_src[y * LR_MAX_W + x] = c;
            }
            g_cover_last = (variant != 3); g_roof_active = 0; g_roof_hold = 0;
            video_compose(&Lc, 320, 240);
            if (variant == 0 || variant == 1) {
                CHK(fb[233 * 320 + 190] == fl && fb[233 * 320 + 100] == fl && fb[233 * 320 + 0] == fl && fb[233 * 320 + 319] == fl, variant ? "The Roof (chao mais claro): ultima linha inteira = copia da de cima" : "The Roof: ultima linha inteira = copia da de cima (sem tracinho)");
                CHK(fb[232 * 320 + 190] == fl && fb[100 * 320 + 190] == sky && fb[169 * 320 + 5] == sky && fb[234 * 320 + 190] == 0u && fb[239 * 320 + 319] == 0u && fb[5 * 320] == 0u, "The Roof: resto do quadro intacto e faixa preta (0..5, 234..239) segue preta");
                CHK(g_roof_active == 1, "The Roof: fase reconhecida");
            } else if (variant == 2) {
                CHK(fb[233 * 320 + 190] == 0x4060e0u && fb[233 * 320 + 100] == 0x101018u && g_roof_active == 0, "outra fase (ceu azul, mesma forma): quadro sem alteracao");
            } else if (variant == 3) {
                CHK(fb[233 * 320 + 190] == leakc && fb[233 * 320 + 100] == dirty && g_roof_active == 0, "opcao off: The Roof como o jogo desenha");
            } else {
                CHK(fb[233 * 320 + 190] == leakc && g_roof_active == 0, "ceu laranja mas chao nao cinza: sem alteracao");
            }
        }
        /* margem de quadros: um quadro fora da assinatura (flash) logo depois da Roof continua coberto; depois de PH_ROOF_HOLD quadros solta */
        for (unsigned y = 0; y < 240; y++) for (unsigned x = 0; x < 320; x++) g_src[y * LR_MAX_W + x] = (y < 6 || y >= 234) ? 0u : (y == 233 ? dirty : (y < 170 ? sky : stone));
        g_cover_last = 1; g_roof_active = 0; g_roof_hold = 0; video_compose(&Lc, 320, 240);
        for (unsigned y = 6; y < 234; y++) for (unsigned x = 0; x < 320; x++) g_src[y * LR_MAX_W + x] = (y == 233) ? dirty : 0xffffffu;   /* quadro branco (flash) */
        video_compose(&Lc, 320, 240);
        CHK(g_roof_active == 1 && fb[233 * 320 + 50] == 0xffffffu, "flash de 1 quadro: ainda coberto (margem)");
        for (int i = 0; i < PH_ROOF_HOLD; i++) video_compose(&Lc, 320, 240);
        CHK(g_roof_active == 0 && fb[233 * 320 + 50] == dirty, "depois da margem sem a assinatura: solta e mostra o quadro como o jogo desenha");
        g_cover_last = 0;
    }


    /* NVRAM */
    retro_run();
    CHK(ph_nvram_formatted(g_m.nvram) && g_m.nvram[0] == 1 && g_m.nvram[1] == 'Z' && g_m.nvram[6] == 2 && g_m.nvram[132] == 0x85 && g_m.nvram[152] == 0x7a, "NVRAM em branco e formatada (volume 3DO) antes do BIOS rodar");
    CHK(retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) == 0x8000 && retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) == g_m.nvram, "NVRAM exposta como SAVE_RAM");
    ((uint8_t *)retro_get_memory_data(RETRO_MEMORY_SAVE_RAM))[10] = 0x5a;
    retro_reset();
    CHK(g_m.nvram[10] == 0x5a, "reset preserva a NVRAM");
    CHK(g_m.cpu.r[15] == 0x03000000u && g_m.vdlp.headvdl == 0, "reset reinicia CPU e VDLP");
    {   size_t ss = retro_serialize_size(); void *st = ss ? malloc(ss) : NULL;      /* savestate (machine.c ph_state_*): ida e volta */
        CHK(ss > 0x300000 && st && retro_serialize(st, ss), "savestate: serializa");
        g_m.nvram[20] = 0x77; retro_run(); retro_run();
        CHK(st && retro_unserialize(st, ss) && g_m.nvram[20] != 0x77, "savestate: restaura (NVRAM volta ao valor salvo)");
        CHK(!retro_unserialize(st, ss - 1) && !retro_serialize(st, ss - 1), "savestate: tamanho errado e recusado");
        retro_run(); free(st); }
    retro_unload_game(); retro_deinit();


    /* ---- opcoes de video (Keep aspect / Expand background / Show borders / Image filter / FBO scale) ---- */
    {
        opt_region = "NTSC"; opt_fmt = "xrgb8888"; setup(); retro_init();
        CHK(retro_load_game(NULL), "video: carrega NTSC");
        uint32_t bmp2 = VR + 0x20000, vdl2 = VR + 0x100;
        for (int y = 0; y < 130; y++) for (int x = 0; x < 320; x++) g_m.ram[(bmp2 >> 2) + y * 320 + x] = ((uint32_t)(5 << 10) << 16) | (uint32_t)(1 << 5);
        uint32_t e2[] = { 240u | (2u << 9) | (1u << 16) | (1u << 21), bmp2, 0, 0, 0x05000000u | 0x00ff0000u, (1u << 24) | 0x0000ff00u };
        for (int i = 0; i < 6; i++) g_m.ram[(vdl2 >> 2) + i] = e2[i];
        ph_write32(&g_m, 0x03300008, MCTL_CLUTXEN | MCTL_VSCTXEN); ph_write32(&g_m, 0x03300580, vdl2);
        retro_run(); retro_run();
        const uint32_t *o;

        /* escala 2x sem filtro: pixel repetido */
        opt_scale = "2x"; read_video_options(); retro_run(); o = vdata;
        CHK(vw == 640 && vh == 480 && vpitch == 640 * 4, "escala 2x: 640x480");
        CHK(o[0] == 0xff0000u && o[1] == 0xff0000u && o[640] == 0xff0000u && o[2 * 640] == 0x00ff00u && o[3 * 640 + 639] == 0x00ff00u, "escala 2x sem filtro: pixels repetidos (2 linhas vermelhas, 2 verdes)");
        retro_get_system_av_info(&av);
        CHK(av.geometry.base_width == 640 && av.geometry.base_height == 480 && av.geometry.aspect_ratio > 1.33f && av.geometry.aspect_ratio < 1.34f, "av_info acompanha a escala (640x480, 4:3)");

        /* linear 2x: linha 1 de saida = 75% vermelho + 25% verde (centros de texel, como GL_LINEAR) */
        opt_filter = "linear"; read_video_options(); retro_run(); o = vdata;
        CHK(o[1 * 640 + 2] == 0xbf4000u && o[2 * 640 + 2] == 0x40bf00u, "filtro linear 2x: mistura 75/25 entre linhas vizinhas");
        CHK(o[0] == 0xff0000u && o[479 * 640 + 2] == 0x00ff00u && o[479 * 640 + 639] == 0x00ff00u, "filtro linear: bordas da imagem sem mistura (clamp)");

        /* shader std: cor lisa continua lisa; aresta vertical ganha tons intermediarios */
        opt_filter = "shader_std"; read_video_options();
        for (unsigned y = 0; y < 240; y++) for (unsigned x = 0; x < 320; x++) g_src[y * LR_MAX_W + x] = 0x336699u;
        lr_layout_t L; video_layout(&L); video_compose(&L, 320, 240); o = g_fb;
        { int ok = 1; for (unsigned i = 0; i < L.ow * L.oh; i++) if (o[i] != 0x336699u) { ok = 0; break; } CHK(ok, "shader std: area lisa fica exatamente igual"); }
        for (unsigned y = 0; y < 240; y++) for (unsigned x = 0; x < 320; x++) g_src[y * LR_MAX_W + x] = (x < 160) ? 0x000000u : 0xffffffu;
        video_compose(&L, 320, 240); o = g_fb;
        { uint32_t m = o[100 * 640 + 319] & 0xff; CHK(o[100 * 640 + 100] == 0 && o[100 * 640 + 500] == 0xffffffu && m > 0 && m < 255, "shader std: aresta suavizada, longe da aresta intacto"); }
        for (unsigned y = 0; y < 240; y++) for (unsigned x = 0; x < 320; x++) g_src[y * LR_MAX_W + x] = ((x + y) & 8) ? 0xffffffu : 0x102030u;   /* diagonais: exercita o ramo diag do shader */
        opt_scale = "4x"; read_video_options(); video_layout(&L); video_compose(&L, 320, 240);
        CHK(L.ow == 1280 && L.oh == 960, "shader std 4x diagonal: 1280x960 sem estourar");

        /* bordas: margem de 5% com a cor de fundo da VDL (bg byte0=R byte1=G byte2=B) */
        opt_scale = "1x"; opt_filter = "off"; opt_borders = "on"; read_video_options();
        g_m.vdlp.bg = 0xff0000u;                              /* azul */
        for (unsigned y = 0; y < 240; y++) for (unsigned x = 0; x < 320; x++) g_src[y * LR_MAX_W + x] = 0xff0000u;
        video_layout(&L); video_compose(&L, 320, 240); o = g_fb;
        CHK(L.ow == 352 && L.oh == 264 && L.aspect > 1.33f && L.aspect < 1.34f, "bordas: 352x264, 4:3");
        CHK(o[0] == 0x0000ffu && o[263 * 352 + 351] == 0x0000ffu && o[12 * 352 + 16] == 0xff0000u && o[251 * 352 + 335] == 0xff0000u && o[11 * 352 + 16] == 0x0000ffu, "bordas: cor de fundo nas margens, imagem no centro");

        /* fundo expandido: 16:9 com a imagem centrada */
        opt_borders = "off"; opt_expand = "on"; read_video_options(); video_layout(&L); video_compose(&L, 320, 240); o = g_fb;
        CHK(L.ow == 426 && L.oh == 240 && L.aspect > 1.77f && L.aspect < 1.78f, "fundo expandido: 426x240, 16:9");
        CHK(o[0] == 0x0000ffu && o[52] == 0x0000ffu && o[53] == 0xff0000u && o[372] == 0xff0000u && o[373] == 0x0000ffu, "fundo expandido: imagem centrada, fundo dos lados");
        /* manter proporcao desligado: pixels quadrados */
        opt_expand = "off"; opt_borders = "on"; opt_keep = "off"; read_video_options(); video_layout(&L);
        CHK(L.aspect > 1.33f && L.aspect < 1.34f, "sem manter proporcao: pixels quadrados (352/264)");
        opt_keep = "on"; opt_borders = "off"; read_video_options();

        /* troca ao vivo: o frontend avisa e o nucleo manda SET_GEOMETRY */
        geom_calls = 0; opt_scale = "3x"; var_update = true; retro_run();
        CHK(geom_calls == 1 && last_geom.base_width == 960 && last_geom.base_height == 720 && vw == 960 && vh == 720, "opcao alterada com o jogo rodando: SET_GEOMETRY 960x720 e quadro novo");
        retro_run(); CHK(geom_calls == 1, "sem aviso do frontend, nao repete SET_GEOMETRY");
        retro_unload_game(); retro_deinit();

        /* PAL + RGB565 + tudo ligado cabe no teto */
        opt_region = "PAL"; opt_fmt = "rgb565"; opt_borders = "on"; opt_expand = "on"; opt_filter = "shader_std"; opt_scale = "4x";
        setup(); retro_init(); CHK(retro_load_game(NULL), "video: PAL com tudo ligado");
        retro_run(); retro_get_system_av_info(&av);
        CHK(vw == 562 * 4 && vh == 316 * 4 && vpitch == (size_t)vw * 2 && vw <= av.geometry.max_width && vh <= av.geometry.max_height, "PAL 16:9 + bordas + 4x em RGB565: 2248x1264 dentro do teto");
        retro_unload_game(); retro_deinit();
        opt_region = "NTSC"; opt_fmt = "xrgb8888"; opt_borders = "off"; opt_expand = "off"; opt_filter = "off"; opt_scale = "1x";
    }

    /* ---- HiRes CEL Rendering (Parte 24): CELs 3D em 2x + saida do VDLP em 2x; vale na hora ---- */
    {
        opt_region = "NTSC"; opt_fmt = "xrgb8888"; opt_hires = "enabled"; opt_scale = "1x"; setup(); retro_init();
        CHK(retro_load_game(NULL), "hires: carrega com a opcao ligada");
        CHK(g_m.hires && g_m.vdlp.hires, "hires: ligado no boot");
        uint32_t bmp2 = VR + 0x20000, vdl2 = VR + 0x100;
        for (int y = 0; y < 130; y++) for (int x = 0; x < 320; x++) ph_write32(&g_m, bmp2 + 4u * (uint32_t)(y * 320 + x), ((uint32_t)(5 << 10) << 16) | (uint32_t)(1 << 5));   /* pela CPU: replica nos 4 planos */
        uint32_t e2[] = { 240u | (2u << 9) | (1u << 16) | (1u << 21), bmp2, 0, 0, 0x05000000u | 0x00ff0000u, (1u << 24) | 0x0000ff00u };
        for (int i = 0; i < 6; i++) ph_write32(&g_m, vdl2 + 4u * (uint32_t)i, e2[i]);
        ph_write32(&g_m, 0x03300008, MCTL_CLUTXEN | MCTL_VSCTXEN); ph_write32(&g_m, 0x03300580, vdl2);
        retro_run(); retro_run(); retro_run();
        const uint32_t *o = vdata;
        retro_get_system_av_info(&av);
        CHK(av.geometry.base_width == 640 && av.geometry.base_height == 480 && vw == 640 && vh == 480 && vpitch == 640 * 4, "hires: saida 2x (640x480) com FBO 1x");
        CHK(av.geometry.aspect_ratio > 1.33f && av.geometry.aspect_ratio < 1.34f, "hires: aspecto continua 4:3");
        CHK(o[0] == 0xff0000u && o[1] == 0xff0000u && o[640] == 0xff0000u && o[641] == 0xff0000u && o[479 * 640] == 0x00ff00u, "hires: imagem 1x replicada nas 4 subamostras (cores certas)");
        g_cover_last = 1; retro_run(); o = vdata;   /* HiRes fora da The Roof: ultimas linhas 2x intactas (listras vermelha/verde) */
        CHK(o[478 * 640] == 0x00ff00u && o[479 * 640] == 0x00ff00u && o[476 * 640] == 0xff0000u, "hires: fora da The Roof as ultimas linhas 2x nao sao alteradas");
        g_cover_last = 0; retro_run(); o = vdata;
        /* so o plano 1 (x impar) zerado: as colunas impares viram a cor de fundo, as pares seguem */
        memset(&g_m.hr[0][(bmp2 - VR) >> 2], 0, 130u * 320u * 4u);
        retro_run(); o = vdata;
        CHK(o[0] == 0xff0000u && o[1] != 0xff0000u && o[640] == 0xff0000u && o[641] == 0xff0000u, "hires: plano 1 vai para (x impar, y par); planos 0, 2 e 3 intactos");
        memset(&g_m.hr[1][(bmp2 - VR) >> 2], 0, 130u * 320u * 4u);
        retro_run(); o = vdata;
        CHK(o[640] != 0xff0000u && o[641] == 0xff0000u, "hires: plano 2 vai para (x par, y impar); plano 3 para (x impar, y impar)");

        /* FBO 3x com hires: origem 2x reamostrada para 3x; FBO 4x: 2x exato */
        opt_scale = "3x"; var_update = true; retro_run();
        CHK(vw == 960 && vh == 720, "hires + FBO 3x: 960x720");
        opt_scale = "4x"; var_update = true; retro_run();
        CHK(vw == 1280 && vh == 960, "hires + FBO 4x: 1280x960");
        opt_filter = "shader_std"; var_update = true; retro_run(); retro_run();
        CHK(vw == 1280 && vh == 960, "hires + shader std 4x: sem estourar buffers");
        opt_filter = "off"; opt_scale = "1x";

        /* desliga ao vivo: SET_GEOMETRY volta para 320x240 e o quadro 1x */
        geom_calls = 0; opt_hires = "disabled"; var_update = true; retro_run();
        CHK(!g_m.hires && !g_m.vdlp.hires && geom_calls == 1 && last_geom.base_width == 320 && vw == 320 && vh == 240, "hires desligado ao vivo: volta a 320x240");
        retro_run(); CHK(vw == 320 && vh == 240 && ((const uint32_t *)vdata)[0] == 0xff0000u, "hires desligado: imagem 1x intacta");
        /* e liga de novo ao vivo (os planos recomecam da VRAM atual) */
        opt_hires = "enabled"; var_update = true; retro_run(); retro_run();
        CHK(g_m.hires && vw == 640 && ((const uint32_t *)vdata)[1] == 0xff0000u, "hires religado ao vivo: 640x480, subamostras de novo iguais a VRAM");
        retro_unload_game(); retro_deinit();
        opt_hires = "disabled";
    }

    /* ---- RGB565 + PAL + fallback de formato ---- */
    opt_region = "PAL"; opt_fmt = "rgb565"; frames_out = 0;
    setup(); retro_init();
    CHK(retro_load_game(NULL), "carrega em PAL/RGB565");
    retro_get_system_av_info(&av);
    CHK(av.geometry.base_width == 384 && av.geometry.base_height == 288 && av.timing.fps > 49.9 && av.timing.fps < 50.1, "PAL: 384x288, ~50 fps");
    CHK(retro_get_region() == RETRO_REGION_PAL, "regiao PAL");
    CHK(pixfmt_offer == RETRO_PIXEL_FORMAT_RGB565, "pediu RGB565");
    for (int i = 0; i < 2; i++) retro_run();
    CHK(g_m.clio.pal == 1 && g_m.vdlp.width == 384 && g_m.vdlp.lines == 288, "PAL: VDLP 384x288 e CLIO em PAL");
    CHK(vw == 384 && vh == 288 && vpitch == 384 * 2, "PAL/RGB565: 384x288, pitch 768");
    CHK(g_frames == 2, "PAL: 1 quadro por retro_run");
    retro_unload_game(); retro_deinit();

    opt_region = "NTSC"; opt_fmt = "xrgb8888"; refuse_xrgb = 1;
    setup(); retro_init();
    CHK(retro_load_game(NULL) && g_fmt == LR_RGB565, "frontend recusa XRGB8888 -> cai para RGB565");
    retro_unload_game(); retro_deinit();

    /* ---- opcoes de nucleo no formato do Opera (ph_options.h) ---- */
    {   static const char *keys[] = { "phoenix_bios", "phoenix_font", "phoenix_cd_speed", "phoenix_region", "phoenix_nvram_storage", "phoenix_nvram_version",
            "phoenix_pixel_format", "phoenix_vdlp_bypass_clut", "phoenix_high_resolution", "phoenix_active_devices", "phoenix_hide_lightgun_crosshairs",
            "phoenix_cpu_overclock", "phoenix_mem_capacity", "phoenix_random_seed", "phoenix_madam_matrix_engine", "phoenix_swi_hle", "phoenix_kprint" };
        write_bios(sysdir); v2_seen = NULL; setup();
        CHK(v2_seen != NULL, "opcoes registradas no formato v2");
        int all = 1; for (unsigned i = 0; i < sizeof keys / sizeof *keys; i++) if (!defof(keys[i])) { all = 0; printf("  falta %s\n", keys[i]); }
        CHK(all, "as 17 opcoes do Opera existem (BIOS, Font, CD Speed, Region, NVRAM x2, Pixel Format, Bypass CLUT, HiRes, Devices, Lightgun, Overclock, Memory, Seed, Matrix, SWI HLE, Debug)");
        const struct retro_core_option_v2_definition *d = defof("phoenix_bios");
        {   unsigned nv = 0; int has_fz1 = 0; if (d) for (; d->values[nv].value; nv++) if (!strcmp(d->values[nv].value, "panafz1.bin")) has_fz1 = 1;
            CHK(d && d->values[0].value && !strcmp(d->values[0].value, "fz10.bin") && nv == 9 && has_fz1 && !strcmp(d->default_value, "panafz1.bin"), "BIOS dinamica: lista o fz10.bin achado em system/ + as 8 incorporadas (padrao: FZ-1 incorporada)");
            d = defof("phoenix_font");
            CHK(d && d->values[0].value && !strcmp(d->values[0].value, "disabled") && d->values[1].value && !strcmp(d->values[1].value, "panafz1-kanji.bin") && d->values[2].value && !strcmp(d->values[2].value, "panafz10ja-anvil-kanji.bin") && d->values[3].value == NULL && !strcmp(d->default_value, "disabled"), "Font dinamica: disabled + as 2 fontes incorporadas (padrao desligado)"); }
        d = defof("phoenix_bios");
        d = defof("phoenix_cd_speed"); CHK(d && !strcmp(d->default_value, "2x") && !strcmp(d->values[0].value, "unbounded"), "CD Speed: padrao 2x (stock), 1o valor unbounded");
        d = defof("phoenix_region"); CHK(d && !strcmp(d->values[1].value, "pal1") && !strcmp(d->values[1].label, "PAL1 320x288@50"), "Region / Mode: ntsc/pal1/pal2 com os rotulos do Opera");
        d = defof("phoenix_cpu_overclock"); CHK(d && !strcmp(d->default_value, "1.0x (12.50Mhz)"), "CPU Overclock: padrao 1.0x (12.50Mhz)");
        CHK(!strcmp(v2_seen->categories[0].key, "system") && defof("phoenix_swi_hle") && !strcmp(defof("phoenix_swi_hle")->category_key, "advanced"), "categorias System/Video/Input/Advanced");
        /* aplicacao das opcoes na maquina */
        ov("phoenix_region", "pal1"); ov("phoenix_cpu_overclock", "2.0x (25.00Mhz)"); ov("phoenix_cd_speed", "unbounded"); ov("phoenix_madam_matrix_engine", "software");
        ov("phoenix_random_seed", "0xdeadbeef"); ov("phoenix_vdlp_bypass_clut", "enabled"); ov("phoenix_pixel_format", "0RGB1555"); ov("phoenix_swi_hle", "enabled");
        setup(); retro_init();
        CHK(retro_load_game(NULL), "carrega com as opcoes novas");
        CHK(g_m.vdlp.width == 320 && g_m.vdlp.lines == 288 && g_m.clio.pal == 1, "PAL1: VDLP 320x288 e CLIO em PAL");
        retro_get_system_av_info(&av); CHK(av.geometry.base_width == 320 && av.geometry.base_height == 288 && av.timing.fps < 50.1, "PAL1: geometria 320x288 a 50 fps");
        CHK(g_m.cd.rate == 0, "CD Speed unbounded: rate 0");
        CHK(g_m.clk_q16 == 32768, "CPU Overclock 2.0x: 32768 (0.5 tick por ciclo)");
        CHK(g_m.madam.regs[0] & 1, "MADAM Matrix Engine software: bit 0 do ID");
        CHK(g_m.clio.dsp_rand == 0x5eadbeefu, "Random seed fixa 0xdeadbeef (31 bits)");
        CHK(g_m.vdlp.bypass_clut == 1 && g_fmt == LR_RGB565, "Bypass CLUT ligado; 0RGB1555 e XRGB8888 recusados pelo frontend -> cai para RGB565");
        { uint32_t c0 = g_m.total_cycles; retro_run(); uint32_t used = g_m.total_cycles - c0;
          CHK(used > 450000u && used < 560000u, "overclock 2x: a CPU roda ~2x mais ciclos por quadro"); }
        ov("phoenix_cpu_overclock", "1.0x (12.50Mhz)"); ov("phoenix_cd_speed", "8x"); ov("phoenix_vdlp_bypass_clut", "disabled"); var_update = true; retro_run();
        CHK(g_m.clk_q16 == 0 && g_m.cd.rate == 20833 && g_m.vdlp.bypass_clut == 0, "opcoes ao vivo: overclock 1x, CD 8x (20833), bypass off");
        /* SWI HLE: MulVec3Mat33 (0x50000) e Dot3 (0x5000C) em C */
        { int32_t *r = (int32_t *)g_m.ram;
          r[0x1000 >> 2] = 0x10000; r[0x1004 >> 2] = 0x20000; r[0x1008 >> 2] = 0x30000;                 /* vec (1,2,3) em 16.16 */
          for (int i = 0; i < 9; i++) r[(0x1100 >> 2) + i] = (i % 4 == 0) ? 0x10000 : 0;               /* identidade */
          g_m.cpu.r[0] = 0x1200; g_m.cpu.r[1] = 0x1000; g_m.cpu.r[2] = 0x1100;
          CHK(ph_swi_hle(&g_m, 0x50000) && r[0x1200 >> 2] == 0x10000 && r[0x1204 >> 2] == 0x20000 && r[0x1208 >> 2] == 0x30000, "SWI HLE 0x50000 vetor x identidade");
          g_m.cpu.r[0] = 0x1000; g_m.cpu.r[1] = 0x1000; CHK(ph_swi_hle(&g_m, 0x5000C) && (int32_t)g_m.cpu.r[0] == 14 * 0x10000, "SWI HLE 0x5000C Dot3 = 14.0");
          g_m.cpu.r[0] = 0x3fffff0; CHK(!ph_swi_hle(&g_m, 0x50000), "SWI HLE: ponteiro fora da RAM nao e tratado"); }
        retro_unload_game(); retro_deinit();
        n_ov = 0;
    }

    /* ---- NVRAM por jogo / compartilhada / versao (arquivo proprio na pasta de saves) ---- */
    {   char sd[300], f[600]; snprintf(sd, sizeof sd, "%s/saves", sysdir); snprintf(cmd, sizeof cmd, CMD_MKDIR, sd); if (system(cmd)) return 2;
        snprintf(savedir, sizeof savedir, "%s", sd); write_bios(sysdir);
        char iso[1300]; snprintf(iso, sizeof iso, "%s/jogoA.iso", sysdir); FILE *fi = fopen(iso, "wb"); for (int i = 0; fi && i < 4 * 2048; i++) fputc(1, fi); if (fi) fclose(fi);
        struct retro_game_info g1 = { iso, NULL, 0, NULL };
        n_ov = 0; setup(); retro_init(); CHK(retro_load_game(&g1), "nvram: carrega");
        CHK(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) == NULL && retro_get_memory_size(RETRO_MEMORY_SAVE_RAM) == 0, "nvram: com pasta de saves o frontend nao grava .srm");
        g_m.nvram[5] = 0x42; retro_unload_game(); retro_deinit();
        snprintf(f, sizeof f, "%s/phoenix/per_game/jogoA.0.srm", sd); FILE *q = fopen(f, "rb"); uint8_t b[0x8000]; int ok = q && fread(b, 1, sizeof b, q) == sizeof b && b[5] == 0x42; if (q) fclose(q);
        CHK(ok, "nvram: por jogo grava <saves>/phoenix/per_game/jogoA.0.srm");
        setup(); retro_init(); CHK(retro_load_game(&g1) && g_m.nvram[5] == 0x42, "nvram: recarrega o save por jogo"); retro_unload_game(); retro_deinit();
        ov("phoenix_nvram_version", "3"); setup(); retro_init(); CHK(retro_load_game(&g1) && g_m.nvram[5] == 0, "nvram: versao 3 e outro slot (vazio)");
        g_m.nvram[6] = 0x43; retro_unload_game(); retro_deinit();
        snprintf(f, sizeof f, "%s/phoenix/per_game/jogoA.3.srm", sd); q = fopen(f, "rb"); CHK(q != NULL, "nvram: slot 3 gravado"); if (q) fclose(q);
        ov("phoenix_nvram_storage", "shared"); ov("phoenix_nvram_version", "0"); setup(); retro_init(); CHK(retro_load_game(&g1) && g_m.nvram[5] == 0, "nvram: compartilhada comeca vazia");
        g_m.nvram[7] = 0x44; retro_unload_game(); retro_deinit();
        snprintf(f, sizeof f, "%s/phoenix/shared/nvram.0.srm", sd); q = fopen(f, "rb"); CHK(q != NULL, "nvram: compartilhada grava <saves>/phoenix/shared/nvram.0.srm"); if (q) fclose(q);
        n_ov = 0; savedir[0] = 0;
    }

    /* ---- arquivos: caminho com acento/espaco e VFS do frontend (ph_file) ---- */
    {   char dir[700], iso[1400], sd[700], f[1500]; struct retro_game_info gv;
        snprintf(dir, sizeof dir, "%s/Jogos " T_ACAO, sysdir);          /* UTF-8, com espaco e acento, como o RetroArch entrega */
        snprintf(cmd, sizeof cmd, CMD_MKDIRQ, dir); if (system(cmd)) return 2;
        snprintf(iso, sizeof iso, "%s/Jogo " T_OTIMO ".iso", dir);
        { FILE *fi = fopen(iso, "wb"); for (int i = 0; fi && i < 6 * 2048; i++) fputc((i / 2048) + 7, fi); if (fi) fclose(fi); }
        gv.path = iso; gv.data = NULL; gv.size = 0; gv.meta = NULL;
        write_bios(sysdir);
        for (unsigned ver = 0; ver <= 3; ver += (ver == 0 ? 1 : 2)) {            /* 0 = sem VFS (stdio), 1 = VFS v1, 3 = VFS v3 */
            char lab[120]; uint8_t sb[2048]; int rd; const unsigned eff = T_VFS_ON ? ver : 0;   /* versao do VFS que o nucleo realmente usa */
            vfs_max_ver = ver; vfs_reset_counters(); savedir[0] = 0; n_ov = 0;
            setup(); retro_init();
            snprintf(lab, sizeof lab, "arquivos (VFS %u): ph_file escolheu a interface certa", ver);
            CHK(ph_file_using_vfs() == (int)eff, lab);
            snprintf(lab, sizeof lab, "arquivos (VFS %u): ISO em pasta com acento e espaco abre", ver);
            CHK(retro_load_game(&gv), lab);
            rd = cd_read_sector(&g_m.cd, 4, sb);
            snprintf(lab, sizeof lab, "arquivos (VFS %u): setor da ISO com acento tem o conteudo certo", ver);
            CHK(g_m.cd.num_sectors == 6 && rd && sb[0] == 11 && sb[2047] == 11, lab);
            snprintf(lab, sizeof lab, "arquivos (VFS %u): BIOS e ISO %s o VFS", ver, eff ? "passaram por" : "nao tocaram em");
            CHK(eff ? vfs_open_n >= 2 : vfs_open_n == 0, lab);
            retro_unload_game(); retro_deinit();

            /* NVRAM: pasta de saves ainda inexistente, tmp + rename pelo VFS */
            snprintf(sd, sizeof sd, "%s/Saves " T_ACAO " %u", sysdir, ver);
            snprintf(cmd, sizeof cmd, CMD_MKDIRQ, sd); if (system(cmd)) return 2;
            snprintf(savedir, sizeof savedir, "%s", sd); vfs_reset_counters(); n_ov = 0;
            setup(); retro_init();
            CHK(retro_load_game(&gv), "arquivos: carrega para gravar a NVRAM");
            g_m.nvram[9] = 0x5a; retro_unload_game(); retro_deinit();
            snprintf(f, sizeof f, "%s/phoenix/per_game/Jogo " T_OTIMO ".0.srm", sd);
            { FILE *q = fopen(f, "rb"); uint8_t b[0x8000]; int ok = q && fread(b, 1, sizeof b, q) == sizeof b && b[9] == 0x5a; if (q) fclose(q);
              snprintf(lab, sizeof lab, "arquivos (VFS %u): NVRAM gravada em caminho com acento", ver); CHK(ok, lab); }
            if (eff) {
                snprintf(lab, sizeof lab, "arquivos (VFS %u): NVRAM gravada pelo VFS (open w + remove + rename)", ver);
                CHK(vfs_write_open_n >= 1 && vfs_remove_n >= 1 && vfs_rename_n >= 1, lab);
                snprintf(lab, sizeof lab, "arquivos (VFS %u): pastas %s", ver, eff >= 3 ? "criadas pelo mkdir do VFS" : "criadas pelo stdio (VFS v1 nao tem mkdir)");
                CHK(eff >= 3 ? vfs_mkdir_n >= 1 : vfs_mkdir_n == 0, lab);
            }
            setup(); retro_init(); CHK(retro_load_game(&gv) && g_m.nvram[9] == 0x5a, "arquivos: recarrega a NVRAM gravada"); retro_unload_game(); retro_deinit();
        }
        vfs_max_ver = 0; savedir[0] = 0; n_ov = 0;
    }

    /* ---- sem BIOS ---- */
    refuse_xrgb = 0; snprintf(cmd, sizeof cmd, CMD_RESET, sysdir, sysdir); if (system(cmd)) return 2;
    setup(); retro_init();
    CHK(retro_load_game(NULL), "sem BIOS no disco: carrega a FZ-1 incorporada ao nucleo");
    retro_run();  /* roda com a BIOS incorporada */
    CHK(vdata == NULL || frames_out > 0, "retro_run sem maquina nao quebra");
    retro_deinit();

    /* ---- imagem de disco inexistente ---- */
    write_bios(sysdir); setup(); retro_init();
    struct retro_game_info gi = { "/tmp/nao_existe.iso", NULL, 0, NULL };
    CHK(!retro_load_game(&gi), "imagem inexistente: false");
    retro_deinit();

    /* ---- imagem de disco valida: vai para o CD no XBUS (PARTE20) ---- */
    {   char iso[1300]; snprintf(iso, sizeof iso, "%s/teste.iso", sysdir);
        FILE *f = fopen(iso, "wb");
        for (int i = 0; f && i < 12 * 2048; i++) fputc((i / 2048) + 1, f);
        if (f) fclose(f);
        write_bios(sysdir); setup(); retro_init();
        struct retro_game_info gj = { iso, NULL, 0, NULL };
        CHK(retro_load_game(&gj), "retro_load_game com ISO");
        {   uint8_t sb[2048]; int rd = cd_read_sector(&g_m.cd, 3, sb);
            CHK(g_m.cd.num_sectors == 12 && rd && sb[0] == 4 && sb[2047] == 4, "ISO carregada no CD (12 setores, conteudo certo, lido sob demanda)"); }
        CHK(g_m.clio.dev[0] == &g_m.cd_dev, "CD ligado no XBUS ID 0");
        retro_unload_game();
        retro_deinit();
    }

    snprintf(cmd, sizeof cmd, CMD_RMDIR, sysdir); if (system(cmd)) return 2;
    printf("test_libretro: %d verificacoes, %d falhas\n", total, fails);
    return fails ? 1 : 0;
}
