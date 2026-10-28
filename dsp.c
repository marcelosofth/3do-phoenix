/* dsp.c - DSP dsp3DO do Phoenix. Portado do .exe (ver dsp.h). Codigo novo; nada do Opera. */
#include "dsp.h"
#include <string.h>

#define PC(d)  ((d)->clio->dsp_in_a)
#define INS(d) ((d)->clio->dsp_in_b)
/* semaforo: no .exe e a propria memoria do DSP (+0x11d6c/+0x11d6e = eo[0xec]/eo[0xed]); o CLIO (0x17d0) usa clio->sem_*.
 * Copiamos nas bordas do dsp_step (o ARM so mexe entre amostras). */
#define SEM_ST(d) ((d)->eo[0xec])
#define SEM_DT(d) ((d)->eo[0xed])
#define D6A(d) ((d)->eo[0xeb])      /* +0x11d6a: se != 0 o DSP nao dorme */
#define D72(d) ((d)->eo[0xef])      /* +0x11d72: relogio (ciclos por amostra) */
#define OUT_L(d) ((d)->eo[0xfe])    /* +0x11d90 */
#define OUT_R(d) ((d)->eo[0xff])    /* +0x11d92 */

#ifdef PH_DSP_STATS
uint32_t dsp_stats[16];
#define ST(i) (dsp_stats[i]++)
#else
#define ST(i) ((void)0)
#endif

/* tabela de condicoes de desvio (.rdata 0xaf89e0): indice = flags&0x1f, bit = (instr>>10)&0x1f */
static const uint32_t cond_tab[32] = {
    0xacee1000,0x6cee1000,0xa64e3000,0x664e2100,0xa3e41200,0x63e41200,0xa3443200,0x63442300,
    0xa3e21400,0x63e21400,0xa3423400,0x63422500,0xace01e00,0x6ce01e00,0xa6403e00,0x66402f00,
    0x9c2e5000,0x5c2e5000,0xa60ef000,0x660ee100,0x93245200,0x53245200,0xa304f200,0x6304e300,
    0x93225400,0x53225400,0xa302f400,0x6302e500,0x9c205e00,0x5c205e00,0xa600fe00,0x6600ef00 };

/* ---- acesso ao MADAM cru (registradores de canal: sem a mascara 0xfffffc nem o "zera o proximo") ---- */
static uint32_t mpeek(dsp_t *d, uint32_t off) { clio_t *c = d->clio; return c->madam_raw_peek ? c->madam_raw_peek(c->ctx, off) : 0; }
static void     mpoke(dsp_t *d, uint32_t off, uint32_t v) { clio_t *c = d->clio; if (c->madam_raw_poke) c->madam_raw_poke(c->ctx, off, v); }
#define R304(d) ((d)->clio->R[0x304 >> 2])

