#include <stdio.h>
#include "machine.h"
#include "dsp.h"
#include "joybus.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* Debug logging callback set by libretro.c */
static void (*g_debug_log)(int level, const char *fmt, ...) = NULL;

void machine_set_debug_log(void (*cb)(int level, const char *fmt, ...)) {
    g_debug_log = cb;
}

/* g_debug_log (logf_ do libretro.c) e variadica: NAO se pode repassar um va_list como
 * argumento dela (UB -- no Win64 isso imprimiu o endereco do va_list da pilha como se
 * fosse o valor, envenenando o diagnostico do boot). Formata aqui e passa a string pronta. */
static void machine_vlogf(int level, const char *fmt, va_list ap) {
    if (!g_debug_log) return;
    char b[512];
    vsnprintf(b, sizeof b, fmt, ap);
    g_debug_log(level, "%s", b);
}

/* Wrapper for arm60_t debug_log signature: void (*debug_log)(void *ud, const char *fmt, ...) */
static void arm60_debug_log(void *ud, const char *fmt, ...) {
    (void)ud;
    va_list ap; va_start(ap, fmt);
    machine_vlogf(RETRO_LOG_INFO, fmt, ap);
    va_end(ap);
}

#define RAM_WORDS_MAX 0x400000u     /* 16 MB: DRAM 14 + VRAM 2 ou DRAM 15 + VRAM 1 */
#define BIOS_WORDS 0x80000u

/* MADAM / SPORT / CEL: gancho do usuario se existir, senao o bloco interno (madam.c, sport.c) */
static uint32_t m_madam_peek(ph_machine_t *m, uint32_t a) { return m->madam_peek ? m->madam_peek(m->ud, a) : madam_peek(&m->madam, a); }
static uint32_t m_madam_poke(ph_machine_t *m, uint32_t a, uint32_t v) { return m->madam_poke ? m->madam_poke(m->ud, a, v) : madam_poke(&m->madam, a, v); }
static int  m_cel_busy(ph_machine_t *m) { return m->cel_busy ? m->cel_busy(m->ud) : madam_cel_busy(&m->madam); }
static void m_cel_run(ph_machine_t *m, uint32_t n) { if (m->cel_run) m->cel_run(m->ud, n); else madam_cel_set(&m->madam, n); }

static uint32_t bios_word(ph_machine_t *m, uint32_t a, int fetch_alias) {
    /* regiao 0x30: se CLIO+0x84 bit2 => banco 1 (+0x100000); regiao 0x60: sempre banco 0 */
    uint32_t off = a & 0xffffc;
    if (!fetch_alias && (m->clio.R[0x84 >> 2] & 4)) off += 0x100000;
    return m->bios[off >> 2];
}
/* read32 (FUN_0049fea0, regioes 0-2/0x30/0x31/0x32/0x33/0x34) */
static uint32_t mem_word(ph_machine_t *m, uint32_t a, int full) {
    uint32_t r = (a >> 20) & 0xfff;
    if (a < m->ram_bytes) {                                  /* DRAM (regioes 0/1 no stock) e VRAM (regiao 2 no stock) */
        if (a >= m->dram_bytes && !m->vram_flag && m->vram_touch) m->vram_touch(m->ud); /* so se *(maq+0x3c)==0 */
        return m->ram[a >> 2];
    }
    switch (r) {
    case 0x30: return bios_word(m, a, 0);
    case 0x60: return bios_word(m, a, 1);
    }
    if (!full) return PH_BADACCE5;
    switch (r) {
    case 0x31: return ((a & 0xc0000) == 0x40000) ? m->nvram[(a >> 2) & 0x7fff] : PH_BADACCE5;
    case 0x32: if (((a ^ 0x3200000u) & 0xffffe000u) != 0) return PH_BADACCE5;
               return m->dev32_read ? m->dev32_read(m->ud, a & 0x1ffc) : sport_peek(&m->sport, a & 0x1ffc);
    case 0x33: return m_madam_peek(m, a);
    case 0x34: return clio_peek(&m->clio, a);
    }
    return PH_BADACCE5;
}
uint32_t ph_read32(ph_machine_t *m, uint32_t a) {
    uint32_t v = mem_word(m, a, 1);
    if (v == PH_BADACCE5 && a >= m->ram_bytes) {
        if (m->stat_bad_rd++ == 0) m->bad_rd_addr = a;
        m->bcount++;
        if (m->cpu.r[15] < 0x300000u && m->bn < 24 && (m->bn == 0 || m->bring[m->bn - 1].a != a || m->bring[m->bn - 1].pc != m->cpu.r[15])) {
            m->bring[m->bn].pc = m->cpu.r[15]; m->bring[m->bn].lr = m->cpu.r[14]; m->bring[m->bn].a = a; m->bring[m->bn].cnt = m->bcount; m->bn++;
        }
    }
    return v;
}

/* fetch (FUN_004a0030, 0x4a0030-0x4a0086, objdump direto no .exe, rodada 12): so regioes 0-2/0x30/0x60,
 * resto = 0xbadacce5. NUNCA chama vram_touch (FUN_0058e9f0) -- ao contrario do read32, que chama na regiao 2
 * quando vram_flag==0. mem_word() acima e so para read32; fetch tem corpo proprio para nao herdar essa chamada. */
uint32_t ph_fetch(ph_machine_t *m, uint32_t a) {
    uint32_t r = (a >> 20) & 0xfff;
    if (a < m->ram_bytes) return m->ram[a >> 2];
    switch (r) {
    case 0x30: return bios_word(m, a, 0);
    case 0x60: return bios_word(m, a, 1);
    }
    if (m->stat_bad_fetch++ == 0) m->bad_fetch_addr = a;
    return PH_BADACCE5;
}

