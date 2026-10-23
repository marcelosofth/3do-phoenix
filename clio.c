/* phoenix_core/clio.c — ver clio.h */
#include "clio.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define RR(o) (c->R[(o) >> 2])

void (*clio_trace_hook)(const char *msg) = NULL;
static int clio_trace_n;
static void clio_tr(const char *fmt, ...) {
    char b[160]; va_list ap;
    if (!clio_trace_hook || clio_trace_n >= 300) return;
    clio_trace_n++;
    va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    clio_trace_hook(b);
}

void clio_reset(clio_t *c) {
    memset(c->R, 0, sizeof c->R);
    c->audio_acc = c->timer_acc = c->line_acc = 0;   /* slots +0x18/+0x30 da vtable: FUN_0048f950/48ef50 */
    c->xbus_sel = 0;
    c->sem_data = c->sem_status = 0;
    /* FUN_0048f950 (reset do CLIO no .exe, 0x48f984): R[0x400] = 0x80 => gate do XBUS ABERTO ao sair do reset.
     * Sem isso a BIOS escreve em 0x500 com o gate fechado e fica em 0x21da0 esperando o bit 0x80 para sempre. */
    c->R[0x400 >> 2] = 0x80;
    /* Inicializa registradores VINT0 (0x08) e VINT1 (0x0C) com linhas de interrupção vertical.
     * NTSC: 263 linhas, VINT0=240, VINT1=262
     * PAL:  313 linhas, VINT0=288, VINT1=312
     * A BIOS configura estes valores, mas inicializamos com defaults NTSC para segurança. */
    c->R[0x08 >> 2] = 240;  /* VINT0 linha NTSC */
    c->R[0x0C >> 2] = 262;  /* VINT1 linha NTSC */
}

/* FUN_0048ef80 */
void clio_raise_irq(clio_t *c, uint32_t irq0, uint32_t irq1) {
    if (irq0 & 0x800u) c->stat_irq11++;
    RR(0x60) |= irq1;
    RR(0x40) |= irq0;
    if (RR(0x60) != 0) RR(0x40) |= 0x80000000u;
}

/* ---- DIPIR: aviso de acesso a midia do XBUS (port de opera_clio_xbus_media_access) ----
 * O Opera grava o dispositivo no primeiro latch livre (0x410, depois 0x414) com o bit CURRENT (0x8000)
 * e gera INT1 bit 1 (DIPIR) so no primeiro. E isso que faz a BIOS rodar a identificacao do disco. */
#define XBUS_DIPIR1         0x410
#define XBUS_DIPIR2         0x414
#define XBUS_DIPIR_DEV_MASK 0x00ffu
#define XBUS_DIPIR_CURRENT  0x8000u
#define CLIO_INT1_DIPIR     0x00000002u

void clio_xbus_media_access(clio_t *c, uint32_t unit) {
    uint32_t latch = XBUS_DIPIR_CURRENT | (unit & XBUS_DIPIR_DEV_MASK);
    uint32_t d1 = RR(XBUS_DIPIR1), d2 = RR(XBUS_DIPIR2);
    if (((d1 & XBUS_DIPIR_CURRENT) && (d1 & XBUS_DIPIR_DEV_MASK) == (latch & XBUS_DIPIR_DEV_MASK)) ||
        ((d2 & XBUS_DIPIR_CURRENT) && (d2 & XBUS_DIPIR_DEV_MASK) == (latch & XBUS_DIPIR_DEV_MASK)))
        return;
    if (!(d1 & XBUS_DIPIR_CURRENT))      { RR(XBUS_DIPIR1) = latch; clio_raise_irq(c, 0, CLIO_INT1_DIPIR); }
    else if (!(d2 & XBUS_DIPIR_CURRENT))   RR(XBUS_DIPIR2) = latch;
}

/* chamado antes de cada escrita de comando/poll no dispositivo selecionado (WR_COM/WR_POL) */
static void xbus_latch_media(clio_t *c, void *dev) {
    xdev_t *d = dev;
    if (d && d->media_access && d->media_access(d->ctx))
        clio_xbus_media_access(c, (uint32_t)((c->xbus_sel & 0x80) | (c->xbus_sel & 0x0f)));
}