/* ---- FIFOs de DMA do audio ---- */
/* FUN_0048f020: estado do FIFO de entrada ch (EI 0xd0..0xdc) */
static uint16_t in_stat(dsp_t *d, unsigned ch) {
    return (mpeek(d, (ch + 0x40) * 0x10) != 0 && (R304(d) & (1u << (ch & 31)))) ? 2 : 0;
}
/* FUN_0048f0b0: estado do FIFO de saida ch (EI 0xe0..0xe3) */
static uint16_t out_stat(dsp_t *d, unsigned ch) {
    return (mpeek(d, (ch + 0x50) * 0x10) != 0 && (R304(d) & (1u << ((ch + 16) & 31)))) ? 1 : 0;
}
static void dma_reload(dsp_t *d, uint32_t base, uint32_t irqbit, uint32_t enbit) {
    uint32_t na = mpeek(d, base + 8);
    mpoke(d, base, na);
    mpoke(d, base + 4, mpeek(d, base + 0xc));
    clio_raise_irq(d->clio, irqbit, 0);
    if (na == 0) R304(d) &= ~enbit;             /* flag de multithread (+0x86680) = 0 no Phoenix */
}
/* FUN_00490360: pega a proxima amostra de 16 bits do canal de entrada ch */
static uint16_t in_pop(dsp_t *d, unsigned ch) {
    clio_t *c = d->clio;
    uint32_t base = (ch + 0x40) * 0x10, addr = mpeek(d, base), enbit = 1u << (ch & 31);
    uint16_t s = 0;
    if (addr == 0 || !(R304(d) & enbit)) return 0;
    int32_t len = (int32_t)mpeek(d, base + 4);
    if (len >= 0) {
        ST(8); if (addr < (c->ram_bytes ? c->ram_bytes : PH_RAM_BYTES)) s = (uint16_t)((c->ram8[addr ^ 3] << 8) | c->ram8[(addr + 1) ^ 3]);
        mpoke(d, base, addr + 2);
        int32_t nl = (len & 2) == 0 ? len + 2 : len - 6;     /* bit1 = metade da palavra ja consumida */
        mpoke(d, base + 4, (uint32_t)nl);
        if (nl >= 0) return s;
    }
    ST(7); dma_reload(d, base, 1u << ((ch + 16) & 31), enbit);
    return s;
}
/* FUN_004910c0: grava uma amostra de 16 bits no canal de saida ch */
static void out_push(dsp_t *d, unsigned ch, uint16_t v) {
    clio_t *c = d->clio;
    uint32_t base = (ch + 0x50) * 0x10, addr = mpeek(d, base), enbit = 1u << ((ch + 16) & 31);
    if (addr == 0 || !(R304(d) & enbit)) return;
    int32_t len = (int32_t)mpeek(d, base + 4);
    if (len >= 0) {
        if (addr < (c->ram_bytes ? c->ram_bytes : PH_RAM_BYTES)) {
            c->ram8[addr ^ 3] = (uint8_t)(v >> 8); c->ram8[(addr + 1) ^ 3] = (uint8_t)v;
            if (c->ram_written) c->ram_written(c->ctx, addr, 2);
        }
        mpoke(d, base, addr + 2);
        int32_t nl = (len & 2) == 0 ? len + 2 : len - 6;
        mpoke(d, base + 4, (uint32_t)nl);
        if (nl >= 0) return;
    }
    dma_reload(d, base, 1u << ((ch + 12) & 31), enbit);
}

/* ---- leitura / escrita de operandos ---- */
/* FUN_00428160: endereco a partir do campo de 4 bits e do RBASE/modo */
static uint16_t addr_of(dsp_t *d, unsigned p) {
    unsigned p3 = p & 8, m = d->rmode;
    unsigned r = ((p & 7) ^ d->rbase) | (p3 << 6);
    if (m == 5) return (uint16_t)(r | ((~p & 8) << 5));
    if (m == 4) return (uint16_t)((p3 << 5) | r);
    if (m == 6) return (uint16_t)((((p * 2) & p3) << 5) | r);
    if (m == 7) return (uint16_t)((((((p & 0xffff) * 2) | (p & 0xffff)) & 8) << 5) | r);
    return (uint16_t)(((p & 4) << 6) | r);
}
/* FUN_004281f0 */
static uint16_t rd(dsp_t *d, unsigned a) {
    a &= 0xffff;
    if (a >= 0xd0 && a <= 0xdc) return in_stat(d, a & 0xf);
    if (a == 0xdd || a == 0xde) return 0;
    if (a >= 0xe0 && a <= 0xe3) return out_stat(d, a & 0xf);
    switch (a) {
    case 0xea: { clio_t *c = d->clio; c->dsp_rand = (c->dsp_rand * 0x41c64e6du + 0x3039u) & 0x7fffffffu; return (uint16_t)(c->dsp_rand >> 16); }
    case 0xeb: return D6A(d);
    case 0xec: return SEM_ST(d);
    case 0xed: return SEM_DT(d);
    case 0xee: return PC(d);
    case 0xef: return (uint16_t)d->remain;
    }
    if (a >= 0xf0 && a <= 0xfc) {                  /* FIFO de entrada: devolve a amostra do EI e ja repoe a proxima */
        uint16_t v = d->ei[a & 0x7f];
        d->ei[a & 0x7f] = in_pop(d, a & 0xf);
        return v;
    }
    if (a >= 0x70 && a <= 0x7e) return d->ei[a & 0x7f];
    if (a > 0x2ff) return 0;
    if (a > 0xff) return d->imem[a & 0xff];
    return d->ei[a & 0x7f];
}
/* FUN_0042e3a0 */
static void wr(dsp_t *d, unsigned a, uint16_t v) {
    a &= 0xffff;
    switch ((a - 0x3ec) & 0xffff) {
    case 0: ST(0); SEM_ST(d) |= 1; return;
    case 1: ST(1); SEM_DT(d) = v; SEM_ST(d) = 4; return;
    case 2: ST(2); ph_dsp_ee++; d->eo[0xee] = v; clio_raise_irq(d->clio, 0x800, 0); return;    /* FUN_0048f010: IRQ0 bit 11 */
    case 4: case 5: case 6: case 7: ST(3); out_push(d, a & 0xf, v); return;
    case 0x11: return;
    default:
        if (a < 0x300) { if (a > 0xff) d->imem[a & 0xff] = v; }
        else { d->eo[a & 0xff] = v; if ((a & 0xff) >= 0xfe) ST(4 + (a & 1)); if (v && (a&0xff)>=0xfe) ST(6); }
    }
}

