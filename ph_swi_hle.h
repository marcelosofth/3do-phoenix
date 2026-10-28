/* ph_swi_hle.h - "OperaOS SWI HLE": emulacao de alto nivel das SWIs matematicas 0x5xxxx do Portfolio OS (vetores/matrizes 16.16).
 * Portado do decode_swi_hle() do opera-libretro (libopera/opera_arm.c + opera_swi_hle_0x5XXXX.h, ISC License,
 * (c) 2021 Antonio SJ Musumeci <trapexit@spawn.link>). Entra pelo gancho cpu.swi_hook: devolve 1 quando tratou a SWI
 * (a CPU nao entra no kernel) e 0 quando o SO deve rodar a SWI de verdade. SWIs tratadas (as mesmas do Opera):
 * 0x50000-02, 0x50005-09, 0x5000C, 0x5000E-12. Ponteiro fora da RAM / desalinhado => devolve 0 (o SO trata). */
#ifndef PH_SWI_HLE_H
#define PH_SWI_HLE_H
#include "machine.h"
int ph_swi_hle(ph_machine_t *m, uint32_t imm24);
#endif