/* ---- XBUS (FUN_00498500..00498690): sem dispositivo, lê 0 / poll 0x30 ---- */
static xdev_t *xsel(clio_t *c) { xdev_t *d = c->dev[c->xbus_sel & 0xf]; if (!d) c->stat_xbus_nodev++; return d; }
static uint32_t xbus_poll(clio_t *c) {
    xdev_t *d = xsel(c);
    uint32_t v = d && d->poll_read ? d->poll_read(d->ctx) : 0x30;
    return (c->xbus_sel & 0x80) ? (v & 0xf) : v;
}
static uint32_t xbus_data_read(clio_t *c) { xdev_t *d = xsel(c); return d && d->data_read ? d->data_read(d->ctx) : 0; }

/* ---- Peek: FUN_0048f170 ---- */
uint32_t clio_peek(clio_t *c, uint32_t addr) {
    uint32_t off = addr & 0xfffc, hi = off >> 12;
    c->rtouch_r[(off >> 2) >> 5] |= 1u << ((off >> 2) & 31);
    if (hi == 1) {
        if (off >= 0x17d0) switch (off) {
        case 0x17d0: return ((uint32_t)c->sem_status << 16) | c->sem_data;
        case 0x17f0: c->dsp_rand = (c->dsp_rand * 0x41c64e6du + 0x3039u) & 0x7fffffffu; return c->dsp_rand >> 16;
        case 0x17f4: return c->dsp_in_a;
        case 0x17f8: return c->dsp_in_b;
        case 0x17fc: return (uint32_t)(c->dsp_halt == 0) | c->dsp_run;
        }
    } else if (hi == 0) {
        switch (off >> 8) {
        case 0:
            switch (off) {
            case 0x00: return 0x02022000u;
            case 0x30: {
                uint32_t L = c->pal ? 0x15d7 : 0x15a8, H = c->pal ? 0x620 : 0x613;
                return ((c->line_acc % L) * H) / L;
            }
            case 0x40: case 0x44: return RR(0x40);
            case 0x48: case 0x4c: return RR(0x48) | 0x80000000u;
            case 0x50: case 0x54: return RR(0x50);
            case 0x60: case 0x64: return RR(0x60);
            case 0x68: case 0x6c: return RR(0x68);
            } break;
        case 1: if (off < 0x180) return RR(off) & 0xffff; break;
        case 2: if (off == 0x200 || off == 0x204) return RR(0x200);
                if (off == 0x208 || off == 0x20c) return RR(0x208);
                break;
        case 3: if (off < 0x380 && (off == 0x304 || off == 0x308)) return RR(0x304); break;
        case 4: if (off == 0x404 || off == 0x400) return RR(0x400);
                if (off == 0x414) return (RR(0x414) & XBUS_DIPIR_CURRENT) ? RR(0x414) : 0x4000;
                break;
        case 5:
            if (RR(0x400) & 0x80) {
                uint32_t g = off >> 4;
                if (g - 0x54 > 0xb) { xdev_t *d = xsel(c); return d && d->res_read ? d->res_read(d->ctx) : 0; }
                if (g >= 0x58 && g <= 0x5b) { xdev_t *d = xsel(c); return d && d->status_read ? d->status_read(d->ctx) : 0; }
                if (g >= 0x5c)              return xbus_data_read(c);
                return xbus_poll(c);
            } break;
        }
    } else if (hi == 3 && (off >> 8) > 0x37) {
        uint32_t k = off >> 8;
        if (k < 0x3c) { uint32_t i = (off >> 1) & 0xfe;
            uint32_t a = c->dsp_read16 ? c->dsp_read16(c->ctx, i + 0x300) : 0;     /* TODO(RE): DSP */
            uint16_t b = c->dsp_read16 ? c->dsp_read16(c->ctx, i + 0x301) : 0;
            return (a << 16) | b; }
        if (k < 0x40) return c->dsp_read16 ? c->dsp_read16(c->ctx, ((off - 0x3c00) >> 2) + 0x300) : 0;
    }
    return RR(off);
}

/* ---- Poke: FUN_0048f9f0 ---- */
static void dsp_w(clio_t *c, uint32_t i, uint16_t v) { if (c->dsp_write16) c->dsp_write16(c->ctx, i, v); }
static void dsp_nw(clio_t *c, uint32_t i, uint16_t v) { if (c->dsp_nwrite16) c->dsp_nwrite16(c->ctx, i, v); }