/* ---- FUN_00427f00: flags de decodificacao ---- */
static void decode_flags(dsp_t *d) {
    uint16_t ins = INS(d), a = ins & 0x300;
    int A = (a == 0x300) || ((ins & 0xc00) == 0xc00);
    d->dcb = A && !(d->mask & 0x10);
    d->dca = A && !(d->mask & 8) && (ins & 0x1000);
    int B1 = (a == 0x100) || ((ins & 0xc00) == 0x400);
    d->dc9 = B1 && !(d->mask & 4);
    int B2 = (a == 0x200) || ((ins & 0xc00) == 0x800);
    d->dc8 = B2 && !(d->mask & 2);
    d->dc7 = ((ins & 0xf) == 8) && !(d->mask & 1);
    d->dce = (uint16_t)(((ins >> 3) & 0x10) | (ins & 0xf));
}

/* ---- FUN_0042de40: busca os operandos; devolve o custo em ciclos ---- */
static int fetch_operands(dsp_t *d) {
    uint16_t ins = INS(d);
    uint8_t isv[16]; uint16_t val[20];
    d->dc6 = 0;
    if (((ins & 0x300) == 0x300) || ((ins & 0xc00) == 0xc00)) d->extra = (ins & 0x70) ? 2 : 3;
    else d->extra = (ins & 0x70) ? 2 : 1;
    unsigned t = (ins >> 13) & 3; int need;
    if (t == 0) {
        if (d->dc9 || d->dc8) need = 5 - (d->dc7 == 0);
        else { need = 1; if (!d->dc7) return 1; }
    } else need = (int)(t + 1) - (d->dc7 == 0);
    uint16_t n = 0, last = 0, v, w, a1, a2; int cost = 1; unsigned idx = 0, flag9;
    do {
        if (PC(d) >= 0x400) { flag9 = 0; last = 0x380; v = rd(d, last); goto joined; }
        w = d->nmem[PC(d)]; PC(d)++;
        unsigned t5 = w >> 13;
        if (t5 == 5) {
            if (w & 0x400) {
                unsigned f = (w >> 5) & 0xf;
                if (!(w & 0x200)) { a1 = addr_of(d, f); last = rd(d, a1); }
                else { a1 = addr_of(d, f); cost++; a1 = rd(d, a1); last = rd(d, a1); }
                if (w & 0x1000) { d->dcc = a1; d->dc6 = 1; }
                n++; isv[idx] = 1; cost++; val[idx] = last; idx = n;
            }
            if (!(w & 0x10)) { last = addr_of(d, w & 0xf); v = rd(d, last); }
            else { a1 = addr_of(d, w & 0xf); cost++; last = rd(d, a1); v = rd(d, last); }
            flag9 = w & 0x800; goto joined;
        }
        if (t5 == 4) {
            if (!(w & 0x400)) { flag9 = w & 0x800; last = w & 0x3ff; v = rd(d, last); goto joined; }
            cost++; flag9 = w & 0x800; last = rd(d, w & 0x3ff); v = rd(d, last); goto joined;
        }
        if (t5 == 6) {
            isv[idx] = 0; v = w & 0x1fff; if (w & 0x1000) v |= 0xf000;
            n++; val[idx] = v;
        } else if (t5 == 7) {
            isv[idx] = 0; n++; val[idx] = (uint16_t)(w << 3);
        } else {                                       /* 0..3: tres operandos */
            unsigned f1 = (w >> 10) & 0xf;
            if (!(w & 0x4000)) { a1 = addr_of(d, f1); last = rd(d, a1); }
            else { a1 = addr_of(d, f1); cost++; a1 = rd(d, a1); last = rd(d, a1); }
            isv[idx] = 1; val[idx] = last;
            unsigned f2 = (w >> 5) & 0xf;
            if (!(w & 0x200)) { cost++; a1 = addr_of(d, f2); last = rd(d, a1); }
            else { cost += 2; a1 = addr_of(d, f2); a1 = rd(d, a1); last = rd(d, a1); }
            isv[(uint16_t)(n + 1)] = 1; val[(uint16_t)(n + 1)] = last;
            uint16_t v3;
            if (!(w & 0x10)) { cost++; last = addr_of(d, w & 0xf); v3 = rd(d, last); }
            else { cost += 2; a2 = addr_of(d, w & 0xf); last = rd(d, a2); v3 = rd(d, last); }
            isv[(uint16_t)(n + 2)] = 1; val[(uint16_t)(n + 2)] = v3;
            n += 3;
        }
        idx = n; cost++;
        continue;
    joined:
        if (flag9) { d->dcc = last; d->dc6 = 1; }
        n++; isv[idx] = 1; cost++; val[idx] = v; idx = n;
        if (need <= (int)n) break;
    } while ((int)n < need);
    unsigned b = d->dcb != 0; uint16_t pos;
    if (b) d->m6 = val[0];
    pos = (uint16_t)b;
    if (d->dca == 0) {
    L_dfbd:
        if (d->dc9) { if (n == pos) goto L300; d->ma = val[pos++]; }
        if (d->dc8) { if (n == pos) return cost; d->mc = val[pos++]; }
        if (d->dc7) { if (n == pos) return cost; d->dce = val[pos]; goto L_e020; }
    } else {
        if (n != b) { d->m8 = val[pos]; pos = (uint16_t)(b + 1); goto L_dfbd; }
        pos = n;
    L300:
        if (d->dc8) return cost;
        if (d->dc7) return cost;
    }
    for (;;) {
        if (n <= pos) return cost;
        if (isv[pos]) break;
    L_e020:
        pos++;
    }
    d->dcc = last; d->dc6 = 1;
    return cost;
}

