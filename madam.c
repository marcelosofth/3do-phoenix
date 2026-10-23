/* madam.c - MADAM Peek/Poke do Phoenix 3DO (Parte 13). Cada bloco cita a funcao do .exe de onde saiu. */
#include "madam.h"
#include <string.h>

void madam_reset(madam_t *m) {
    void *ud = m->ud;
    void (*r0)(void *, int8_t) = m->reg0_write;
    void (*d5)(void *, uint32_t) = m->dma580_write;
    uint32_t (*ce)(void *, uint8_t) = m->cel_exec;
    void (*cs)(void *) = m->cel_sync;
    void (*sb)(void *, uint32_t) = m->scope_begin;
    void (*se)(void *, uint32_t) = m->scope_end;
    uint8_t en = m->cel_enabled, fl = m->cel_flag40;
    memset(m, 0, sizeof *m);
    m->ud = ud; m->reg0_write = r0; m->dma580_write = d5; m->cel_exec = ce; m->cel_sync = cs;
    m->scope_begin = sb; m->scope_end = se; m->cel_enabled = en; m->cel_flag40 = fl;
}

static void scope_b(madam_t *m) { if (m->scope_begin) m->scope_begin(m->ud, m->cel_arg); }
static void scope_e(madam_t *m) { if (m->scope_end)   m->scope_end(m->ud, m->cel_arg); }
static void do_sync(madam_t *m) { if (m->cel_sync) m->cel_sync(m->ud); }
static uint32_t do_exec(madam_t *m, uint8_t async) { return m->cel_exec ? m->cel_exec(m->ud, async) : 0; }

/* FUN_004894c0(cel, n) */
void madam_cel_set(madam_t *m, uint32_t n) {
    if (n == 0) do_sync(m);                                         /* FUN_00704850 */
    if (m->cel_flag40 == 0 && m->cel_enabled != 0 && m->cel_state != 0 && n == 0) {
        scope_b(m); do_exec(m, 0); scope_e(m);                      /* FUN_00488700(cel, 0) */
    }
    m->cel_state = n;
}
int madam_cel_busy(const madam_t *m) { return m->cel_state == 0x10; }

/* FUN_00489650(cel, off-0x100, v): comandos do CEL; o retorno de 0x100 e o stall do ARM */
static uint32_t cel_write(madam_t *m, uint32_t off, uint32_t v) {
    if (off >= 0xc0) return 0;
    uint32_t idx = off >> 2;
    if (idx >= 0x20) {                                              /* 0x180..0x1bc: palavra de 32 bits -> duas de 16 */
        m->cel_w[idx * 2 - 0x20] = (v >> 16) & 0xffff;
        m->cel_w[idx * 2 - 0x1f] = v & 0xffff;
        return 0;
    }
    switch (idx) {
    case 0:                                                         /* 0x100: dispara o CEL */
        if (m->cel_state != 0) return 0;
        m->cel_state = 0x10;
        scope_b(m);
        { uint8_t async = m->cel_enabled ? (uint8_t)(m->cel_flag40 ^ 1) : 0;
          uint32_t r = do_exec(m, async);
          scope_e(m);
          return r; }
    case 1:                                                         /* 0x104: para o CEL */
        do_sync(m);
        if (m->cel_enabled != 0 && m->cel_state != 0 && m->cel_flag40 == 0) { scope_b(m); do_exec(m, 0); scope_e(m); }
        m->cel_state = 0;
        return 0;
    case 2: if (m->cel_state == 0x30) m->cel_state = 0x10; return 0;   /* 0x108: continua */
    case 3: if (m->cel_state == 0x10) m->cel_state = 0x30; return 0;   /* 0x10c: pausa */
    case 0xc: {                                                     /* 0x130: grava e deriva dois tamanhos */
        uint32_t a, b, v2;
        m->cel_w[12] = v;
        a = 0x20;
        if (!(v & 1)) { a = 0x200; if (!(v & 2)) { a = 0x100; if (!(v & 4)) a = (v & 8) << 7; } }
        b = 0x40;
        if (!(v & 0x10)) { b = 0x80; if (!(v & 0x20)) b = (v * 8) & 0x400; }
        m->cel_w21c = (b + a) * 4;
        v2 = v >> 8;
        a = 0x20;
        if (!(v2 & 1)) { a = 0x200; if (!(v & 0x200)) { a = 0x100; if (!(v & 0x400)) a = (v2 & 8) << 7; } }
        b = 0x40;
        if (!(v & 0x1000)) { b = 0x80; if (!(v & 0x2000)) b = (v2 * 8) & 0x400; }
        m->cel_w220 = (b + a) * 4;
        return 0; }
    default: m->cel_w[idx] = v; return 0;
    }
}

/* FUN_0047c7c0 */
static uint32_t cel_read(const madam_t *m, uint32_t off) { return off < 0x100 ? m->cel_w[off >> 2] : 0; }

