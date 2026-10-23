/* test_libretro.c - exercita o front-end como o RetroArch faria (inclui libretro.c para ver os estaticos).
 * Uso: ./test_libretro [diretorio_system]   (sem argumento usa uma BIOS sintetica "B ." num diretorio temporario)
 * Com diretorio_system (ex.: .) procura fz10.bin etc. em ./BIOS e roda 120 quadros so para ver que nao trava. */
#include "libretro.c"
#include <unistd.h>
/* system() no Windows usa cmd.exe: "mkdir -p" e "rm -rf" nao existem la; usa comandos nativos e diretorio relativo */
#ifdef _WIN32
#define SYSDIR_FMT   "ph_sys_%d"
#define CMD_MKDIR    "mkdir \"%s\""
#define CMD_RESET    "rmdir /s /q \"%s\" 2>nul & mkdir \"%s\""
#define CMD_RMDIR    "rmdir /s /q \"%s\""
#else
#define SYSDIR_FMT   "/tmp/ph_sys_%d"
#define CMD_MKDIR    "mkdir -p %s"
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

static bool env(unsigned cmd, void *data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = sysdir; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixfmt_offer = *(int *)data;
        if (refuse_xrgb && pixfmt_offer == RETRO_PIXEL_FORMAT_XRGB8888) return false;
        return pixfmt_offer == RETRO_PIXEL_FORMAT_XRGB8888 || pixfmt_offer == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: case RETRO_ENVIRONMENT_SET_VARIABLES: return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        if (!strcmp(v->key, "phoenix_region")) { v->value = opt_region; return true; }
        if (!strcmp(v->key, "phoenix_pixel_format")) { v->value = opt_fmt; return true; }
        if (!strcmp(v->key, "phoenix_bios")) { v->value = opt_bios; return true; }
        return false; }
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
    if (argc > 1) {
        strncpy(sysdir, argv[1], sizeof sysdir - 1); setup(); retro_init();
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
    CHK(av.geometry.base_width == 320 && av.geometry.base_height == 240 && av.geometry.max_height == 288 && av.geometry.max_width == 384, "geometria NTSC 320x240 (max 384x288)");
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

    /* NVRAM */
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

    /* ---- sem BIOS ---- */
    refuse_xrgb = 0; snprintf(cmd, sizeof cmd, CMD_RESET, sysdir, sysdir); if (system(cmd)) return 2;
    setup(); retro_init();
    CHK(!retro_load_game(NULL), "sem BIOS: retro_load_game devolve false");
    retro_run();  /* nao pode quebrar sem maquina */
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