/* ---- FUN_0045e910: ULA, escrita do resultado e busca da proxima instrucao ---- */
static void alu_exec(dsp_t *d) {
    uint16_t ins = INS(d);
    unsigned selA = (ins >> 10) & 3, op = (ins >> 4) & 0xf;
    uint32_t A, B, u3;
#define SX(x) ((uint32_t)((int32_t)(int16_t)(x)))
#define MUL(x, y) (((SX(x) * SX(y)) * 2u) & 0xfffff000u)
    uint16_t acc_hi = (uint16_t)(d->acc >> 16);
    int ca = (op == 5 || op == 3);                    /* ADC/SBC: B = carry */
    if (selA == 1) A = SX(d->ma) << 16;
    else if (selA == 2) A = SX(d->mc) << 16;
    else if (selA == 0) A = d->acc;
    else if (ins & 0x1000) A = MUL(d->m8, d->m6);
    else if (ca) { A = 0; if (d->flags & 0x10) A = 0u - (SX(d->m6) << 16); }
    else A = MUL(acc_hi, d->m6);
    if (ca) B = (uint32_t)((d->flags >> 4) & 1) << 16;
    else {
        unsigned selB = (ins >> 8) & 3;
        if (selB == 1) B = SX(d->ma) << 16;
        else if (selB == 2) B = SX(d->mc) << 16;
        else if (selB == 0) B = d->acc;
        else if (!(ins & 0x1000)) B = MUL(acc_hi, d->m6);
        else B = MUL(d->m8, d->m6);
    }
    d->flags &= 0xeb;
    u3 = A;
    switch (op) {
    case 0: case 8: break;
    case 1:
        u3 = -B;
        d->flags = (uint8_t)((d->flags & 0xeb) | (((~(u3 | B)) >> 31) << 4) | (((B & u3) >> 31) << 2));
        break;
    case 2: case 3: {
        u3 = A + B; unsigned c = 1; uint32_t nu = ~u3;
        if ((int32_t)(A & B) >= 0 && (int32_t)(nu & A) >= 0) c = (nu & B) >> 31;
        d->flags = (uint8_t)((d->flags & 0xef) | (c << 4));
        unsigned ov = 1;
        if ((int32_t)(nu & A & B) >= 0) ov = ((uint8_t)(~((uint8_t)(A >> 24) | (uint8_t)(B >> 24)) & (uint8_t)(u3 >> 24))) >> 7;
        d->flags = (uint8_t)((d->flags & 0xfb) | (ov * 4));
        break; }
    case 4: case 5: {
        unsigned c = 1; u3 = A - B;
        if ((int32_t)(~B & A) >= 0 && (int32_t)(~u3 & A) >= 0) c = (~(u3 | B)) >> 31;
        unsigned ov = 1;
        d->flags = (uint8_t)((d->flags & 0xef) | (c << 4));
        if ((int32_t)(~u3 & ~B & A) >= 0) ov = (~A & u3 & B) >> 31;
        d->flags = (uint8_t)((d->flags & 0xfb) | (ov << 2));
        break; }
    case 6:
        u3 = A + 0x1000;
        d->flags = (uint8_t)((d->flags & 0xeb) | (((~u3 & A) >> 31) << 4) | (((~A & u3) >> 31) << 2));
        break;
    case 7: {
        u3 = A - 0x1000; unsigned c = ((int32_t)A < 0) ? 1 : (~u3 >> 31);
        d->flags = (uint8_t)((d->flags & 0xeb) | (c << 4) | (((A & ~u3) >> 31) << 2));
        break; }
    case 10: u3 = A & B; break;
    case 0xb: u3 = ~(A & B) & 0xfffff000u; break;
    case 0xc: u3 = A | B; break;
    case 0xd: u3 = ~(A | B) & 0xfffff000u; break;
    case 0xe: u3 = A ^ B; break;
    case 0xf: u3 = ~(A ^ B) & 0xfffff000u; break;
    default: u3 = ~A & 0xfffff000u; break;           /* 9 */
    }
    if ((u3 & 0xffff0000u) == 0) { d->flags |= 2; d->flags &= 0xf7; }
    else { d->flags &= 0xfd; if ((int32_t)u3 >= 0) d->flags &= 0xf7; else d->flags |= 8; }
    if ((u3 & 0xf000u) != 0) d->flags &= 0xfe; else d->flags |= 1;
    switch (d->dce & 0x1f) {
    case 1: case 0x11: u3 = (u3 * 2) & 0xfffff000u; break;
    case 2: case 0x12: u3 = (u3 * 4) & 0xfffff000u; break;
    case 3: case 0x13: u3 = (u3 * 8) & 0xfffff000u; break;
    case 4: case 0x14: u3 = (u3 & 0xfffff00u) << 4; break;
    case 5: case 0x15: u3 = (u3 & 0x7ffff80u) << 5; break;
    case 6: case 0x16: u3 = (u3 & 0xfffff0u) << 8; break;
    case 7: case 0x17:
        if (d->flags & 4) u3 = ((((d->flags & 8) == 0) ? 0x1000u : 0u)) + 0x7ffff000u;
        else u3 &= 0xfffff000u;
        break;
    case 0: case 0x10: u3 &= 0xfffff000u; break;
    case 8: case 0x18: {
        uint8_t b = d->flags;
        d->flags = (uint8_t)((b & 0xef) | ((u3 >> 31) << 4));
        u3 = (u3 & 0xf000u) | ((uint32_t)((b >> 4) & 1) << 16) | ((u3 * 2) & 0xfffe0000u);
        break; }
    case 9:  u3 = (uint32_t)((int32_t)u3 >> 16) & 0xfffff000u; break;
    case 10: u3 = (uint32_t)((int32_t)u3 >> 8) & 0xfffff000u; break;
    case 0xb: u3 = (uint32_t)((int32_t)u3 >> 5) & 0xfffff000u; break;
    case 0xc: u3 = (uint32_t)((int32_t)u3 >> 4) & 0xfffff000u; break;
    case 0xd: u3 = (uint32_t)((int32_t)u3 >> 3) & 0xfffff000u; break;
    case 0xe: u3 = (uint32_t)((int32_t)u3 >> 2) & 0xfffff000u; break;
    case 0xf: u3 = (uint32_t)((int32_t)u3 >> 1) & 0xfffff000u; break;
    case 0x19: u3 = (u3 >> 16) & 0xfffff000u; break;
    case 0x1a: u3 = (u3 >> 8) & 0xfffff000u; break;
    case 0x1b: u3 = (u3 >> 5) & 0xfffff000u; break;
    case 0x1c: u3 = (u3 >> 4) & 0xfffff000u; break;
    case 0x1d: u3 = (u3 >> 3) & 0xfffff000u; break;
    case 0x1e: u3 = (u3 >> 2) & 0xfffff000u; break;
    default:   u3 = (u3 >> 1) & 0xfffff000u; break;
    }
    d->acc = u3;
    if (d->dc6) wr(d, d->dcc, (uint16_t)(u3 >> 16));
    uint16_t p = PC(d), w = 0x8380;
    if (p < 0x400) { w = d->nmem[p]; PC(d) = p + 1; }
    INS(d) = w;
}