/* FUN_0048b2a0 */
uint32_t madam_peek(madam_t *m, uint32_t addr) {
    uint32_t off = addr & 0x7fc, hi = off >> 8;
    if (hi == 1) return cel_read(m, off - 0x100);
    if (hi == 0) {
        if (off == 0x28) return m->cel_state;
    } else if (hi - 4 < 2) {
        if (off - 0x5a0 < 0x20) return m->dma_arr[(off - 0x5a0) >> 2] & 0xfffffc;
        return m->regs[off >> 2] & 0xfffffc;
    }
    return m->regs[off >> 2];
}

/* matrix engine (poke 0x7fc): R(x) = registrador 0x600+x como int32; produtos em 64 bits, >>16 */
#define R(o) ((int64_t)(int32_t)m->regs[(o) >> 2])
static int32_t sh16(int64_t s) { return (int32_t)((uint64_t)s >> 16); }
static void latch_prev(madam_t *m) {
    m->regs[0x660 >> 2] = (uint32_t)m->mat_out[0]; m->regs[0x664 >> 2] = (uint32_t)m->mat_out[1];
    m->regs[0x668 >> 2] = (uint32_t)m->mat_out[2]; m->regs[0x66c >> 2] = (uint32_t)m->mat_out[3];
}
static void matrix_op(madam_t *m, uint32_t mode) {
    int64_t v0 = R(0x640), v1 = R(0x644), v2 = R(0x648), v3 = R(0x64c);
    m->regs[0x7fc >> 2] = 0;
    switch (mode) {
    case 0: latch_prev(m); break;
    case 1:                                                         /* 4x4 */
        latch_prev(m);
        m->mat_out[3] = sh16(v3 * R(0x63c) + v2 * R(0x638) + v1 * R(0x634) + v0 * R(0x630));
        m->mat_out[2] = sh16(R(0x62c) * v3 + R(0x620) * v0 + R(0x624) * v1 + R(0x628) * v2);
        m->mat_out[1] = sh16(R(0x61c) * v3 + R(0x610) * v0 + R(0x614) * v1 + R(0x618) * v2);
        m->mat_out[0] = sh16(R(0x60c) * v3 + R(0x600) * v0 + R(0x604) * v1 + R(0x608) * v2);
        break;
    case 2:                                                         /* 3x3: out3 fica como estava */
        latch_prev(m);
        m->mat_out[2] = sh16(v1 * R(0x624) + v0 * R(0x620) + v2 * R(0x628));
        m->mat_out[1] = sh16(R(0x618) * v2 + R(0x610) * v0 + R(0x614) * v1);
        m->mat_out[0] = sh16(R(0x608) * v2 + R(0x600) * v0 + R(0x604) * v1);
        break;
    case 3: {                                                       /* 3x3 + perspectiva: x,y * (N / z) */
        int64_t q; int64_t num;
        latch_prev(m);
        m->mat_out[2] = sh16(R(0x620) * v0 + R(0x624) * v1 + R(0x628) * v2);
        num = (int64_t)(((uint64_t)m->regs[0x680 >> 2] << 32) | m->regs[0x684 >> 2]);
        q = num;
        if (m->mat_out[2] != 0) {
            if (m->mat_out[2] == -1) q = (int64_t)(0 - (uint64_t)num);   /* INT64_MIN/-1 derruba o .exe (#DE); aqui da a volta */
            else q = num / (int64_t)m->mat_out[2];
        }
        m->mat_out[1] = (int32_t)((uint64_t)((int64_t)sh16(R(0x618) * v2 + R(0x610) * v0 + R(0x614) * v1) * q) >> 32);
        m->mat_out[0] = (int32_t)((uint64_t)((int64_t)sh16(R(0x600) * v0 + v1 * R(0x604) + v2 * R(0x608)) * q) >> 32);
        break; }
    default: break;                                                 /* modo > 3: so zera 0x7fc */
    }
}

/* FUN_0048adb0 */
uint32_t madam_poke(madam_t *m, uint32_t addr, uint32_t v) {
    uint32_t off = addr & 0x7fc, hi = off >> 8;
    if (hi == 0) {
        if (off == 4) { m->regs[1] = 0x29; return 0; }
        if (off == 8) { m->regs[2] = v; if ((v & 0x8000u) && m->mctl_write) m->mctl_write(m->ud, v); return 0; }
        if (off == 0) { if (m->reg0_write) m->reg0_write(m->ud, (int8_t)v); return 0; }
    } else if (hi == 1) {
        return cel_write(m, off - 0x100, v);
    } else if (hi == 4 || hi == 5) {
        v &= 0xfffffc;
        if (off - 0x400 < 0x140) {
            if ((off & 0xc) < 8) m->regs[(off + 8) >> 2] = 0;       /* gravar endereco/tamanho zera a palavra 8 bytes adiante */
        } else {
            if (off - 0x5a0 < 0x20) { m->dma_arr[(off - 0x5a0) >> 2] = v; return 0; }
            if (off == 0x580) { if (m->dma580_write) m->dma580_write(m->ud, v); return 0; }
        }
    } else if (hi == 6 || hi == 7) {
        if (off == 0x7fc) { matrix_op(m, v); return 0; }
    }
    m->regs[off >> 2] = v;
    return 0;
}