uint8_t ph_read8(ph_machine_t *m, uint32_t a) {            /* FUN_004a0130 (0x4a0130-0x4a01e9), objdump rodada 12 */
    if (a < m->ram_bytes) {
        if (a >= m->dram_bytes && !m->vram_flag && m->vram_touch) m->vram_touch(m->ud); /* so se *(maq+0x3c)==0, igual ao read32 */
        return ((uint8_t *)m->ram)[a ^ 3];
    }
    switch ((a >> 20) & 0xfff) {
    case 0x31: return ((a & 0xc0000) == 0x40000) ? m->nvram[(a >> 2) & 0x7fff] : 0;  /* indice SEM ^3 (confirmado) */
    case 0x30: { uint32_t o = a & 0xfffff; if (m->clio.R[0x84 >> 2] & 4) o += 0x100000; return ((uint8_t *)m->bios)[o ^ 3]; }
    case 0x60: return ((uint8_t *)m->bios)[(a & 0xfffff) ^ 3];
    }
    return 0;
}

/* FUN_004a0090 (0x4a0090-0x4a0126): ponteiro de host para um bloco [a, a+len) usado por LDM (aceleracao).
 * Exige que a REGIAO inteira do bloco seja a mesma (a>>20 == (a+len)>>20); fora disso devolve NULL.
 * Regioes 0/1: ponteiro direto pra RAM sem tocar vram_touch. Regiao 2: mesma regra do read32 (chama
 * vram_touch se vram_flag==0) e so ENTAO devolve o ponteiro -- ou seja, um unico aviso para o bloco
 * inteiro, nao um por palavra. Regioes 0x30/0x60: ponteiro pra BIOS (mesma regra de banco). Resto: NULL.
 * O caminho lento ja usado em arm60.c (ph_read32 palavra a palavra) e equivalente em efeito (Parte 10),
 * entao esta funcao so importa se quisermos a aceleracao; nao mudei o LDM para usa-la. */
const uint32_t *ph_block_read_ptr(ph_machine_t *m, uint32_t a, uint32_t len) {
    uint32_t end = a + len;
    if ((a >> 20) != (end >> 20)) return NULL;
    uint32_t r = (a >> 20) & 0xfff;
    if (a < m->ram_bytes) {
        if (a >= m->dram_bytes && !m->vram_flag && m->vram_touch) m->vram_touch(m->ud);
        return &m->ram[a >> 2];
    }
    switch (r) {
    case 0x30: { uint32_t off = a & 0xffffc; if (m->clio.R[0x84 >> 2] & 4) off += 0x100000; return &m->bios[off >> 2]; }
    case 0x60: return &m->bios[(a & 0xffffc) >> 2];
    }
    return NULL;
}

static inline void ph_watch(ph_machine_t *m, uint32_t a, uint32_t v, uint32_t sz) {
    if ((v == 0xd6d66728u || v == 0xcd5d0000u) && m->vn < 24) {
        uint32_t i = m->vn++;
        m->vring[i].pc = m->cpu.r[15]; m->vring[i].lr = m->cpu.r[14]; m->vring[i].a = a; m->vring[i].v = v;
        m->vring[i].r0 = m->cpu.r[0]; m->vring[i].r1 = m->cpu.r[1]; m->vring[i].r2 = m->cpu.r[2]; m->vring[i].r3 = m->cpu.r[3];
    }
    if (m->tk_addr && a == m->tk_addr && sz == 4) {
        m->tk_n++;
        if (m->tk_cnt < 24) { uint32_t i = m->tk_cnt++; m->tkring[i].pc = m->cpu.r[15]; m->tkring[i].lr = m->cpu.r[14]; m->tkring[i].old = m->ram[a >> 2]; m->tkring[i].v = v; }
    }
    if (m->whi && !m->wfreeze && a >= m->wlo && a < m->whi) {
        uint32_t i = m->wn++ & 31;
        m->wring[i].pc = m->cpu.r[15]; m->wring[i].lr = m->cpu.r[14]; m->wring[i].a = a; m->wring[i].v = v; m->wring[i].sz = sz;
    }
}
void ph_write32(ph_machine_t *m, uint32_t a, uint32_t v) {  /* FUN_0049fda0 (tabela 0xb03940) */
    if (a < m->ram_bytes) {
        if (a >= m->dram_bytes && m->hires) { uint32_t i = (a - m->dram_bytes) >> 2; m->hr[0][i] = m->hr[1][i] = m->hr[2][i] = v; }   /* HiRes: a CPU escreve 1x = 4 subamostras iguais (como o opera_mem_write32) */
        ph_watch(m, a, v, 4); m->ram[a >> 2] = v;       /* FUN_0049fda0: nao chama FUN_0058e9f0; so consulta a tabela de watchpoints (depuracao, ignorada) */
        return;
    }
    switch ((a >> 20) & 0xfff) {
    case 0x31: if ((a & 0xc0000) == 0x40000) m->nvram[(a >> 2) & 0x7fff] = (uint8_t)v; break;
    case 0x32: if (m->dev32_write) m->dev32_write(m->ud, a & 0xfffc, v); else sport_poke(&m->sport, a & 0xfffc, v); break;
    case 0x33: {
        uint32_t r;
        /* DIAGNOSTICO AITD (so com phoenix_debug=on): quem grava um recorte minusculo (<=8x8) em REGCTL1 (0x03300134)? */
        if (a == 0x3300134u && m->cpu.trace && (v & 0x3ffu) < 8u && ((v >> 16) & 0x3ffu) < 8u) {
            static unsigned clipn;
            if (clipn++ < 40) {
                const arm60_t *c = &m->cpu;
                arm60_debug_log(NULL, "[CLIPTINY] REGCTL1=%08x pc=%08x lr=%08x r0-r7=%08x %08x %08x %08x %08x %08x %08x %08x wr=%08x rd=%08x", v, c->r[15], c->r[14],
                        c->r[0], c->r[1], c->r[2], c->r[3], c->r[4], c->r[5], c->r[6], c->r[7], m->madam.cel_w[15], m->madam.cel_w[14]);
            }
        }
        r = m_madam_poke(m, a, v);
        if (a == 0x3300100) { m->stall = r; if (r == 0) m_cel_run(m, 0); }
        break; }
    case 0x34: clio_poke(&m->clio, a, v); break;
    case 0x30: case 0x60: break;                             /* ROM: ignora */
    default: if (m->stat_bad_wr++ == 0) m->bad_wr_addr = a; break;   /* nao mapeado: ignora (conta para a auditoria) */
    }
}
void ph_write8(ph_machine_t *m, uint32_t a, uint8_t v) {    /* FUN_004a0530 (0x4a0530-0x4a05d1), objdump rodada 12 */
    if (a < m->ram_bytes) {
        if (a >= m->dram_bytes) {
            if (!m->vram_flag && m->vram_touch) m->vram_touch(m->ud); /* so se *(maq+0x3c)==0, igual ao read8/read32 */
            if (m->hires) { uint32_t o = (a - m->dram_bytes) ^ 3u; ((uint8_t *)m->hr[0])[o] = ((uint8_t *)m->hr[1])[o] = ((uint8_t *)m->hr[2])[o] = v; }   /* HiRes: idem write32 */
        }
        ph_watch(m, a & ~3u, v, 1); ((uint8_t *)m->ram)[a ^ 3] = v;      /* depois disso consulta a tabela de watchpoint
                                                                    (+0x159f568) e chama FUN_557550 se marcada --
                                                                    e so depuracao, ignorado aqui (ver Parte 11) */
        return;
    }
    switch ((a >> 20) & 0xfff) {
    case 0x31: if ((a & 0xc0000) == 0x40000) m->nvram[(a >> 2) & 0x7fff] = v; break;  /* indice SEM ^3 (confirmado) */
    /* BIOS (0x30/0x60): escrita ignorada -- confirmado, write8 nao tem case para essas regioes */
    }
}