/* ---- FUN_0045f000: MOVE (imediato / registrador -> destino) ---- */
static void fetch_next(dsp_t *d) {
    uint16_t p = PC(d), w = 0x8380;
    if (p < 0x400) { w = d->nmem[p]; PC(d) = p + 1; }
    INS(d) = w;
}
static int op_move(dsp_t *d, int byreg) {
    uint16_t pc0 = PC(d), W, ins, a, vv; uint32_t u6; int cost; unsigned c8;
    if (pc0 < 0x400) {
        W = d->nmem[pc0]; PC(d) = pc0 + 1; c8 = W >> 13; u6 = W;
        if (c8 == 5) {
            if (W & 0x400) goto skip;
            if (!(W & 0x10)) { a = addr_of(d, W & 0xf); u6 = rd(d, a); cost = 2; }
            else { a = addr_of(d, W & 0xf); vv = rd(d, a); u6 = rd(d, vv); cost = 3; }
        } else if (c8 < 6) {
            if (c8 != 4) goto skip;
            u6 = W & 0x3ff;
            if (W & 0x400) { vv = rd(d, u6); u6 = rd(d, vv); cost = 3; }
            else { u6 = rd(d, u6); cost = 2; }
        } else if (c8 == 6) { u6 = W & 0x1fff; if (W & 0x1000) u6 |= 0xf000; cost = 2; }
        else { u6 = (uint32_t)W * 8; cost = 2; }
    } else { u6 = rd(d, 0x380); cost = 2; }
    ins = INS(d);
    if (byreg) {
        if (!(ins & 0x10)) { wr(d, addr_of(d, ins & 0xf), (uint16_t)u6); }
        else { a = addr_of(d, ins & 0xf); wr(d, rd(d, a), (uint16_t)u6); }
    } else {
        if (!(ins & 0x400)) wr(d, ins & 0x3ff, (uint16_t)u6);
        else wr(d, rd(d, ins & 0x3ff), (uint16_t)u6);
    }
    fetch_next(d);
    return cost;
skip:   /* palavra de dados invalida: pula e carrega a instrucao seguinte */
    { uint16_t p1 = pc0 + 1; uint16_t w = 0x8380;
      if (p1 != 0x400) { w = d->nmem[p1]; PC(d) = pc0 + 2; }
      INS(d) = w; return 2; }
}