void clio_poke(clio_t *c, uint32_t addr, uint32_t v) {
    uint32_t off = addr & 0xfffc, hi = off >> 12;
    c->rtouch_w[(off >> 2) >> 5] |= 1u << ((off >> 2) & 31);
    if (hi == 1) {
        if (off < 0x17d0) goto raw;
        switch (off) {
        case 0x17d0: c->sem_data = (uint16_t)v; c->sem_status = 8; return;
        case 0x17d4: case 0x17d8: case 0x17dc: case 0x17e0: case 0x17ec:
        case 0x17f0: case 0x17f4: case 0x17f8: goto raw;
        case 0x17e4: if (c->dsp_reset) c->dsp_reset(c->ctx, 1); return;
        case 0x17e8: if (c->dsp_reset) c->dsp_reset(c->ctx, 0); return;
        case 0x17fc: c->dsp_run = v & 1; return;
        default: {   /* 0x1800..0x1fff: N-mem, 2 palavras de 16 bits por palavra ARM */
            uint32_t i = ((off & 0xfbfc) - 0x1800) >> 1;
            dsp_nw(c, i & 0x3fe, (uint16_t)(v >> 16));
            dsp_nw(c, (i + 1) & 0x3ff, (uint16_t)v);
            return; }
        }
    }
    if (hi == 2) { dsp_nw(c, (off & 0x7ff) >> 2, (uint16_t)v); return; }     /* N-mem 16 bits (o .exe tambem invalida o cache do otimizador: sem efeito aqui) */
    if (hi == 3) {
        uint32_t k = off >> 8;
        if (k >= 0x30) {
            if (k < 0x34) { uint32_t i = (off >> 1) & 0xfe; dsp_w(c, i, (uint16_t)(v >> 16)); dsp_w(c, i + 1, (uint16_t)v); return; }
            if (k < 0x38) { dsp_w(c, (off - 0x3400) >> 2, (uint16_t)v); return; }
        }
        goto raw;
    }
    switch (off >> 8) {           /* hi == 0 */
    case 0:
        switch (off) {
        case 0x30: {
            uint32_t L = c->pal ? 0x15d7 : 0x15a8, H = c->pal ? 0x620 : 0x613;
            uint32_t m = v < H + 1 ? v : H;
            uint32_t t = (c->line_acc / L) * H + m;
            c->line_acc = (L * t) / H;
            return; }
        case 0x28: {        /* CSTAT: logica do opera_clio.c (linhas 671-686) */
            uint32_t old = RR(0x28), cs = v & ~(0x10u | 0x20u | 0x43u | 0x0Cu);
            cs |= old & 0x0Cu;                    /* bits de watchdog: preservados */
            cs |= old & v & 0x43u;                /* PON/ROLLOVER/DIPIR_RESET: so limpam escrevendo 0 */
            if ((v & 0x30u) == 0x30u) cs |= 0x40u;     /* reset + clear DIPIR => liga DIPIR_RESET (0x40) */
            RR(0x28) = cs;
            if (v & 0x10u) c->reset_req = v & 0x30u;
            return; }
        case 0x3c: return;
        case 0x40: RR(0x40) |= v; if (RR(0x60)) RR(0x40) |= 0x80000000u; return;
        case 0x44: RR(0x40) &= ~v; if (!RR(0x60)) RR(0x40) &= 0x7fffffffu; return;
        case 0x48: RR(0x48) |= v; return;
        case 0x4c: RR(0x48) = (~v & RR(0x48)) | 0x80000000u; return;
        case 0x50: RR(0x50) |= v; return;
        case 0x54: RR(0x50) &= ~v; return;
        case 0x60: RR(0x60) |= v; if (RR(0x60)) RR(0x40) |= 0x80000000u; return;
        case 0x64: RR(0x60) &= ~v; if (!RR(0x60)) RR(0x40) &= 0x7fffffffu; return;
        case 0x68: RR(0x68) |= v; return;
        case 0x6c: RR(0x68) &= ~v; return;
        case 0x84: RR(0x84) = v & 0xf; return;
        } break;
    case 1: if (off < 0x180) { RR(off) = v & 0xffff; return; } break;
    case 2:
        switch (off) {
        case 0x200: RR(0x200) |= v; return;   case 0x204: RR(0x200) &= ~v; return;
        case 0x208: RR(0x208) |= v; return;   case 0x20c: RR(0x208) &= ~v; return;
        case 0x220: RR(0x220) = v & 0x3ff; return;   /* o Phoenix tambem notifica vtable+0x78 (observador de UI) */
        } break;
    case 3:
        if (off < 0x380) {
            if (off == 0x304) { RR(0x304) |= v; return; }          /* so arma; o DMA roda em clio_tick */
            if (off == 0x308) { RR(0x304) &= ~v; return; }
            if (off == 0x300) {
                RR(0x304) &= ~v;
                uint32_t m = v;      /* os 4 registradores de cada canal sao do MADAM (FUN_008ff560 = madam_poke) */
                for (int i = 0; i < 13; i++) if (m & (1u << i)) {
                    for (int k = 0; k < 16; k += 4) if (c->madam_poke) c->madam_poke(c->ctx, 0x400 + i * 16 + k, 0);
                    m &= ~(1u << i); }
                for (int i = 0; i < 4; i++) if (m & (1u << (i + 16))) {
                    for (int k = 0; k < 16; k += 4) if (c->madam_poke) c->madam_poke(c->ctx, 0x500 + i * 16 + k, 0);
                    m &= ~(1u << (i + 16)); }
                return;
            }
        } break;
    case 4:
        if (off == 0x400) { RR(0x400) = v & 0xffffbf7fu; if (!(v & 0x800)) RR(0x400) |= 0x80; return; }
        if (off == XBUS_DIPIR1 || off == XBUS_DIPIR2) return;      /* so leitura: quem escreve e o acesso a midia */
        break;
    case 5:
        if (RR(0x400) & 0x80) {
            uint32_t g = off >> 4; xdev_t *d = xsel(c);
            if (g - 0x54 > 0xb) { c->xbus_sel = (uint8_t)v; c->xbus_seen |= 1u << (v & 15); return; }
            if (g >= 0x58 && g <= 0x5b) { xbus_latch_media(c, d); if (d && d->cmd_write) d->cmd_write(d->ctx, (uint8_t)v); return; }
            if (g >= 0x5c)              { if (d && d->data_write) d->data_write(d->ctx, (uint8_t)v); return; }
            xbus_latch_media(c, d);
            if (d && d->poll_write) d->poll_write(d->ctx, (uint8_t)v);
            return;
        }
        if (((off >> 4) - 0x50) < 4) { RR(0x500) = v; RR(0x400) |= 0x40; }    /* gate fechado: guarda selecao */
        break;
    }
    /* VCNT (0x34) e registrador somente-leitura (contador de linha de video) */
    if (off == 0x34) return;
raw:
    RR(off) = v;
}

