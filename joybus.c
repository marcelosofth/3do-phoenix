/* joybus.c - Joybus/PBUS do 3DO (controles), portado do opera_pbus.c / lr_input.c do opera-libretro.
 *
 * O 3DO nao tem registradores de controle mapeados na CPU: o Portfolio OS (ControlPort.c) programa a DMA8 da
 * PBUS (MADAM MCTL PLAYXEN) e le da RAM uma cadeia de dispositivos (machine.c, c_mctl). Cada dispositivo
 * conectado acrescenta seus bytes a cadeia, na ordem das portas (porta 0 = jogador 1, porta 1 = jogador 2...).
 * A cadeia e completada com 0xFF.
 *
 *   joypad (2 bytes)       1 0 0 D U R L A  |  B C P X RT LT 0 0
 *   flightstick (9 bytes)  01 7B 08 + X/Y/Z de 10 bits + botoes
 *   mouse (4 bytes)        0x49 + botoes + X/Y de 10 bits (deslocamento)
 *   lightgun (5 bytes)     0x4D + trigger/option + contador de varredura de 20 bits + "hit count"
 *   arcade lightgun (5)    0x4D + trigger/service/coins/start/holster + contador + hit count
 *   Orbatak (4 + 3 bytes)  trackball (0x49) por porta + UM bloco de botoes (0xC0) no fim da cadeia
 *
 * Mapeamento RetroPad -> 3DO (joypad, igual ao do Opera):
 *   RetroPad Y -> A      RetroPad B -> B      RetroPad A -> C
 *   RetroPad Start ou X -> P (Play/Pause)      RetroPad Select -> X (Stop)
 *   RetroPad L -> LT     RetroPad R -> RT     D-pad -> D/U/L/R
 * Os demais dispositivos usam o mesmo mapeamento do Opera (veja ph_input_descriptors em libretro.c).
 */
#include "joybus.h"
#include <string.h>

#define PBUS_FLIGHTSTICK_ID_0       0x01
#define PBUS_FLIGHTSTICK_ID_1       0x7B
#define PBUS_JOYPAD_ID              0x80
#define PBUS_MOUSE_ID               0x49
#define PBUS_LIGHTGUN_ID            0x4D
#define PBUS_ORBATAK_TRACKBALL_ID   PBUS_MOUSE_ID
#define PBUS_ORBATAK_BUTTONS_ID     0xC0
#define PBUS_END_CHAIN_ID           0xFF

#define PBUS_JOYPAD_SHIFT_LT        0x02
#define PBUS_JOYPAD_SHIFT_RT        0x03
#define PBUS_JOYPAD_SHIFT_X         0x04
#define PBUS_JOYPAD_SHIFT_P         0x05
#define PBUS_JOYPAD_SHIFT_C         0x06
#define PBUS_JOYPAD_SHIFT_B         0x07
#define PBUS_JOYPAD_SHIFT_A         0x00
#define PBUS_JOYPAD_SHIFT_L         0x01
#define PBUS_JOYPAD_SHIFT_R         0x02
#define PBUS_JOYPAD_SHIFT_U         0x03
#define PBUS_JOYPAD_SHIFT_D         0x04

#define PBUS_FLIGHTSTICK_SHIFT_FIRE 0x07
#define PBUS_FLIGHTSTICK_SHIFT_A    0x06
#define PBUS_FLIGHTSTICK_SHIFT_B    0x05
#define PBUS_FLIGHTSTICK_SHIFT_C    0x04
#define PBUS_FLIGHTSTICK_SHIFT_U    0x03
#define PBUS_FLIGHTSTICK_SHIFT_D    0x02
#define PBUS_FLIGHTSTICK_SHIFT_R    0x01
#define PBUS_FLIGHTSTICK_SHIFT_L    0x00
#define PBUS_FLIGHTSTICK_SHIFT_P    0x07
#define PBUS_FLIGHTSTICK_SHIFT_X    0x06
#define PBUS_FLIGHTSTICK_SHIFT_LT   0x05
#define PBUS_FLIGHTSTICK_SHIFT_RT   0x04