/* ---- FUN_00428080: desvio condicional ---- */
static void cond_branch(dsp_t *d) {
    uint16_t ins = INS(d), p = PC(d);
    if ((cond_tab[d->flags & 0x1f] >> ((ins >> 10) & 0x1f)) & 1) {
        INS(d) = (p < 0x400) ? d->nmem[p] : 0x8380;
        PC(d) = ins & 0x3ff; d->delay = 1; return;
    }
    if (p > 0x3ff) { INS(d) = 0x8380; return; }
    INS(d) = d->nmem[p]; PC(d) = p + 1;
}

/* ---- FUN_0045f270: instrucoes de controle (bit 15 = 1) ---- */
static int ctrl_exec(dsp_t *d) {
    uint16_t ins = INS(d);
    int e;
#define DEC0() do { if (d->extra) d->extra--; } while (0)
    if (((ins >> 13) & 3) != 0) {
        if (d->delay == 0) { cond_branch(d); e = (int)d->extra; d->extra = 0; return e + 1; }
        goto L2bb;
    }
    switch ((ins >> 10) & 7) {
    case 0:
        switch ((ins >> 7) & 7) {
        case 0: case 6: goto L2bb;
        case 1:
            if (d->delay == 0) {
                e = (int)d->extra; d->extra = 0;
                INS(d) = (PC(d) < 0x400) ? d->nmem[PC(d)] : 0x8380;
                d->delay = 1; PC(d) = (uint16_t)((d->acc >> 16) & 0x3ff);
                return e + 1;
            }
            fetch_next(d); d->delay = 0;
            DEC0();
            return 1;
        case 2: DEC0(); d->rbase = (uint16_t)((ins * 4) & 0xff); goto L2c5;
        case 3: DEC0(); d->rmode = ins & 7; goto L2c5;
        case 4:
            DEC0();
            INS(d) = (PC(d) < 0x400) ? d->nmem[PC(d)] : 0x8380;
            d->delay = 1; PC(d) = (uint16_t)d->retaddr;
            return 1;
        case 5: DEC0(); d->mask = ins & 0x1f; goto L2c5;
        default:
            DEC0();
            if (d->delay == 0) { d->clio->dsp_halt = 0; return 1; }
            goto L2c5;
        }
    case 1:
        DEC0();
        if (d->delay == 0) {
            INS(d) = (PC(d) < 0x400) ? d->nmem[PC(d)] : 0x8380;
            PC(d) = ins & 0x3ff; d->delay = 1; return 1;
        }
        goto L2c5;
    case 2:
        DEC0();
        if (d->delay == 0) {
            uint16_t p = PC(d);
            INS(d) = (p < 0x400) ? d->nmem[p] : 0x8380;
            d->retaddr = p; PC(d) = ins & 0x3ff; d->delay = 1; return 1;
        }
        goto L2c5;
    case 3:
        DEC0();
        if (d->delay != 0) {
            INS(d) = (PC(d) < 0x400) ? d->nmem[PC(d)] : 0x8380;
            PC(d) = ins & 0x3ff; return 1;
        }
        fetch_next(d); return 1;
    case 4:
        if (d->delay == 0) {
            int c = op_move(d, 1), r = (int)d->extra - c;
            d->extra = (uint32_t)(r >= 0 ? r : 0); return c;
        }
        break;
    case 5: break;
    default:
        if (d->delay == 0) {
            int c = op_move(d, 0), r = (int)d->extra - c;
            d->extra = (uint32_t)(r >= 0 ? r : 0); return c;
        }
    }
L2bb:
    DEC0();
L2c5:
    fetch_next(d); d->delay = 0;
    return 1;
}

