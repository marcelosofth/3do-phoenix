/* joybus.h - Joybus/PBUS do 3DO (controles). Formato baseado em opera_pbus.c (opera-libretro). */
#ifndef PH_JOYBUS_H
#define PH_JOYBUS_H
#include <stdint.h>
#include <libretro.h>

#define JOYBUS_PORTS 2
#define JOYBUS_BUFFER_WORDS 4  /* 16 bytes = 4 palavras (2 joypads = 4 bytes + 0xFF de enchimento) */

typedef struct joybus {
    uint8_t  buf[16];            /* cadeia PBUS (bytes na ordem do fio; byte 0 = 1o dispositivo) */
    uint8_t  connected[JOYBUS_PORTS];
    uint32_t state[JOYBUS_PORTS];
    uint32_t bit_count[JOYBUS_PORTS];
    uint32_t cmd[JOYBUS_PORTS];
    void *ctx;
    retro_input_poll_t input_poll_cb;
    retro_input_state_t input_state_cb;
    uint32_t (*get_buttons)(void *ctx, uint32_t port);
} joybus_t;

void joybus_init(joybus_t *j);
void joybus_free(joybus_t *j);
void joybus_reset(joybus_t *j);

void joybus_set_callbacks(void *j, retro_input_poll_t poll_cb, retro_input_state_t state_cb);
void joybus_poll(void *j);   /* le o estado do RetroPad (SEM chamar input_poll: o retro_run ja chama) e refaz a cadeia */

/* Copia a cadeia PBUS para 'out' como 4 palavras big-endian (byte 0 = bits 31..24), prontas para a RAM do 3DO.
 * O parametro 'port' e ignorado: a cadeia contem todos os controles conectados, na ordem das portas (como o Opera). */
void joybus_get_pbus_buffer(void *j, uint32_t port, uint32_t *out);

void joybus_set_connected(void *j, uint32_t port, uint8_t connected);
uint8_t joybus_is_connected(void *j, uint32_t port);

joybus_t *joybus_get_global(void);

#endif