#define PBUS_MOUSE_SHIFT_LEFT       7
#define PBUS_MOUSE_SHIFT_MIDDLE     6
#define PBUS_MOUSE_SHIFT_RIGHT      5
#define PBUS_MOUSE_SHIFT_SHIFT      4

#define PBUS_LG_SHIFT_TRIGGER       7
#define PBUS_LG_SHIFT_SERVICE       6
#define PBUS_LG_SHIFT_COINS         5
#define PBUS_LG_SHIFT_START         4
#define PBUS_LG_SHIFT_HOLSTER       3
#define PBUS_LG_SHIFT_OPTION        3

#define PBUS_LG_NTSC_XSCANTIME      1030
#define PBUS_LG_NTSC_YSCANTIME      12707
#define PBUS_LG_NTSC_TIMEOFFSET     (-12835)
#define PBUS_LG_NTSC_WIDTH          320
#define PBUS_LG_COUNTER_MASK        0x000FFFFF
#define PBUS_LG_HIT_COUNT           0x1F

#define PBUS_ORBATAK_SHIFT_COIN_P1  5
#define PBUS_ORBATAK_SHIFT_COIN_P2  4
#define PBUS_ORBATAK_SHIFT_START_P1 1
#define PBUS_ORBATAK_SHIFT_START_P2 2
#define PBUS_ORBATAK_SHIFT_SERVICE  0

/* fiacao da cadeia sendo montada (uma por joybus_poll) */
typedef struct { uint32_t idx; uint8_t buf[JOYBUS_BUF_BYTES]; } chain_t;

static joybus_t g_joybus = {0};

static int reserve(const chain_t *c, uint32_t len) { return (c->idx + len) <= JOYBUS_BUF_BYTES; }

/* ------------------------------------------------------------------ montagem dos blocos (opera_pbus.c) */
static uint16_t pos10(const int32_t pos) {
    int32_t rv = (pos + 32768) / 64;
    if (rv < 0) return 0;
    if (rv > 1023) return 1023;
    return (uint16_t)rv;
}

static uint32_t lightgun_counter(const int32_t x_, const int32_t y_) {
    int32_t x = x_, y = y_, counter;
    if (x < 0) x = 0;
    if (x > 319) x = 319;
    if (y < 0) y = 0;
    if (y > 239) y = 239;
    counter = (((y * PBUS_LG_NTSC_YSCANTIME) + ((x * PBUS_LG_NTSC_XSCANTIME * 10) / PBUS_LG_NTSC_WIDTH)) / 10);
    return ((uint32_t)(counter - PBUS_LG_NTSC_TIMEOFFSET) & PBUS_LG_COUNTER_MASK);
}

typedef struct { uint8_t u, d, l, r, x, p, a, b, c, lt, rt; } joypad_t;
typedef struct { uint8_t fire, a, b, c, u, d, l, r, p, x, lt, rt; int32_t h_pos, v_pos, z_pos; } flightstick_t;
typedef struct { uint8_t left, middle, right, shift; int16_t x, y; } mouse_t;
typedef struct { uint8_t trigger, option, reload; int16_t x, y; } lightgun_t;
typedef struct { uint8_t trigger, service, coins, start, holster; int16_t x, y; } arcade_lightgun_t;
typedef struct { int16_t x, y; } orbatak_trackball_t;
typedef struct { uint8_t start_p1, start_p2, coin_p1, coin_p2, service; } orbatak_buttons_t;

