/* test_perifericos.c - testa os 6 tipos de controle (joybus.c) ligados ao front-end (libretro.c), como o RetroArch faria.
 * Compilar:  gcc -O2 -std=gnu99 -Ilibretro-common/include test_perifericos.c machine.c arm60.c clio.c madam.c sport.c vdlp.c cel.c cel_opera.c \
 *            celexec.c cd.c cd_chd.c dsp.c joybus.c ph_audit.c ph_fixedpoint.c ph_swi_hle.c chdobj/*.o -lm -o test_perifericos   (ou:  make test_perifericos) */
#include "libretro.c"
#include <unistd.h>

static int fails, total;
#define CHK(c, msg) do { total++; if (!(c)) { fails++; printf("FALHOU: %s (linha %d)\n", msg, __LINE__); } } while (0)

static char sysdir[512];
static const void *vdata; static unsigned vw, vh; static size_t vpitch;
static const char *opt_fmt = "xrgb8888", *opt_active = "1", *opt_hide = "disabled", *opt_borders = "off";
static int16_t g_pad[JOYBUS_PORTS], g_lgx[JOYBUS_PORTS], g_lgy[JOYBUS_PORTS]; static int g_trig[JOYBUS_PORTS], g_mouse_dx;
static const struct retro_controller_info *g_ci; static unsigned g_ndesc; static unsigned g_max_users;

static bool env(unsigned cmd, void *data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = sysdir; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return *(int *)data == RETRO_PIXEL_FORMAT_XRGB8888 || *(int *)data == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: case RETRO_ENVIRONMENT_SET_VARIABLES: case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: return true;
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: g_ci = data; return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: { const struct retro_input_descriptor *d = data; g_ndesc = 0; while (d[g_ndesc].description) g_ndesc++; return true; }
    case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS: if (!g_max_users) return false; *(unsigned *)data = g_max_users; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        if (!strcmp(v->key, "phoenix_pixel_format")) { v->value = opt_fmt; return true; }
        if (!strcmp(v->key, "phoenix_active_devices")) { v->value = opt_active; return true; }
        if (!strcmp(v->key, "phoenix_hide_lightgun_crosshairs")) { v->value = opt_hide; return true; }
        if (!strcmp(v->key, "phoenix_borders")) { v->value = opt_borders; return true; }
        return false; }
    }
    return false;
}
static void vid(const void *d, unsigned w, unsigned h, size_t p) { vdata = d; vw = w; vh = h; vpitch = p; }
static size_t aud(const int16_t *d, size_t n) { (void)d; return n; }
static void poll(void) {}
static int16_t inp(unsigned port, unsigned dev, unsigned idx, unsigned id) {
    if (port >= JOYBUS_PORTS) return 0;
    if (dev == RETRO_DEVICE_JOYPAD) return (g_pad[port] >> id) & 1;
    if (dev == RETRO_DEVICE_LIGHTGUN) { if (id == RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X) return g_lgx[port]; if (id == RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y) return g_lgy[port]; if (id == RETRO_DEVICE_ID_LIGHTGUN_TRIGGER) return g_trig[port]; return 0; }
    if (dev == RETRO_DEVICE_MOUSE && id == RETRO_DEVICE_ID_MOUSE_X) return (int16_t)g_mouse_dx;
    (void)idx; return 0;
}
static void setup(void) { retro_set_environment(env); retro_set_video_refresh(vid); retro_set_audio_sample_batch(aud); retro_set_input_poll(poll); retro_set_input_state(inp); }
static void write_bios(const char *dir) {
    char p[600]; snprintf(p, sizeof p, "%s/fz10.bin", dir);
    FILE *f = fopen(p, "wb"); uint8_t *b = calloc(1, 0x100000);
    b[0] = 0xEA; b[1] = 0xFF; b[2] = 0xFF; b[3] = 0xFE;           /* B . (branch para si mesmo) */
    fwrite(b, 1, 0x100000, f); fclose(f); free(b);
}
static void boot(void) { setup(); retro_init(); CHK(retro_load_game(NULL), "carrega (BIOS sintetica)"); }
static void end(void) { retro_unload_game(); retro_deinit(); }
static unsigned cross_px(unsigned x, unsigned y) { return vdata ? (g_fmt == LR_XRGB8888 ? ((const uint32_t *)vdata)[y * vw + x] : ((const uint16_t *)vdata)[y * vw + x]) : 0xdeadu; }