/* ---- FUN_0048f540: 16 timers ---- */
static void timers_tick(clio_t *c) {
    int first = 1;
    for (int i = 0; i < 16; i++) {
        uint32_t *ctl = &RR(i < 8 ? 0x200 : 0x208);
        unsigned sh = (i * 4) & 0x1c;
        uint32_t nib = *ctl >> sh;
        if ((((nib & 4) == 0) || first) && (first = 0, (nib & 1) != 0)) {
            uint32_t cnt = RR(0x100 + 8 * i), nxt = cnt - 1;
            int under = ((int16_t)cnt == 0);
            first = under;
            if (under) {
                if (i & 1) clio_raise_irq(c, 1u << (10 - (i >> 1)), 0);
                if ((nib & 2) == 0) *ctl &= ~(1u << sh);
                else nxt = RR(0x104 + 8 * i) & 0xffff;
            }
            RR(0x100 + 8 * i) = nxt & 0xffff;
        }
    }
}

/* ---- FUN_0048f690: DMA XBUS (uma "rodada" por tick) ---- */
/* estado so pra depuracao: evita logar o mesmo "travado esperando DATA_RDY"
 * a cada tick (seriam milhares/seg); so loga na BORDA de entrada/saida do bloqueio. */
static int g_dma_blocked_logged;