/* ---- API ---- */
/* Construtor do .exe (0x471340..0x471491): EI/I/EO zerados, d72 = 0x238, N-mem inteira = 0x8380 (dormir),
 * da0 = 0x237, dbc = 4, db8 = 1, PC = 1, instrucao atual = N[0]. O estado que mora no CLIO e posto em dsp_set_clio. */
void dsp_init(dsp_t *d) {
    memset(d, 0, sizeof *d);
    D72(d) = 0x238;                         /* 568 ciclos por amostra (relogio padrao) */
    for (int i = 0; i < DSP_NMEM_WORDS; i++) d->nmem[i] = 0x8380;
    d->remain = 0x237;
}
void dsp_set_clio(dsp_t *d, clio_t *c) {
    d->clio = c; c->dsp_run = 4; c->dsp_halt = 1; c->dsp_in_a = 1; c->dsp_in_b = d->nmem[0];
}
void dsp_free(dsp_t *d) { (void)d; }

/* FUN_00427d90 */
void dsp_reset(dsp_t *d, int flag) {
    clio_t *c = d->clio; if (!c) return;
    d->remain = flag ? (int16_t)(D72(d) - 1) : 0x237;
    c->dsp_run |= 4;
    d->rbase = 0; d->rmode = 0; d->mask = 0; d->delay = 0; d->extra = 0;
    c->dsp_halt = 1;                        /* "em execucao" (o Peek 0x17fc devolve dsp_halt==0 como "dormindo") */
    PC(d) = 1; INS(d) = d->nmem[0];
}
uint16_t dsp_read16(dsp_t *d, uint32_t idx) {                /* FUN_0045f8c0 */
    if ((uint16_t)idx < 0x300) return 0;
    switch (idx & 0xff) {
    case 0xec: return d->clio ? d->clio->sem_status : SEM_ST(d);
    case 0xed: return d->clio ? d->clio->sem_data : SEM_DT(d);
    }
    return d->eo[idx & 0xff];
}
void dsp_write16(dsp_t *d, uint32_t idx, uint16_t v) { if ((uint16_t)idx < 0x100) d->ei[idx & 0x7f] = v; }   /* FUN_0045f8a0 */
void dsp_nwrite16(dsp_t *d, uint32_t idx, uint16_t v) { d->nmem[idx & 0x3ff] = v; }