/* FUN_004a0340 (0x4a0340-0x4a03ab): ponteiro de host para bloco de ESCRITA, usado por STM. So cobre RAM
 * (a e a+len precisam caber em 0..ram_bytes-1; fora disso devolve NULL); nao cobre BIOS/NVRAM/MMIO (fora de
 * RAM, STM sempre cai no caminho lento palavra a palavra). Antes de devolver o ponteiro, percorre a tabela
 * de watchpoints palavra a palavra chamando FUN_557550 pras marcadas -- e depuracao (ver nota do write8);
 * NAO toca vram_touch (igual ao write32: escrita nunca avisa a VRAM). Nao usada aqui pelo mesmo motivo do
 * ph_block_read_ptr: o caminho lento ja e equivalente em efeito observavel (Parte 10). */
uint32_t *ph_block_write_ptr(ph_machine_t *m, uint32_t a, uint32_t len) {
    if (a >= m->ram_bytes || a + len >= m->ram_bytes) return NULL;
    if (m->whi && a < m->whi && a + len > m->wlo) return NULL;   /* vigia ativo: forca caminho lento */
    return &m->ram[a >> 2];
}

/* adaptadores para arm60 e clio */
static uint32_t a_fetch(void *u, uint32_t a) { return ph_fetch(u, a); }
static uint32_t a_rd32(void *u, uint32_t a)  { return ph_read32(u, a); }
static uint8_t  a_rd8(void *u, uint32_t a)   { return ph_read8(u, a); }
static void a_wr32(void *u, uint32_t a, uint32_t v) { ph_write32(u, a, v); }
static void a_wr8(void *u, uint32_t a, uint8_t v)   { ph_write8(u, a, v); }
static uint32_t c_madam_peek(void *u, uint32_t off) { ph_machine_t *m = u; return m_madam_peek(m, 0x3300000u + off); }
static void c_madam_poke(void *u, uint32_t off, uint32_t v) { ph_machine_t *m = u; m_madam_poke(m, 0x3300000u + off, v); }
/* VDLP: o tick do CLIO chama FUN_00474410(vdlp, linha) e, se devolver 1, FUN_004788b0 */
static int  c_vdlp_pending(void *u) { 
    ph_machine_t *m = u; 
    int r = vdlp_line(&m->vdlp, m->clio.R[0x34 >> 2]);
    /* Atualiza o registrador VCNT (0x34) com a linha atual processada pelo VDLP.
     * O VDLP processa linhas sequencialmente e o registrador VCNT deve refletir a linha atual. */
    uint32_t current_line = m->clio.R[0x34 >> 2] & 0x7ff;
    m->clio.R[0x34 >> 2] = (m->clio.R[0x34 >> 2] & ~0x7ffu) | (current_line & 0x7ffu);
    return r;
}
static void c_vdlp_frame(void *u)   { ph_machine_t *m = u; vdlp_frame_end(&m->vdlp); }
/* MADAM 0x580: callback de membro (madam+0x1000bb8) -> HEADVDL. SUPOSICAO (alvo do ponteiro nao achado no .exe):
 * grava HEADVDL = valor (ja mascarado com 0xfffffc pelo madam_poke); CURRENTVDL so e carregado na linha 0. */
static void c_dma580(void *u, uint32_t v) { ph_machine_t *m = u; vdlp_set_head(&m->vdlp, v); }

/* MADAM MCTL (0x008) com PLAYXEN (0x8000): DMA8 da PBUS. Port fiel do HandleDMA8/DMAPBus do Opera
 * (opera_madam.c). Portfolio OS (ControlPort.c, EnableControlPort): a 1a palavra e 'sludge' do control
 * port e Length conta os bytes DEPOIS dela; seguem as palavras da cadeia PBUS (joybus.c, formato do
 * opera_pbus.c) e o resto ate esgotar Length e 0xFFFFFFFF. Ao terminar: PLAYXEN limpo, INT1 bit 0
 * (CLIO_INT1 = DMA da PBUS) e LEN = 0xFFFFFFFC (idle) -- o kernel (eventbroker/controles) espera isso. */