int main(void) {
    snprintf(sysdir, sizeof sysdir, "/tmp/ph_perif_%d", (int)getpid());
    { char c[700]; snprintf(c, sizeof c, "rm -rf %s && mkdir -p %s", sysdir, sysdir); if (system(c)) return 2; }
    write_bios(sysdir);

    /* 1) menu de controles: 8 portas x 6 tipos, nomes e valores do Opera */
    setup(); retro_init();
    CHK(g_ci != NULL, "SET_CONTROLLER_INFO enviado");
    { int np = 0; while (g_ci && g_ci[np].types) np++;
      CHK(np == 8, "8 portas no menu de controles");
      CHK(g_ci && g_ci[0].num_types == 6 && g_ci[7].num_types == 6, "6 tipos por porta");
      CHK(g_ci && !strcmp(g_ci[0].types[1].desc, "3DO Flightstick") && g_ci[0].types[1].id == JOYBUS_DEV_FLIGHTSTICK, "flightstick no menu");
      CHK(g_ci && !strcmp(g_ci[0].types[4].desc, "Arcade Lightgun") && g_ci[0].types[4].id == RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 0), "arcade lightgun no menu");
      CHK(g_ci && !strcmp(g_ci[0].types[5].desc, "Orbatak Trackball") && g_ci[0].types[5].id == RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1), "Orbatak no menu"); }
    retro_deinit();

    /* 2) arcade lightgun no P1: cadeia PBUS + mira desenhada no centro da imagem */
    opt_active = "1"; opt_hide = "disabled"; boot();
    retro_set_controller_port_device(0, JOYBUS_DEV_ARCADE_LIGHTGUN);
    CHK(g_ndesc == 7, "descricoes do arcade lightgun (7)");
    g_lgx[0] = 0; g_lgy[0] = 0; g_trig[0] = 1;
    retro_run();
    CHK(g_m.joybus.buf[0] == 0x4D && (g_m.joybus.buf[1] & 0x80), "PBUS: id 0x4D + gatilho");
    CHK(g_m.joybus.buf[4] == 0x1F || (g_m.joybus.buf[4] & 0x1F) == 0x1F, "PBUS: hit count 0x1F");
    CHK(vw == 320 && vh == 240, "320x240");
    CHK(cross_px(160, 120) == 0x000000FFu, "mira azul (P1) no centro");
    CHK(cross_px(159, 120) == 0x000000FFu && cross_px(161, 120) == 0x000000FFu && cross_px(160, 119) == 0x000000FFu && cross_px(160, 121) == 0x000000FFu, "cruz completa");
    CHK(cross_px(159, 119) != 0x000000FFu, "so a cruz (diagonal fica de fora)");
    g_lgx[0] = -32768; g_lgy[0] = -32768; retro_run();
    CHK(cross_px(0, 0) == 0x000000FFu, "mira no canto superior esquerdo");
    end();

    /* 3) Hide Lightgun Crosshairs */
    opt_hide = "enabled"; boot();
    retro_set_controller_port_device(0, JOYBUS_DEV_LIGHTGUN); g_lgx[0] = 0; g_lgy[0] = 0; g_trig[0] = 0; retro_run();
    CHK(g_m.joybus.buf[0] == 0x4D, "lightgun na cadeia mesmo com a mira escondida");
    CHK(cross_px(160, 120) != 0x000000FFu, "mira escondida pela opcao");
    end(); opt_hide = "disabled";

    /* 4) RGB565: mesma mira convertida */
    opt_fmt = "rgb565"; boot();
    retro_set_controller_port_device(0, JOYBUS_DEV_LIGHTGUN); g_lgx[0] = 0; g_lgy[0] = 0; retro_run();
    CHK(g_fmt == LR_RGB565 && cross_px(160, 120) == 0x001Fu, "mira em RGB565");
    end(); opt_fmt = "xrgb8888";

    /* 5) joypad + mouse (2 dispositivos, Active Input Devices = 2): ordem das portas na cadeia */
    opt_active = "2"; boot();
    retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD); retro_set_controller_port_device(1, RETRO_DEVICE_MOUSE);
    g_pad[0] = (1 << RETRO_DEVICE_ID_JOYPAD_UP) | (1 << RETRO_DEVICE_ID_JOYPAD_Y); g_mouse_dx = 5;
    retro_run();
    CHK(g_m.joybus.buf[0] == 0x89 && g_m.joybus.buf[1] == 0x00, "P1 joypad (Cima+A) = 89 00");
    CHK(g_m.joybus.buf[2] == 0x49 && g_m.joybus.buf[5] == 0x05, "P2 mouse: id 0x49 e X=5");
    CHK(g_m.joybus.buf[6] == 0xFF, "cadeia termina em 0xFF");
    /* a cadeia chega ao 3DO pela DMA8: mesmo codigo do c_mctl */
    { uint32_t w[JOYBUS_BUFFER_WORDS]; joybus_get_pbus_buffer(&g_m.joybus, 0, w); CHK(w[0] == 0x89004900u, "palavra 0 da DMA (big-endian)"); }
    /* limite por Active Input Devices e por maximo de usuarios do frontend */
    g_max_users = 1; apply_port_devices(); retro_run();
    CHK(g_m.joybus.buf[0] == 0x89 && g_m.joybus.buf[2] == 0xFF, "GET_INPUT_MAX_USERS=1 corta o P2");
    g_max_users = 0;
    retro_set_controller_port_device(1, RETRO_DEVICE_NONE); apply_port_devices(); retro_run();
    CHK(g_m.joybus.buf[2] == 0xFF, "porta NONE nao entra na cadeia");
    retro_set_controller_port_device(1, 0x7777); retro_run();       /* tipo desconhecido vira joypad */
    CHK(g_port_dev[1] == RETRO_DEVICE_JOYPAD && g_m.joybus.buf[2] == 0x80, "tipo desconhecido vira joypad");
    retro_set_controller_port_device(8, RETRO_DEVICE_MOUSE);        /* porta fora de 0..7: ignorada */
    CHK(g_port_dev[0] == RETRO_DEVICE_JOYPAD, "porta 8 ignorada");
    end(); opt_active = "1";

    /* 6) 8 portas ligadas ao mesmo tempo: a cadeia aguenta (8 joypads = 16 bytes) e o Orbatak acrescenta o bloco de botoes no fim */
    opt_active = "8"; boot();
    for (unsigned p = 0; p < 8; p++) retro_set_controller_port_device(p, RETRO_DEVICE_JOYPAD);
    retro_run(); CHK(g_m.joybus.buf[14] == 0x80 && g_m.joybus.buf[16] == 0xFF, "8 joypads = 16 bytes");
    retro_set_controller_port_device(0, JOYBUS_DEV_ORBATAK); g_pad[0] = 1 << RETRO_DEVICE_ID_JOYPAD_L; retro_run();
    CHK(g_m.joybus.buf[0] == 0x49, "Orbatak: trackball na porta 0");
    { int found = 0; for (int i = 0; i < 40; i++) if (g_m.joybus.buf[i] == 0xC0) { found = i; break; }
      CHK(found > 0 && (g_m.joybus.buf[found + 2] & 0x20), "Orbatak: bloco de botoes 0xC0 no fim da cadeia, com a moeda do P1"); }
    end(); opt_active = "1";

    /* 7) com bordas ligadas a janela da imagem encolhe: o centro continua no centro, o canto da tela fica fora da imagem */
    opt_borders = "on"; boot();
    CHK(g_m.joybus.view_l == -32768, "antes do 1o quadro: tela inteira");
    retro_set_controller_port_device(0, JOYBUS_DEV_ARCADE_LIGHTGUN); retro_run();
    CHK(g_m.joybus.view_l > -32768 && g_m.joybus.view_r < 32768, "janela da imagem menor que a tela com bordas");
    end(); opt_borders = "off";

    { char c[700]; snprintf(c, sizeof c, "rm -rf %s", sysdir); if (system(c)) {} }
    printf("test_perifericos: %d verificacoes, %d falhas\n", total, fails);
    return fails != 0;
}