static void add_joypad(chain_t *c, const joypad_t *jp) {
    if (!reserve(c, 2)) return;
    c->buf[c->idx++] = (uint8_t)(PBUS_JOYPAD_ID | (jp->d << PBUS_JOYPAD_SHIFT_D) | (jp->u << PBUS_JOYPAD_SHIFT_U) |
                                 (jp->r << PBUS_JOYPAD_SHIFT_R) | (jp->l << PBUS_JOYPAD_SHIFT_L) | (jp->a << PBUS_JOYPAD_SHIFT_A));
    c->buf[c->idx++] = (uint8_t)((jp->b << PBUS_JOYPAD_SHIFT_B) | (jp->c << PBUS_JOYPAD_SHIFT_C) | (jp->p << PBUS_JOYPAD_SHIFT_P) |
                                 (jp->x << PBUS_JOYPAD_SHIFT_X) | (jp->rt << PBUS_JOYPAD_SHIFT_RT) | (jp->lt << PBUS_JOYPAD_SHIFT_LT));
}

static void add_flightstick(chain_t *c, const flightstick_t *fs) {
    if (!reserve(c, 9)) return;
    uint16_t x = pos10(fs->h_pos), y = pos10(fs->v_pos), z = pos10(fs->z_pos);
    c->buf[c->idx++] = PBUS_FLIGHTSTICK_ID_0;
    c->buf[c->idx++] = PBUS_FLIGHTSTICK_ID_1;
    c->buf[c->idx++] = 0x08;
    c->buf[c->idx++] = (uint8_t)(x >> 2);
    c->buf[c->idx++] = (uint8_t)(((x & 0x003) << 6) | ((y & 0x3F0) >> 4));
    c->buf[c->idx++] = (uint8_t)(((y & 0x00F) << 4) | ((z & 0x3C0) >> 6));
    c->buf[c->idx++] = (uint8_t)(((z & 0x03F) << 2) | 0x02);
    c->buf[c->idx++] = (uint8_t)((fs->fire << PBUS_FLIGHTSTICK_SHIFT_FIRE) | (fs->a << PBUS_FLIGHTSTICK_SHIFT_A) | (fs->b << PBUS_FLIGHTSTICK_SHIFT_B) |
                                 (fs->c << PBUS_FLIGHTSTICK_SHIFT_C) | (fs->u << PBUS_FLIGHTSTICK_SHIFT_U) | (fs->d << PBUS_FLIGHTSTICK_SHIFT_D) |
                                 (fs->r << PBUS_FLIGHTSTICK_SHIFT_R) | (fs->l << PBUS_FLIGHTSTICK_SHIFT_L));
    c->buf[c->idx++] = (uint8_t)((fs->p << PBUS_FLIGHTSTICK_SHIFT_P) | (fs->x << PBUS_FLIGHTSTICK_SHIFT_X) |
                                 (fs->lt << PBUS_FLIGHTSTICK_SHIFT_LT) | (fs->rt << PBUS_FLIGHTSTICK_SHIFT_RT));
}

static void add_mouse(chain_t *c, const mouse_t *m) {
    if (!reserve(c, 4)) return;
    c->buf[c->idx++] = PBUS_MOUSE_ID;
    c->buf[c->idx++] = (uint8_t)((m->left << PBUS_MOUSE_SHIFT_LEFT) | (m->middle << PBUS_MOUSE_SHIFT_MIDDLE) |
                                 (m->right << PBUS_MOUSE_SHIFT_RIGHT) | (m->shift << PBUS_MOUSE_SHIFT_SHIFT) | ((m->y & 0x3C0) >> 6));
    c->buf[c->idx++] = (uint8_t)(((m->y & 0x03F) << 2) | ((m->x & 0x300) >> 8));
    c->buf[c->idx++] = (uint8_t)(m->x & 0xFF);
}

