/* joybus.h - Joybus/PBUS do 3DO (controles). Formato baseado em opera_pbus.c / lr_input.c do opera-libretro.
 *
 * Dispositivos suportados (mesmos 6 do Opera): joypad, flightstick, mouse, lightgun, arcade lightgun e trackball Orbatak.
 * Ate 8 portas (JOYBUS_PORTS), cadeia PBUS de ate 256 bytes (JOYBUS_BUF_BYTES), como o Opera (LR_INPUT_MAX_DEVICES / PBUS_BUF_SIZE). */
#ifndef PH_JOYBUS_H
#define PH_JOYBUS_H
#include <stdint.h>
#include <libretro.h>

#define JOYBUS_PORTS        8
#define JOYBUS_BUF_BYTES    256
#define JOYBUS_BUFFER_WORDS (JOYBUS_BUF_BYTES / 4)   /* 64 palavras; o que a cadeia nao usa sai como 0xFFFFFFFF */

/* tipos de dispositivo por porta (os mesmos valores do Opera, para o menu de controles do RetroArch ficar igual) */
#define JOYBUS_DEV_NONE            RETRO_DEVICE_NONE
#define JOYBUS_DEV_JOYPAD          RETRO_DEVICE_JOYPAD
#define JOYBUS_DEV_FLIGHTSTICK     RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0)
#define JOYBUS_DEV_MOUSE           RETRO_DEVICE_MOUSE
#define JOYBUS_DEV_LIGHTGUN        RETRO_DEVICE_LIGHTGUN
#define JOYBUS_DEV_ARCADE_LIGHTGUN RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 0)
#define JOYBUS_DEV_ORBATAK         RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1)

typedef struct joybus {
    uint8_t  buf[JOYBUS_BUF_BYTES];      /* cadeia PBUS (bytes na ordem do fio; byte 0 = 1o dispositivo), completada com 0xFF */
    uint8_t  connected[JOYBUS_PORTS];    /* porta ligada (dispositivo != NONE e dentro de "Active Input Devices") */
    uint32_t device[JOYBUS_PORTS];       /* JOYBUS_DEV_* escolhido para a porta */
    /* mira desenhada na tela (lightgun / arcade lightgun): posicao bruta do frontend (-32768..32767), atualizada a cada joybus_poll */
    uint8_t  xh_on[JOYBUS_PORTS];
    int32_t  xh_x[JOYBUS_PORTS], xh_y[JOYBUS_PORTS];
    /* onde fica a imagem do 3DO dentro da tela inteira (bordas / fundo expandido), no mesmo espaco -32768..32768.
     * Padrao = tela inteira (sem ajuste). Usado so para a mira de lightgun. */
    int32_t  view_l, view_t, view_r, view_b;
    void *ctx;
    retro_input_poll_t input_poll_cb;
    retro_input_state_t input_state_cb;
} joybus_t;

void joybus_init(joybus_t *j);
void joybus_free(joybus_t *j);
void joybus_reset(joybus_t *j);

void joybus_set_callbacks(void *j, retro_input_poll_t poll_cb, retro_input_state_t state_cb);
void joybus_poll(void *j);   /* le o estado dos dispositivos (SEM chamar input_poll: o retro_run ja chama) e refaz a cadeia */

/* Copia a cadeia PBUS para 'out' como JOYBUS_BUFFER_WORDS palavras big-endian (byte 0 = bits 31..24), prontas para a RAM do 3DO.
 * O parametro 'port' e ignorado: a cadeia contem todos os dispositivos conectados, na ordem das portas (como o Opera). */
void joybus_get_pbus_buffer(void *j, uint32_t port, uint32_t *out);

void    joybus_set_connected(void *j, uint32_t port, uint8_t connected);
uint8_t joybus_is_connected(void *j, uint32_t port);

/* tipo do dispositivo da porta (qualquer valor desconhecido vira joypad, como no Opera) */
uint32_t joybus_normalize_device(uint32_t device);   /* NONE e os 6 tipos acima passam; o resto vira JOYBUS_DEV_JOYPAD */
void     joybus_set_device(void *j, uint32_t port, uint32_t device);
uint32_t joybus_get_device(void *j, uint32_t port);

/* janela da imagem do 3DO na tela (veja joybus_t.view_*); l >= r ou t >= b volta ao padrao (tela inteira) */
void joybus_set_view(void *j, int32_t l, int32_t t, int32_t r, int32_t b);

/* mira da porta: devolve 1 e a posicao bruta (-32768..32767) se a porta tem lightgun ativa neste quadro */
int joybus_crosshair(const void *j, uint32_t port, int32_t *x, int32_t *y);

joybus_t *joybus_get_global(void);

#endif