static void c_mctl(void *u, uint32_t v) {
    ph_machine_t *m = u; (void)v;
    uint32_t *R = m->madam.regs;
    int32_t len = (int32_t)R[0x574 >> 2];
    if (len >= 0) {
        uint32_t dst = R[0x570 >> 2], prog = R[0x578 >> 2];
        m->ram[(dst & 0x1ffffcu) >> 2] = 0xffffffffu;               /* sludge do control port */
        dst += 4; prog += 4;
        uint32_t buf[JOYBUS_BUFFER_WORDS]; unsigned nw = 0;
        if (m->joybus_get_pbus_buffer) {
            m->joybus_get_pbus_buffer(&m->joybus, 0, buf);
            nw = JOYBUS_BUFFER_WORDS;
        }
        for (unsigned i = 0; len > 0 && i < nw; i++) { m->ram[(dst & 0x1ffffcu) >> 2] = buf[i]; len -= 4; dst += 4; prog += 4; }
        while (len > 0) { m->ram[(dst & 0x1ffffcu) >> 2] = 0xffffffffu; len -= 4; dst += 4; prog += 4; }
        R[0x570 >> 2] = dst; R[0x578 >> 2] = prog; R[0x574 >> 2] = 0xfffffffcu;
    }
    R[2] &= ~0x8000u;
    clio_raise_irq(&m->clio, 0, 1);
}

/* MADAM cel_exec (FUN_00488700): async=1 so mede (devolve 0); async=0 percorre a cadeia de CCBs (PARTE19) */
static uint32_t c_cel_exec(void *u, uint8_t async) { ph_machine_t *m = u; return celexec_run(&m->cel, &m->madam, m->ram, async); }

/* CD no XBUS (PARTE20). O drive avisa a CPU por IRQ0 bit 2 (0x4) do CLIO: cd.c chama o gancho irq quando
 * (S & 0x11) == 0x11 ou (S & 0x22) == 0x22 (FUN_004aa150), isto e, status/dado pendente COM o enable ligado pela BIOS.
 * Teste (BIOS fz10 + ISO 3DO minimo): sem este gancho a BIOS manda so 0x83/0x82 e trava em PC=0x20600 com S=0x1f;
 * com ele ela segue a sequencia completa, emite o 0x10 (leitura), o setor 0 e lido e o DMA XBUS termina (IRQ0 0x20000000).
 * Nota: a decompilacao anterior (Ghidra 12.1.4) achava que FUN_004aa150 so consultava watchpoints; o comportamento
 * observado na BIOS real mostra que o bit 0x4 e necessario. */
/* IRQ do XBUS/CD por NIVEL, como o opera_xbus_tick: enquanto a condicao do drive persistir (status/dado pendente COM o
 * enable ligado), o bit 2 de IRQ0 e re-levantado a cada passo. O cd.c so avisa na transicao (irq_check); se a BIOS limpar o
 * pendente antes de tratar o status, o aviso se perdia. */
static void c_cd_tick(void *u, uint32_t cyc) {
    ph_machine_t *m = u; cd_tick(&m->cd, cyc);
#ifndef PH_CD_EDGE
    if (((m->cd.S & 0x11) == 0x11) || ((m->cd.S & 0x22) == 0x22)) clio_raise_irq(&m->clio, 0x4u, 0);
#endif
}
static void c_cd_irq(void *u) { ph_machine_t *m = u; clio_raise_irq(&m->clio, 0x4u, 0); }

/* Pontes CLIO -> DSP. O CLIO chama os ganchos com clio.ctx == ph_machine_t*; as funcoes dsp_clio_*
 * do dsp.c esperam um dsp_t*, o que escrevia por cima da struct da CPU (primeiro membro da maquina). */
static void     c_dsp_reset(void *ud, int flag)                { dsp_reset(&((ph_machine_t *)ud)->dsp, flag); }
static void     c_dsp_write16(void *ud, uint32_t i, uint16_t v){ dsp_write16(&((ph_machine_t *)ud)->dsp, i, v); }
static void     c_dsp_nwrite16(void *ud, uint32_t i, uint16_t v){ dsp_nwrite16(&((ph_machine_t *)ud)->dsp, i, v); }
/* registradores de canal DMA do MADAM sem efeitos colaterais (FIFOs do DSP) */
static uint32_t c_madam_raw_peek(void *u, uint32_t off) { ph_machine_t *m = u; return m->madam.regs[(off & 0x7fc) >> 2]; }
static void     c_madam_raw_poke(void *u, uint32_t off, uint32_t v) { ph_machine_t *m = u; m->madam.regs[(off & 0x7fc) >> 2] = v; }
static uint16_t c_dsp_read16(void *ud, uint32_t i)             { return dsp_read16(&((ph_machine_t *)ud)->dsp, i); }
static uint32_t c_dsp_step(void *ud)                           { return dsp_step(&((ph_machine_t *)ud)->dsp); }
static void     c_dsp_audio_out(void *ud, uint32_t s)          { (void)ud; (void)s; }