/* Igual ao driver de lightgun do Portfolio OS: contador de varredura de 20 bits + hit count. */
static void add_lightgun(chain_t *c, const lightgun_t *lg) {
    int32_t x, y; uint32_t counter; uint8_t hit_count, trigger;
    if (!reserve(c, 5)) return;
    trigger = lg->trigger;
    if (lg->reload) {
        /* alguns jogos usam contador zero como "sem acerto" */
        counter = 0; trigger = 1; hit_count = 0;
    } else {
        x = (lg->x + (32 * 1024)); y = (lg->y + (32 * 1024));
        x = (int32_t)(x / (65535.0 / 320.0)); y = (int32_t)(y / (65535.0 / 240.0));
        counter = lightgun_counter(x, y);
        hit_count = PBUS_LG_HIT_COUNT;
    }
    c->buf[c->idx++] = PBUS_LIGHTGUN_ID;
    c->buf[c->idx++] = (uint8_t)((trigger << PBUS_LG_SHIFT_TRIGGER) | (lg->option << PBUS_LG_SHIFT_OPTION) | ((counter & 0x80000) >> 19));
    c->buf[c->idx++] = (uint8_t)((counter & 0x7F800) >> 11);
    c->buf[c->idx++] = (uint8_t)((counter & 0x007F8) >> 3);
    c->buf[c->idx++] = (uint8_t)(((counter & 0x00007) << 5) | hit_count);
}

static void add_arcade_lightgun(chain_t *c, const arcade_lightgun_t *lg) {
    int32_t x, y; uint32_t counter; uint8_t hit_count;
    if (!reserve(c, 5)) return;
    if (lg->holster) { x = 0; y = 0; hit_count = 0; }
    else {
        x = (lg->x + (32 * 1024)); y = (lg->y + (32 * 1024));
        x = (int32_t)(x / (65535.0 / 320.0)); y = (int32_t)(y / (65535.0 / 240.0));
        hit_count = PBUS_LG_HIT_COUNT;
    }
    counter = lightgun_counter(x, y);
    c->buf[c->idx++] = PBUS_LIGHTGUN_ID;
    c->buf[c->idx++] = (uint8_t)((lg->trigger << PBUS_LG_SHIFT_TRIGGER) | (lg->service << PBUS_LG_SHIFT_SERVICE) | (lg->coins << PBUS_LG_SHIFT_COINS) |
                                 (lg->start << PBUS_LG_SHIFT_START) | (lg->holster << PBUS_LG_SHIFT_HOLSTER) | ((counter & 0x80000) >> 19));
    c->buf[c->idx++] = (uint8_t)((counter & 0x7F800) >> 11);
    c->buf[c->idx++] = (uint8_t)((counter & 0x007F8) >> 3);
    c->buf[c->idx++] = (uint8_t)(((counter & 0x00007) << 5) | hit_count);
}

static void add_orbatak_trackball(chain_t *c, const orbatak_trackball_t *tb) {
    if (!reserve(c, 4)) return;
    c->buf[c->idx++] = PBUS_ORBATAK_TRACKBALL_ID;
    c->buf[c->idx++] = (uint8_t)((tb->y & 0x3C0) >> 6);
    c->buf[c->idx++] = (uint8_t)(((tb->y & 0x03F) << 2) | ((tb->x & 0x300) >> 8));
    c->buf[c->idx++] = (uint8_t)(tb->x & 0xFF);
}

static void add_orbatak_buttons(chain_t *c, const orbatak_buttons_t *b) {
    if (!reserve(c, 3)) return;
    c->buf[c->idx++] = PBUS_ORBATAK_BUTTONS_ID;
    c->buf[c->idx++] = 0;
    c->buf[c->idx++] = (uint8_t)((b->coin_p1 << PBUS_ORBATAK_SHIFT_COIN_P1) | (b->coin_p2 << PBUS_ORBATAK_SHIFT_COIN_P2) |
                                 (b->start_p1 << PBUS_ORBATAK_SHIFT_START_P1) | (b->start_p2 << PBUS_ORBATAK_SHIFT_START_P2) |
                                 (b->service << PBUS_ORBATAK_SHIFT_SERVICE));
}

/* ------------------------------------------------------------------ leitura do frontend (lr_input.c) */
/* Fallback de teclado (jogador 1, so joypad), independente do mapeamento do RetroArch. Teclas = padrao do RetroArch:
 *   setas = direcional | Z = B | X = C | A = A | Enter = P (Start) | Shift direito = X (Stop) | Q = L | W = R */