static void xbus_dma_tick(clio_t *c) {
    if (!(RR(0x304) & 0x100000) || !(RR(0x400) & 0x800)) { g_dma_blocked_logged = 0; return; }
    if (!(xbus_poll(c) & 0x20)) {
        if (!g_dma_blocked_logged) {
            uint32_t addr = c->madam_peek ? c->madam_peek(c->ctx, 0x540) : 0;
            int32_t  len  = (int32_t)(c->madam_peek ? c->madam_peek(c->ctx, 0x544) : 0);
            clio_tr("dma armado sem DATA_RDY: addr=%08x len=%d poll=%02x R400=%08x",
                    addr, len, xbus_poll(c), RR(0x400));
            g_dma_blocked_logged = 1;
        }
        return;
    }
    g_dma_blocked_logged = 0;
    uint32_t addr = c->madam_peek ? c->madam_peek(c->ctx, 0x540) : 0;
    int32_t  len  = (int32_t)(c->madam_peek ? c->madam_peek(c->ctx, 0x544) : 0);
    uint32_t len_before = (uint32_t)len;
    RR(0x400) = (RR(0x400) & 0xffffff7fu) | 0x400;
    uint32_t a = addr; int32_t left = -4;
    if (RR(0x404) & 0x200) {
        if (len >= 0) {
            do {
                left = len;
                if (!(xbus_poll(c) & 0x20)) break;
                for (uint32_t k = 0; k < 4; k++) { uint8_t b = (uint8_t)xbus_data_read(c);
                    if (a + k < PH_RAM_BYTES && c->ram8) c->ram8[(a + k) ^ 3] = b; }
                a += 4; len -= 4; left = len;
            } while (len >= 0);
            if (a != addr && c->ram_written) c->ram_written(c->ctx, addr, a - addr);
            if (left < 0) left = -4;
        }
    }
    if (c->madam_poke) { c->madam_poke(c->ctx, 0x544, (uint32_t)left); c->madam_poke(c->ctx, 0x540, a); }
    int done = (left < 0);
    clio_tr("dma addr=%08x len=%u xfer=%u left=%d data_rdy=%d done=%d",
            addr, len_before, (unsigned)(a - addr), left, (xbus_poll(c) & 0x20) != 0, done);
    if (done) {
        if (RR(0x400) & 0x40) c->xbus_sel = (uint8_t)RR(0x500);
        RR(0x304) &= 0xffefffffu;
        RR(0x400) = (RR(0x400) & 0xfffff3bfu) | 0x80;
        clio_raise_irq(c, 0x20000000u, 0);
        clio_tr("dma IRQ29 (completo) disparado");
    }
}

/* ---- FUN_00490890: o relogio ---- */
void clio_tick(clio_t *c, uint32_t cyc) {
    if (c->cd_tick) c->cd_tick(c->ctx, cyc);
    xbus_dma_tick(c);

    c->audio_acc += cyc * 9;
    while (c->audio_acc > 0x9f6) {
        c->audio_acc -= 0x9f7;
        uint32_t s = c->dsp_step ? c->dsp_step(c->ctx) : 0;
        if (c->audio_out) c->audio_out(c->ctx, s);
        /* handshake do semaforo ARM<->DSP (0x17d0): o ARM grava sem_status=8 (bit3, "mensagem
         * pendente para o DSP") em clio_poke; o lado DSP nunca respondia, entao a BIOS ficava
         * presa num polling eterno em TST r12,#0x40000 (bit18 = bit2 de sem_status, valor 0x4)
         * esperando o reconhecimento do DSP, e acabava caindo na idle task do kernel pra sempre
         * (confirmado via trace: pc=0x00058df8 girando com v=0x00080000 ate o fim da execucao,
         * jogo testado: AI Shougi (Japan)). Aqui, a cada amostra de DSP processada, se havia uma
         * mensagem pendente do ARM, sinaliza o reconhecimento (bit 0x4) -- e o que falta pro
         * handshake fechar e a BIOS seguir em frente. */
        if (c->sem_status & 0x8u) { c->sem_status = 0x4u; c->stat_sem_ack++; }
    }

    c->timer_acc += cyc * 2;
    while (c->timer_acc >= RR(0x220) + 0x40u) { c->timer_acc -= RR(0x220) + 0x40u; timers_tick(c); }

    c->line_acc += cyc * 7;
    while (c->line_acc >= 0x15d8) {
        uint32_t v = RR(0x34);
        if (!c->pal) { c->line_acc -= 0x15a8; RR(0x34) = (v & 0x7ff) < 0x106 ? v + 1 : (~v & 0x800); }
        else         { c->line_acc -= 0x15d7; RR(0x34) = (v & 0x7ff) > 0x137 ? (~v & 0x800) : v + 1; }
        if (c->vdlp_line_pending && c->vdlp_line_pending(c->ctx) && c->vdlp_line) c->vdlp_line(c->ctx);
        uint32_t line = RR(0x34) & 0x7ff;
        if (line == 0x18 && c->frame_cb) c->frame_cb(c->ctx);
        RR(0x3c) = RR(0x3c) * 0x41c64e6du + 0x3039u;
        if (line == (RR(0x08) & 0x7ff)) clio_raise_irq(c, 1, 0);
        if (line == (RR(0x0c) & 0x7ff)) clio_raise_irq(c, 2, 0);
    }
}