/* ---- NVRAM: volume 3DO "linked mem" (portado de opera_nvram.c; valores big-endian, como o BIOS le byte a byte) ---- */
static void nv_put32(uint8_t *nv, unsigned off, uint32_t v) { nv[off] = (uint8_t)(v >> 24); nv[off + 1] = (uint8_t)(v >> 16); nv[off + 2] = (uint8_t)(v >> 8); nv[off + 3] = (uint8_t)v; }
int ph_nvram_formatted(const uint8_t *nv) {
    if (nv[0] != 1 || nv[6] != 2) return 0;                         /* dl_RecordType = 1, dl_VolumeStructureVersion = 2 (LINKED_MEM) */
    for (int i = 1; i <= 5; i++) if (nv[i] != 'Z') return 0;        /* dl_VolumeSyncBytes */
    return 1;
}
void ph_nvram_format(uint8_t *nv) {
    const uint32_t LABEL = 132, BLK = 20;                           /* sizeof(DiscLabel), sizeof(LinkedMemBlock) */
    memset(nv, 0, PH_NVRAM_BYTES);
    nv[0] = 1; memset(nv + 1, 'Z', 5); nv[6] = 2; nv[7] = 0;
    memcpy(nv + 8, "phoenix formatted", 17);                        /* dl_VolumeCommentary */
    memcpy(nv + 40, "nvram", 5);                                    /* dl_VolumeIdentifier */
    nv_put32(nv, 72, 0xffffffffu);                                  /* dl_VolumeUniqueIdentifier = -1 */
    nv_put32(nv, 76, 1);                                            /* dl_VolumeBlockSize */
    nv_put32(nv, 80, PH_NVRAM_BYTES);                               /* dl_VolumeBlockCount */
    nv_put32(nv, 84, 0xfffffffeu);                                  /* dl_RootUniqueIdentifier = -2 */
    nv_put32(nv, 88, 0);                                            /* dl_RootDirectoryBlockCount */
    nv_put32(nv, 92, 1);                                            /* dl_RootDirectoryBlockSize */
    nv_put32(nv, 96, 0);                                            /* dl_RootDirectoryLastAvatarIndex */
    nv_put32(nv, 100, LABEL);                                       /* dl_RootDirectoryAvatarList[0] */
    nv_put32(nv, LABEL + 0,  0x855A02B6u);                          /* bloco ancora */
    nv_put32(nv, LABEL + 4,  LABEL + BLK); nv_put32(nv, LABEL + 8, LABEL + BLK);
    nv_put32(nv, LABEL + 12, BLK); nv_put32(nv, LABEL + 16, BLK);
    nv_put32(nv, LABEL + BLK + 0,  0x7AA565BDu);                    /* bloco livre: o resto da NVRAM */
    nv_put32(nv, LABEL + BLK + 4,  LABEL); nv_put32(nv, LABEL + BLK + 8, LABEL);
    nv_put32(nv, LABEL + BLK + 12, PH_NVRAM_BYTES - LABEL - BLK); nv_put32(nv, LABEL + BLK + 16, BLK);
}

/* DMA (XBUS/DSP) gravou na RAM: o que cair na VRAM vale para as 4 subamostras do HiRes */
static void c_ram_written(void *u, uint32_t addr, uint32_t nbytes) {
    ph_machine_t *m = u;
    if (!m->hires || nbytes == 0) return;
    uint32_t lo = addr < m->dram_bytes ? m->dram_bytes : addr, hi = addr + nbytes;
    if (hi > m->ram_bytes) hi = m->ram_bytes;
    if (lo >= hi) return;
    uint32_t w0 = (lo & ~3u) >> 2, w1 = (hi + 3u) >> 2;
    for (int q = 0; q < 3; q++) memcpy(&m->hr[q][w0 - (m->dram_bytes >> 2)], &m->ram[w0], (size_t)(w1 - w0) * 4);
}

int ph_set_hires(ph_machine_t *m, int on) {
    on = on ? 1 : 0;
    if (on == m->hires) return 0;
    if (on) {
        for (int q = 0; q < 3; q++) if (!m->hr[q] && !(m->hr[q] = malloc(m->vram_bytes))) return -1;
        for (int q = 0; q < 3; q++) memcpy(m->hr[q], &m->ram[m->dram_bytes >> 2], m->vram_bytes);      /* todas as subamostras partem da VRAM atual (2x por replicacao) */
        if (vdlp_set_hires(&m->vdlp, 1, m->hr[0], m->hr[1], m->hr[2]) != 0) return -1;
        for (int q = 0; q < 3; q++) m->sport.hr[q] = m->hr[q];
        opera_cel_set_hires(m->hr[0], m->hr[1], m->hr[2]);
    } else {
        opera_cel_set_hires(NULL, NULL, NULL);
        for (int q = 0; q < 3; q++) m->sport.hr[q] = NULL;
        vdlp_set_hires(&m->vdlp, 0, NULL, NULL, NULL);
    }
    m->hires = on;
    return 0;
}