static int kbd_key_for(unsigned id) {
    switch (id) {
    case RETRO_DEVICE_ID_JOYPAD_UP:     return RETROK_UP;
    case RETRO_DEVICE_ID_JOYPAD_DOWN:   return RETROK_DOWN;
    case RETRO_DEVICE_ID_JOYPAD_LEFT:   return RETROK_LEFT;
    case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return RETROK_RIGHT;
    case RETRO_DEVICE_ID_JOYPAD_B:      return RETROK_z;        /* 3DO B */
    case RETRO_DEVICE_ID_JOYPAD_A:      return RETROK_x;        /* 3DO C */
    case RETRO_DEVICE_ID_JOYPAD_Y:      return RETROK_a;        /* 3DO A */
    case RETRO_DEVICE_ID_JOYPAD_X:      return RETROK_s;        /* 3DO P */
    case RETRO_DEVICE_ID_JOYPAD_START:  return RETROK_RETURN;   /* 3DO P */
    case RETRO_DEVICE_ID_JOYPAD_SELECT: return RETROK_RSHIFT;   /* 3DO X */
    case RETRO_DEVICE_ID_JOYPAD_L:      return RETROK_q;        /* 3DO LT */
    case RETRO_DEVICE_ID_JOYPAD_R:      return RETROK_w;        /* 3DO RT */
    }
    return 0;
}
static int16_t st(const joybus_t *j, uint32_t port, unsigned dev, unsigned idx, unsigned id) {
    return j->input_state_cb ? j->input_state_cb(port, dev, idx, id) : 0;
}
static unsigned rp(const joybus_t *j, uint32_t port, unsigned id) {            /* botao do RetroPad (0/1) */
    if (st(j, port, RETRO_DEVICE_JOYPAD, 0, id) != 0) return 1;
    return 0;
}
static unsigned rp_kbd(const joybus_t *j, uint32_t port, unsigned id) {        /* idem + teclado no jogador 1 (so o joypad 3DO) */
    if (rp(j, port, id)) return 1;
    if (port == 0 && j->input_state_cb) { int k = kbd_key_for(id); if (k && j->input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, (unsigned)k)) return 1; }
    return 0;
}
static int16_t an(const joybus_t *j, uint32_t port, unsigned index, unsigned id) { return st(j, port, RETRO_DEVICE_ANALOG, index, id); }
static unsigned ms(const joybus_t *j, uint32_t port, unsigned id)  { return st(j, port, RETRO_DEVICE_MOUSE, 0, id) != 0; }
static unsigned lg(const joybus_t *j, uint32_t port, unsigned id)  { return st(j, port, RETRO_DEVICE_LIGHTGUN, 0, id) != 0; }

/* leva a posicao bruta do frontend (tela inteira) para o espaco da imagem do 3DO (so muda algo com bordas / fundo expandido) */
static int16_t view_map(int32_t v, int32_t lo, int32_t hi) {
    int64_t r;
    if (lo == -32768 && hi == 32768) { r = v; }
    else { r = ((int64_t)(v - lo) * 65536) / (hi - lo) - 32768; }
    if (r < -32768) r = -32768;
    if (r > 32767) r = 32767;
    return (int16_t)r;
}