/* FUN_0046d570 (caminho interpretado) */
uint32_t ph_dsp_steps, ph_dsp_passes, ph_dsp_ee;
uint32_t dsp_step(dsp_t *d) {
    clio_t *c = d->clio;
    ph_dsp_steps++;
    if (c) { SEM_ST(d) = c->sem_status; SEM_DT(d) = c->sem_data; }
    if (c && (c->dsp_run & 1)) {
        if (!c->dsp_halt || D6A(d) != 0) {
            ph_dsp_passes++; c->dsp_run |= 4; d->rbase = 0; d->rmode = 0; d->delay = 0;
            d->remain = (int16_t)(D72(d) - 1); d->mask = 0; d->extra = 0;
            c->dsp_halt = 1; PC(d) = 1; INS(d) = d->nmem[0];
        }
        d->start = d->remain;
        int16_t sv = d->remain;
        while (sv > 0 || D6A(d) != 0) {
            int cost;
            if (!(INS(d) & 0x8000)) {
                if (d->delay == 0) { decode_flags(d); cost = fetch_operands(d); alu_exec(d); }
                else {
                    if (d->extra) d->extra--;
                    uint16_t p = PC(d), w = 0x8380;
                    if (p < 0x400) { w = d->nmem[p]; PC(d) = p + 1; }
                    INS(d) = w; cost = 1; d->delay = 0;
                }
            } else cost = ctrl_exec(d);
            sv = (int16_t)(d->remain - (int16_t)cost);
            d->remain = sv;
            if ((uint16_t)(d->start - sv) > 0x238 || !c->dsp_halt) goto out;
        }
        c->dsp_halt = 0;
    }
out:
    if (c) { c->sem_status = SEM_ST(d); c->sem_data = SEM_DT(d); }
    return ((uint32_t)OUT_L(d) << 16) | OUT_R(d);
}