int ph_mem_cfg_valid(unsigned dram_mb, unsigned vram_mb) {   /* as 8 combinacoes do Opera (0x21 0x22 0x41 0x42 0x81 0x82 0xE2 0xF1) */
    if (vram_mb != 1 && vram_mb != 2) return 0;
    if (dram_mb == 2 || dram_mb == 4 || dram_mb == 8) return 1;
    return (dram_mb == 14 && vram_mb == 2) || (dram_mb == 15 && vram_mb == 1);
}
int ph_init(ph_machine_t *m) { return ph_init_mem(m, 2, 1); }
/* MSYSBITS como o opera_mem_madam_red_sysbits: bits 0-2 = VRAM (MB); bits 3-4 = tamanho do banco de DRAM 1; bits 5-6 = banco 0 (1 = 1 MB, 2 = 4 MB, 3 = 16 MB) */
static uint32_t ph_mem_sysbits(unsigned dram_mb, unsigned vram_mb) {
    uint32_t v = vram_mb;
    if (dram_mb == 2)       v |= (1u << 5) | (1u << 3);
    else if (dram_mb == 4)  v |= (2u << 5);
    else if (dram_mb == 8)  v |= (2u << 5) | (2u << 3);
    else                    v |= (3u << 3);        /* 14 / 15 MB */
    return v;
}
int ph_init_mem(ph_machine_t *m, unsigned dram_mb, unsigned vram_mb) {
    if (!ph_mem_cfg_valid(dram_mb, vram_mb)) return -1;
    memset(m, 0, sizeof *m);
    m->dram_bytes = dram_mb << 20; m->vram_bytes = vram_mb << 20; m->ram_bytes = m->dram_bytes + m->vram_bytes;
    cel_set_ram_bytes(m->ram_bytes); opera_cel_set_mem(m->dram_bytes, m->vram_bytes);
    m->ram = calloc(m->ram_bytes / 4, 4); m->bios = calloc(BIOS_WORDS, 4);
    if (!m->ram || !m->bios) { free(m->ram); free(m->bios); m->ram = m->bios = NULL; return -1; }
    m->madam.sysbits = ph_mem_sysbits(dram_mb, vram_mb);
    m->slice = 128; m->clk_mult = 16;
    m->cpu.ud = m; m->cpu.fetch = a_fetch; m->cpu.rd32 = a_rd32; m->cpu.rd8 = a_rd8; m->cpu.wr32 = a_wr32; m->cpu.wr8 = a_wr8;
    m->cpu.debug_log = arm60_debug_log;
    arm60_reset(&m->cpu);
    clio_reset(&m->clio);
    madam_reset(&m->madam); m->madam.ud = m; m->madam.cel_enabled = 1;
    if (dram_mb != 2 || vram_mb != 1) m->madam.regs[1] = m->madam.sysbits;   /* fora do stock o MSYSBITS ja vale desde o reset (como o Opera) */
    dsp_init(&m->dsp); m->dsp.ctx = NULL; dsp_set_clio(&m->dsp, &m->clio);   /* ganchos do dsp_t ficam NULL: a memoria interna do dsp.c atende */
    joybus_init(&m->joybus);
    m->sport.vram = &m->ram[m->dram_bytes >> 2]; m->sport.vram_words = m->vram_bytes >> 2; m->sport.ud = m;   /* VRAM = RAM + 2 MB (ctor 0x4716a0) */
    m->clio.ctx = m; m->clio.ram8 = (uint8_t *)m->ram; m->clio.ram_bytes = m->ram_bytes; m->clio.ram_written = c_ram_written;
    cd_init(&m->cd, m, c_cd_irq);
    m->cd_dev.ctx = &m->cd;
    m->cd_dev.status_read = cd_status_read; m->cd_dev.cmd_write = cd_cmd_write;
    m->cd_dev.poll_read = cd_poll_read;     m->cd_dev.poll_write = cd_poll_write;
    m->cd_dev.data_read = cd_data_read;     m->cd_dev.data_write = cd_data_write;
    m->cd_dev.res_read = cd_res_read;
    m->cd_dev.media_access = cd_media_access;
    m->clio.dev[0] = &m->cd_dev; m->clio.cd_tick = c_cd_tick;
    m->clio.madam_peek = c_madam_peek; m->clio.madam_poke = c_madam_poke;
    m->clio.vdlp_line_pending = c_vdlp_pending; m->clio.vdlp_line = c_vdlp_frame;
    /* DSP hooks (PARTE5) */
    m->clio.dsp_step      = c_dsp_step;       /* clio.ctx = m (a maquina), NAO o dsp_t: usar as pontes abaixo */
    m->clio.audio_out     = c_dsp_audio_out;
    m->clio.dsp_reset     = c_dsp_reset;
    m->clio.dsp_write16   = c_dsp_write16;
    m->clio.dsp_nwrite16  = c_dsp_nwrite16;
    m->clio.madam_raw_peek = c_madam_raw_peek; m->clio.madam_raw_poke = c_madam_raw_poke;
    m->clio.dsp_read16    = c_dsp_read16;
    /* Joybus hooks. Atencao: a regiao 0x03200000 e o SPORT (FUN_004714d0/4714e0, ver sport.c);
     * o joybus NAO tem registradores mapeados na CPU -- ele so entra pela DMA8 da PBUS (c_mctl).
     * Por isso dev32_read/dev32_write ficam NULL aqui (o teste/substituicao continua possivel). */
    m->joybus_set_callbacks = joybus_set_callbacks;
    m->joybus_poll = joybus_poll;
    m->joybus_get_pbus_buffer = joybus_get_pbus_buffer;
    m->madam.dma580_write = c_dma580; m->madam.mctl_write = c_mctl; m->madam.cel_exec = c_cel_exec;
    if (vdlp_init(&m->vdlp, 0, &m->ram[m->dram_bytes >> 2]) != 0) return -1;   /* VRAM = RAM + DRAM */
    m->vdlp.vram_base = m->dram_bytes; m->vdlp.vram_bytes = m->vram_bytes;
    return 0;
}
int ph_cd_insert(ph_machine_t *m, const char *path) {
    if (cd_load_image(&m->cd, path) != 0) return -1;
    cd_reset(&m->cd, 1);          /* o reset zera o TOC (FUN_004aa020): monta o lead-out DEPOIS */
    cd_build_toc(&m->cd);
    return 0;
}
void ph_cd_eject(ph_machine_t *m) { cd_free(&m->cd); cd_reset(&m->cd, 0); }
void ph_free(ph_machine_t *m) { cd_free(&m->cd); dsp_free(&m->dsp); joybus_free(&m->joybus); celexec_free(&m->cel); vdlp_free(&m->vdlp); opera_cel_set_hires(NULL, NULL, NULL); for (int q = 0; q < 3; q++) { free(m->hr[q]); m->hr[q] = NULL; } m->hires = 0;
    free(m->ram); free(m->bios); m->ram = m->bios = NULL; }