static void poll_joypad(const joybus_t *j, uint32_t port, chain_t *c) {
    joypad_t jp;
    jp.u  = (uint8_t)rp_kbd(j, port, RETRO_DEVICE_ID_JOYPAD_UP);
    jp.d  = (uint8_t)rp_kbd(j, port, RETRO_DEVICE_ID_JOYPAD_DOWN);
    jp.l  = (uint8_t)rp_kbd(j, port, RETRO_DEVICE_ID_JOYPAD_LEFT);
    jp.r  = (uint8_t)rp_kbd(j, port, RETRO_DEVICE_ID_JOYPAD_RIGHT);
    jp.lt = (uint8_t)rp_kbd(j, port, RETRO_DEVICE_ID_JOYPAD_L);
    jp.rt = (uint8_t)rp_kbd(j, port, RETRO_DEVICE_ID_JOYPAD_R);
    jp.x  = (uint8_t)rp_kbd(j, port, RETRO_DEVICE_ID_JOYPAD_SELECT);
    jp.p  = (uint8_t)(rp_kbd(j, port, RETRO_DEVICE_ID_JOYPAD_START) | rp_kbd(j, port, RETRO_DEVICE_ID_JOYPAD_X));
    jp.a  = (uint8_t)rp_kbd(j, port, RETRO_DEVICE_ID_JOYPAD_Y);
    jp.b  = (uint8_t)rp_kbd(j, port, RETRO_DEVICE_ID_JOYPAD_B);
    jp.c  = (uint8_t)rp_kbd(j, port, RETRO_DEVICE_ID_JOYPAD_A);
    /* Direcional: cima+baixo ou esq+dir simultaneos nao existem num D-pad 3DO; alguns jogos travam com isso */
    if (jp.u && jp.d) jp.u = jp.d = 0;
    if (jp.l && jp.r) jp.l = jp.r = 0;
    add_joypad(c, &jp);
}

