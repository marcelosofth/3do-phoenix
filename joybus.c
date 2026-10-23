/* joybus.c - Joybus/PBUS do 3DO (controles), baseado no opera_pbus.c / lr_input.c do opera-libretro.
 *
 * O 3DO nao tem registradores de controle mapeados na CPU: o Portfolio OS (ControlPort.c) programa a DMA8 da
 * PBUS (MADAM MCTL PLAYXEN) e le da RAM uma cadeia de dispositivos. Cada joypad ocupa 2 bytes:
 *
 *   byte 0: 1 0 0 D U R L A      (0x80 = ID do joypad | D<<4 | U<<3 | R<<2 | L<<1 | A<<0)
 *   byte 1: B C P X RT LT 0 0    (B<<7 | C<<6 | P<<5 | X<<4 | RT<<3 | LT<<2)
 *
 * A cadeia e terminada/enchida com 0xFF. Botao pressionado = bit em 1. Cada porta conectada acrescenta seu
 * joypad a cadeia, na ordem (porta 0 = jogador 1, porta 1 = jogador 2).
 *
 * Mapeamento RetroPad -> 3DO (igual ao do Opera):
 *   RetroPad Y -> A      RetroPad B -> B      RetroPad A -> C
 *   RetroPad Start ou X -> P (Play/Pause)      RetroPad Select -> X (Stop)
 *   RetroPad L -> LT     RetroPad R -> RT     D-pad -> D/U/L/R
 */
#include "joybus.h"
#include <string.h>

#define PBUS_JOYPAD_ID   0x80
#define PBUS_END_CHAIN   0xFF

#define SH_A   0
#define SH_L   1
#define SH_R   2
#define SH_U   3
#define SH_D   4
#define SH_LT  2
#define SH_RT  3
#define SH_X   4
#define SH_P   5
#define SH_C   6
#define SH_B   7

static joybus_t g_joybus = {0};

/* Fallback de teclado (jogador 1), independente do mapeamento do RetroArch. Teclas = padrao do RetroArch:
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
static int rp(joybus_t *j, uint32_t port, unsigned id) {
    if (!j->input_state_cb) return 0;
    if (j->input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, id) != 0) return 1;
    if (port == 0) { int k = kbd_key_for(id); if (k && j->input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, (unsigned)k)) return 1; }
    return 0;
}

/* Acrescenta os 2 bytes do joypad da porta 'port' em buf[*idx]. */
static void add_joypad(joybus_t *j, uint32_t port, uint8_t *buf, unsigned *idx) {
    unsigned u  = rp(j, port, RETRO_DEVICE_ID_JOYPAD_UP);
    unsigned d  = rp(j, port, RETRO_DEVICE_ID_JOYPAD_DOWN);
    unsigned l  = rp(j, port, RETRO_DEVICE_ID_JOYPAD_LEFT);
    unsigned r  = rp(j, port, RETRO_DEVICE_ID_JOYPAD_RIGHT);
    unsigned lt = rp(j, port, RETRO_DEVICE_ID_JOYPAD_L);
    unsigned rt = rp(j, port, RETRO_DEVICE_ID_JOYPAD_R);
    unsigned x  = rp(j, port, RETRO_DEVICE_ID_JOYPAD_SELECT);
    unsigned p  = rp(j, port, RETRO_DEVICE_ID_JOYPAD_START) | rp(j, port, RETRO_DEVICE_ID_JOYPAD_X);
    unsigned a  = rp(j, port, RETRO_DEVICE_ID_JOYPAD_Y);
    unsigned b  = rp(j, port, RETRO_DEVICE_ID_JOYPAD_B);
    unsigned c  = rp(j, port, RETRO_DEVICE_ID_JOYPAD_A);

    /* Direcional: cima+baixo ou esq+dir simultaneos nao existem num D-pad 3DO; alguns jogos travam com isso */
    if (u && d) u = d = 0;
    if (l && r) l = r = 0;

    buf[(*idx)++] = (uint8_t)(PBUS_JOYPAD_ID | (d << SH_D) | (u << SH_U) | (r << SH_R) | (l << SH_L) | (a << SH_A));
    buf[(*idx)++] = (uint8_t)((b << SH_B) | (c << SH_C) | (p << SH_P) | (x << SH_X) | (rt << SH_RT) | (lt << SH_LT));
}

static void rebuild_chain(joybus_t *j) {
    uint8_t tmp[16];
    unsigned idx = 0;
    memset(tmp, PBUS_END_CHAIN, sizeof tmp);
    for (uint32_t p = 0; p < JOYBUS_PORTS; p++)
        if (j->connected[p] && idx + 2 <= sizeof tmp) add_joypad(j, p, tmp, &idx);
    memcpy(j->buf, tmp, sizeof tmp);
}

void joybus_set_callbacks(void *jp, retro_input_poll_t poll_cb, retro_input_state_t state_cb) {
    joybus_t *j = (joybus_t *)jp;
    j->input_poll_cb = poll_cb;
    j->input_state_cb = state_cb;
}

void joybus_poll(void *jp) {
    joybus_t *j = (joybus_t *)jp;
    rebuild_chain(j);
}

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

void joybus_init(joybus_t *j) {
    memset(j, 0, sizeof *j);
    memset(j->buf, PBUS_END_CHAIN, sizeof j->buf);
    j->connected[0] = 1;   /* jogador 1 */
    j->connected[1] = 0;   /* jogador 2: ligado via retro_set_controller_port_device */
}
void joybus_free(joybus_t *j) { (void)j; }
void joybus_reset(joybus_t *j) {
    memset(j->buf, PBUS_END_CHAIN, sizeof j->buf);
    memset(j->state, 0, sizeof j->state);
    memset(j->bit_count, 0, sizeof j->bit_count);
    memset(j->cmd, 0, sizeof j->cmd);
}
joybus_t *joybus_get_global(void) { return &g_joybus; }