int ph_set_region(ph_machine_t *m, int mode) {
    vdlp_t old = m->vdlp;                       /* guarda ganchos/estado para devolver se falhar */
    vdlp_t nv;
    if (vdlp_init_mode(&nv, mode, &m->ram[m->dram_bytes >> 2]) != 0) return -1;
    nv.vram_base = m->dram_bytes; nv.vram_bytes = m->vram_bytes;
    nv.ud = old.ud; nv.frame_id = old.frame_id; nv.set_background = old.set_background; nv.blit = old.blit; nv.present = old.present;
    nv.headvdl = old.headvdl; nv.bypass_clut = old.bypass_clut;
    if (m->hires && vdlp_set_hires(&nv, 1, m->hr[0], m->hr[1], m->hr[2]) != 0) { vdlp_free(&nv); return -1; }   /* novo tamanho => novo rgb_hr */
    vdlp_free(&old);
    m->vdlp = nv; m->clio.pal = nv.pal;
    return 0;
}
int ph_set_pal(ph_machine_t *m, int pal) { return ph_set_region(m, pal ? VDLP_MODE_PAL2 : VDLP_MODE_NTSC); }
void ph_set_cpu_overclock(ph_machine_t *m, double f) {
    if (f < 0.25) f = 0.25; else if (f > 8.0) f = 8.0;
    m->clk_q16 = (f > 0.9999 && f < 1.0001) ? 0u : (uint32_t)(65536.0 / f + 0.5);
    m->clk_acc = 0;
}
void ph_set_matrix_software(ph_machine_t *m, int software) {
    m->madam.regs[0] = software ? (m->madam.regs[0] | 1u) : (m->madam.regs[0] & ~1u);   /* MADAM_GREENWW do Opera = ID | 1 */
}
void ph_set_random_seed(ph_machine_t *m, uint32_t seed) { m->clio.dsp_rand = seed & 0x7fffffffu; }
void ph_load_font_rom(ph_machine_t *m, const uint8_t *img, size_t n) {
    if (n > 0x100000) n = 0x100000;
    for (size_t i = 0; i < n; i += 4) {
        uint32_t w = 0; for (int k = 0; k < 4; k++) w = (w << 8) | (i + k < n ? img[i + k] : 0);
        m->bios[(0x100000 + i) >> 2] = w;
    }
}
void ph_load_bios(ph_machine_t *m, const uint8_t *img, size_t n) {
    if (n > PH_BIOS_BYTES) n = PH_BIOS_BYTES;
    for (size_t i = 0; i < n; i += 4) {
        uint32_t w = 0; for (int k = 0; k < 4; k++) w = (w << 8) | (i + k < n ? img[i + k] : 0);
        m->bios[i >> 2] = w;
    }
}

/* linhas de interrupcao do CLIO (IRQ0/IRQ1 pendentes e habilitadas; bit 31 do IRQ0 nao conta) */
static int irq_line(ph_machine_t *m) {
    uint32_t *R = m->clio.R;
    return ((R[0x40 >> 2] & R[0x48 >> 2] & 0x7fffffffu) != 0) || ((R[0x60 >> 2] & R[0x68 >> 2]) != 0);
}
/* No 3DO a interrupcao do CLIO entra como FIQ (vetor 0x1c); o IRQ do ARM nunca e usado pelo laco. */
static void take_fiq(ph_machine_t *m) { arm60_fiq(&m->cpu); }

/* Soft reset pedido por escrita em CSTAT (0x03400028, bits 0x10/0x20) -- port de opera_3do_soft_reset/dipir_reset.
 * Preserva RAM/VRAM/registradores do CLIO e MADAM; reinicia so a CPU (SPSR_svc=CPSR, R14_svc=proximo PC, I/F=1,
 * PC=0x03000000 = vetor de reset na ROM), para o CEL e, com 0x20, marca os latches DIPIR como "antes do reset". */
void ph_soft_reset(ph_machine_t *m, uint32_t req) {
    arm60_t *c = &m->cpu;
    uint32_t cpsr = c->cpsr, ret = c->r[15];
    arm60_set_mode(c, M_SVC);
    c->spsr[3] = cpsr;
    c->cpsr = ((cpsr & ~0x1fu) | M_SVC) | 0xc0u;
    c->r[14] = ret;
    c->r[15] = 0x03000000u;
    if (c->mode_hook) c->mode_hook(c->ud);
    m->stall = 0;
    m->madam.cel_state = 0;
    if (req & 0x20u) {
        for (int i = 0; i < 2; i++) {
            uint32_t *r = &m->clio.R[(0x410 + 4 * i) >> 2], v = *r, o = 0;
            for (int s = 0; s <= 16; s += 16) { uint32_t f = (v >> s) & 0xffffu; if (f & 0x8000u) o |= (0x4000u | (f & 0xffu)) << s; }
            *r = o;
        }
        m->clio.xbus_sel = 0;
    }
}

void ph_run(ph_machine_t *m, int quantum) {                 /* FUN_004a0920 */
    int budget = quantum;
    arm60_t *c = &m->cpu;
    if (m->slice_begin) m->slice_begin(m->ud);
    for (;;) {
        uint32_t used = 0;
        while (used < m->slice) {
            for (;;) {
                if (m->break_valid && m->break_pc == c->r[15]) { used += m->slice; goto done; }
                if (m_cel_busy(m)) {
                    if ((c->cpsr & 0x1f) != 0x11) {          /* ARM parado enquanto o CEL roda (salvo em FIQ) */
                        if (m->slice < m->stall) { used += m->slice; m->stall -= m->slice; }
                        else { uint32_t s = m->stall; m->stall = 0; used += s; m_cel_run(m, 0); }
                        goto done;
                    }
                    m_cel_run(m, 0x30);
                }
                used += (uint32_t)arm60_step(c);
                if (m->clio.reset_req) { uint32_t rq = m->clio.reset_req; m->clio.reset_req = 0; ph_soft_reset(m, rq); }
                if (!irq_line(m) || (c->cpsr & 0x40)) break;
                take_fiq(m);
                if (m->slice <= used) goto done;
            }
        }
    done:
        budget -= (int)used;
        m->total_cycles += used;
        if (m->clk_q16) {                                   /* CPU Overclock: ticks = ciclos * (65536/fator) >> 16, com o resto carregado */
            uint64_t a = (uint64_t)m->clk_acc + (uint64_t)m->clk_q16 * used;
            m->clk_acc = (uint32_t)(a & 0xffffu);
            clio_tick(&m->clio, (uint32_t)(a >> 16));
        } else clio_tick(&m->clio, (uint32_t)(((int32_t)(m->clk_mult * (int32_t)used)) >> 4));
        if (irq_line(m) && !(c->cpsr & 0x40)) take_fiq(m);
        if (budget < 1) { if (m->slice_end) m->slice_end(m->ud); return; }
    }
}

/* ---------------------------------------------------------------- savestate ---------------------------------------------------------------- */
#include <stddef.h>
#define PH_STATE_MAGIC 0x50485354u   /* 'PHST' */
#define PH_STATE_VER   1u
typedef struct { uint32_t magic, ver, msize, ramsize, rgbsize, pad; } ph_state_hdr_t;