static void poll_flightstick(const joybus_t *j, uint32_t port, chain_t *c) {
    flightstick_t fs;
    fs.h_pos = an(j, port, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X);
    fs.v_pos = an(j, port, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y);
    fs.z_pos = an(j, port, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
    fs.fire = (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_R2);
    fs.a    = (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_Y);
    fs.b    = (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_B);
    fs.c    = (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_A);
    fs.u    = (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_UP);
    fs.d    = (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_DOWN);
    fs.l    = (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_LEFT);
    fs.r    = (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_RIGHT);
    fs.p    = (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_START);
    fs.x    = (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_SELECT);
    fs.lt   = (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_L);
    fs.rt   = (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_R);
    add_flightstick(c, &fs);
}

static void poll_mouse(const joybus_t *j, uint32_t port, chain_t *c) {
    mouse_t m;
    m.x      = (int16_t)st(j, port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
    m.y      = (int16_t)st(j, port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
    m.left   = (uint8_t)ms(j, port, RETRO_DEVICE_ID_MOUSE_LEFT);
    m.middle = (uint8_t)ms(j, port, RETRO_DEVICE_ID_MOUSE_MIDDLE);
    m.right  = (uint8_t)ms(j, port, RETRO_DEVICE_ID_MOUSE_RIGHT);
    m.shift  = (uint8_t)ms(j, port, RETRO_DEVICE_ID_MOUSE_BUTTON_4);
    add_mouse(c, &m);
}

static void poll_lightgun(joybus_t *j, uint32_t port, chain_t *c) {
    lightgun_t g;
    int32_t rx = st(j, port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
    int32_t ry = st(j, port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);
    g.x       = view_map(rx, j->view_l, j->view_r);
    g.y       = view_map(ry, j->view_t, j->view_b);
    g.trigger = (uint8_t)lg(j, port, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER);
    g.option  = (uint8_t)lg(j, port, RETRO_DEVICE_ID_LIGHTGUN_SELECT);
    g.reload  = (uint8_t)lg(j, port, RETRO_DEVICE_ID_LIGHTGUN_RELOAD);
    if (lg(j, port, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN)) {      /* mirando fora da tela: gatilho ou reload = "recarregar" */
        g.trigger = 0;
        g.reload  = (uint8_t)(lg(j, port, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER) || lg(j, port, RETRO_DEVICE_ID_LIGHTGUN_RELOAD));
    }
    j->xh_on[port] = 1; j->xh_x[port] = rx; j->xh_y[port] = ry;
    add_lightgun(c, &g);
}

static void poll_arcade_lightgun(joybus_t *j, uint32_t port, chain_t *c) {
    arcade_lightgun_t g;
    int32_t rx = st(j, port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
    int32_t ry = st(j, port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);
    g.x       = view_map(rx, j->view_l, j->view_r);
    g.y       = view_map(ry, j->view_t, j->view_b);
    g.trigger = (uint8_t)lg(j, port, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER);
    g.service = (uint8_t)lg(j, port, RETRO_DEVICE_ID_LIGHTGUN_AUX_A);
    g.coins   = (uint8_t)lg(j, port, RETRO_DEVICE_ID_LIGHTGUN_SELECT);
    g.start   = (uint8_t)lg(j, port, RETRO_DEVICE_ID_LIGHTGUN_START);
    g.holster = (uint8_t)lg(j, port, RETRO_DEVICE_ID_LIGHTGUN_RELOAD);
    if (lg(j, port, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN)) {
        g.trigger = 0;
        g.holster = (uint8_t)(lg(j, port, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER) || lg(j, port, RETRO_DEVICE_ID_LIGHTGUN_RELOAD));
    }
    j->xh_on[port] = 1; j->xh_x[port] = rx; j->xh_y[port] = ry;
    add_arcade_lightgun(c, &g);
}

static void poll_orbatak_trackball(const joybus_t *j, uint32_t port, chain_t *c) {
    orbatak_trackball_t tb;
    tb.x = (int16_t)(an(j, port, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) / (32768 / 24));
    tb.y = (int16_t)(an(j, port, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) / (32768 / 24));
    if (tb.x == 0) {
        if (rp(j, port, RETRO_DEVICE_ID_JOYPAD_LEFT)) tb.x = -24;
        else if (rp(j, port, RETRO_DEVICE_ID_JOYPAD_RIGHT)) tb.x = 24;
    }
    if (tb.y == 0) {
        if (rp(j, port, RETRO_DEVICE_ID_JOYPAD_UP)) tb.y = -24;
        else if (rp(j, port, RETRO_DEVICE_ID_JOYPAD_DOWN)) tb.y = 24;
    }
    add_orbatak_trackball(c, &tb);
}

static void poll_orbatak_buttons(const joybus_t *j, uint32_t port, orbatak_buttons_t *b) {
    b->start_p1 |= (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_SELECT);
    b->start_p2 |= (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_START);
    b->coin_p1  |= (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_L);
    b->coin_p2  |= (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_R);
    b->service  |= (uint8_t)rp(j, port, RETRO_DEVICE_ID_JOYPAD_R2);
}

static void rebuild_chain(joybus_t *j) {
    chain_t c; orbatak_buttons_t ob; int has_orbatak = 0;
    memset(c.buf, PBUS_END_CHAIN_ID, sizeof c.buf); c.idx = 0;
    memset(&ob, 0, sizeof ob);
    memset(j->xh_on, 0, sizeof j->xh_on);
    for (uint32_t p = 0; p < JOYBUS_PORTS; p++) {
        if (!j->connected[p]) continue;
        switch (j->device[p]) {
        case JOYBUS_DEV_NONE:            break;
        case JOYBUS_DEV_FLIGHTSTICK:     poll_flightstick(j, p, &c); break;
        case JOYBUS_DEV_MOUSE:           poll_mouse(j, p, &c); break;
        case JOYBUS_DEV_LIGHTGUN:        poll_lightgun(j, p, &c); break;
        case JOYBUS_DEV_ARCADE_LIGHTGUN: poll_arcade_lightgun(j, p, &c); break;
        case JOYBUS_DEV_ORBATAK:         poll_orbatak_trackball(j, p, &c); poll_orbatak_buttons(j, p, &ob); has_orbatak = 1; break;
        default:                         poll_joypad(j, p, &c); break;
        }
    }
    if (has_orbatak) add_orbatak_buttons(&c, &ob);    /* um unico bloco de botoes Orbatak, no fim da cadeia */
    memcpy(j->buf, c.buf, sizeof j->buf);
}

/* ------------------------------------------------------------------ API */
void joybus_set_callbacks(void *jp, retro_input_poll_t poll_cb, retro_input_state_t state_cb) {
    joybus_t *j = (joybus_t *)jp;
    j->input_poll_cb = poll_cb;
    j->input_state_cb = state_cb;
}

void joybus_poll(void *jp) { rebuild_chain((joybus_t *)jp); }

void joybus_get_pbus_buffer(void *jp, uint32_t port, uint32_t *out) {
    joybus_t *j = (joybus_t *)jp; (void)port;
    for (int w = 0; w < JOYBUS_BUFFER_WORDS; w++)   /* big-endian: RAM do 3DO guarda o valor logico */
        out[w] = ((uint32_t)j->buf[w*4] << 24) | ((uint32_t)j->buf[w*4+1] << 16) |
                 ((uint32_t)j->buf[w*4+2] << 8) | (uint32_t)j->buf[w*4+3];
}

void joybus_set_connected(void *jp, uint32_t port, uint8_t connected) {
    joybus_t *j = (joybus_t *)jp;
    if (port < JOYBUS_PORTS) j->connected[port] = connected ? 1 : 0;
}
uint8_t joybus_is_connected(void *jp, uint32_t port) {
    joybus_t *j = (joybus_t *)jp;
    return port < JOYBUS_PORTS ? j->connected[port] : 0;
}

uint32_t joybus_normalize_device(uint32_t device) {
    switch (device) {
    case JOYBUS_DEV_NONE: case JOYBUS_DEV_JOYPAD: case JOYBUS_DEV_FLIGHTSTICK: case JOYBUS_DEV_MOUSE:
    case JOYBUS_DEV_LIGHTGUN: case JOYBUS_DEV_ARCADE_LIGHTGUN: case JOYBUS_DEV_ORBATAK:
        return device;
    }
    return JOYBUS_DEV_JOYPAD;     /* desconhecido = joypad (como o Opera) */
}
void joybus_set_device(void *jp, uint32_t port, uint32_t device) {
    joybus_t *j = (joybus_t *)jp;
    if (port >= JOYBUS_PORTS) return;
    j->device[port] = joybus_normalize_device(device);
    j->xh_on[port] = 0;
}
uint32_t joybus_get_device(void *jp, uint32_t port) {
    joybus_t *j = (joybus_t *)jp;
    return port < JOYBUS_PORTS ? j->device[port] : JOYBUS_DEV_NONE;
}

void joybus_set_view(void *jp, int32_t l, int32_t t, int32_t r, int32_t b) {
    joybus_t *j = (joybus_t *)jp;
    if (r <= l || b <= t) { l = -32768; t = -32768; r = 32768; b = 32768; }
    j->view_l = l; j->view_t = t; j->view_r = r; j->view_b = b;
}

int joybus_crosshair(const void *jp, uint32_t port, int32_t *x, int32_t *y) {
    const joybus_t *j = (const joybus_t *)jp;
    if (port >= JOYBUS_PORTS || !j->xh_on[port]) return 0;
    *x = j->xh_x[port]; *y = j->xh_y[port];
    return 1;
}

void joybus_init(joybus_t *j) {
    memset(j, 0, sizeof *j);
    memset(j->buf, PBUS_END_CHAIN_ID, sizeof j->buf);
    for (int p = 0; p < JOYBUS_PORTS; p++) j->device[p] = JOYBUS_DEV_JOYPAD;
    j->connected[0] = 1;   /* jogador 1; os demais sao ligados por retro_set_controller_port_device / Active Input Devices */
    joybus_set_view(j, 0, 0, 0, 0);
}
void joybus_free(joybus_t *j) { (void)j; }
void joybus_reset(joybus_t *j) {
    memset(j->buf, PBUS_END_CHAIN_ID, sizeof j->buf);
    memset(j->xh_on, 0, sizeof j->xh_on);
}
joybus_t *joybus_get_global(void) { return &g_joybus; }