static size_t rgb_bytes(const ph_machine_t *m) { return (size_t)m->vdlp.width * (size_t)m->vdlp.lines * 3; }
size_t ph_state_size(const ph_machine_t *m) { return sizeof(ph_state_hdr_t) + sizeof *m + (size_t)m->ram_bytes + rgb_bytes(m); }

int ph_state_save(const ph_machine_t *m, void *buf, size_t size) {
    if (!m || !m->ram || size < ph_state_size(m)) return -1;
    uint8_t *p = buf;
    ph_state_hdr_t hd = { PH_STATE_MAGIC, PH_STATE_VER, (uint32_t)sizeof *m, m->ram_bytes, (uint32_t)rgb_bytes(m), m->dram_bytes >> 20 };   /* pad = DRAM em MB (14+2 e 15+1 somam 16 MB) */
    memcpy(p, &hd, sizeof hd); p += sizeof hd;
    memcpy(p, m, sizeof *m); p += sizeof *m;
    memcpy(p, m->ram, (size_t)m->ram_bytes); p += (size_t)m->ram_bytes;
    memcpy(p, m->vdlp.rgb, rgb_bytes(m));
    return 0;
}

int ph_state_load(ph_machine_t *m, const void *buf, size_t size) {
    const uint8_t *p = buf; ph_state_hdr_t hd;
    if (!m || !m->ram || size < sizeof hd) return -1;
    memcpy(&hd, p, sizeof hd);
    if (hd.magic != PH_STATE_MAGIC || hd.ver != PH_STATE_VER || hd.msize != sizeof *m || hd.ramsize != m->ram_bytes || hd.pad != (m->dram_bytes >> 20) || hd.rgbsize != rgb_bytes(m) || size < ph_state_size(m)) return -1;
    p += sizeof hd;
    static ph_machine_t keep;                       /* copia da maquina viva: dona de todos os ponteiros/ganchos validos nesta sessao */
    memcpy(&keep, m, sizeof keep);
    memcpy(m, p, sizeof *m); p += sizeof *m;
    memcpy(m->ram, p, (size_t)keep.ram_bytes); p += (size_t)keep.ram_bytes;
    memcpy(keep.vdlp.rgb, p, rgb_bytes(m));
    /* ---- religa o host ---- */
    uint32_t *ram = keep.ram; uint32_t *bios = keep.bios; void *ud = keep.ud;
    /* ganchos da propria maquina: tudo de `ud` ate o fim da estrutura nao tem dado de emulacao (so ponteiros de funcao) */
    memcpy((uint8_t *)m + offsetof(ph_machine_t, ud), (const uint8_t *)&keep + offsetof(ph_machine_t, ud), sizeof(ph_machine_t) - offsetof(ph_machine_t, ud));
    m->ram = ram; m->bios = bios; m->ud = ud;
    m->dram_bytes = keep.dram_bytes; m->vram_bytes = keep.vram_bytes; m->ram_bytes = keep.ram_bytes;
    m->cd_dev = keep.cd_dev;
    { arm60_t *c = &m->cpu; const arm60_t *k = &keep.cpu;
      c->ud = k->ud; c->fetch = k->fetch; c->rd32 = k->rd32; c->rd8 = k->rd8; c->wr32 = k->wr32; c->wr8 = k->wr8; c->swi_hook = k->swi_hook; c->mode_hook = k->mode_hook; c->debug_log = k->debug_log; c->trace = k->trace; }
    { clio_t *c = &m->clio; const clio_t *k = &keep.clio;
      memcpy(c->dev, k->dev, sizeof c->dev);
      memcpy((uint8_t *)c + offsetof(clio_t, ram8), (const uint8_t *)k + offsetof(clio_t, ram8), offsetof(clio_t, reset_req) - offsetof(clio_t, ram8)); }
    { madam_t *a = &m->madam; const madam_t *k = &keep.madam;
      memcpy((uint8_t *)a + offsetof(madam_t, ud), (const uint8_t *)k + offsetof(madam_t, ud), sizeof *a - offsetof(madam_t, ud)); }
    m->sport.vram = keep.sport.vram; m->sport.ud = keep.sport.ud; m->sport.event = keep.sport.event; m->sport.written = keep.sport.written;
    for (int q = 0; q < 3; q++) m->sport.hr[q] = keep.sport.hr[q];
    m->cel.dec = keep.cel.dec; m->cel.tex = keep.cel.tex; m->cel.legacy = keep.cel.legacy;   /* o motor de CEL e escolha do host, nao do estado salvo */
    { cd_t *c = &m->cd; const cd_t *k = &keep.cd;
      c->image = k->image; c->image_size = k->image_size; c->iso_fp = k->iso_fp; c->iso_ssize = k->iso_ssize; c->iso_soff = k->iso_soff; c->chd = k->chd;
      c->num_sectors = k->num_sectors; c->irq = k->irq; c->ud = k->ud;
      c->fifo_status.buf = k->fifo_status.buf; c->fifo_cmd.buf = k->fifo_cmd.buf; c->fifo_data.buf = k->fifo_data.buf; }
    m->dsp.clio = keep.dsp.clio; m->dsp.ctx = keep.dsp.ctx;
    m->joybus = keep.joybus;
    { vdlp_t *v = &m->vdlp; const vdlp_t *k = &keep.vdlp;
      v->vram = k->vram; v->rgb = k->rgb; v->ud = k->ud; v->set_background = k->set_background; v->blit = k->blit; v->present = k->present;
      v->hires = k->hires; v->rgb_hr = k->rgb_hr; for (int q = 0; q < 3; q++) v->hr[q] = k->hr[q];
      for (int i = 0; i < VDLP_MAX_LINES; i++) v->line[i].dirty = 1; }
    if (m->hires) for (int q = 0; q < 3; q++) memcpy(m->hr[q], &m->ram[m->dram_bytes >> 2], m->vram_bytes);   /* o estado nao guarda os planos extras: recomeca de 2x por replicacao */
    return 0;
}
