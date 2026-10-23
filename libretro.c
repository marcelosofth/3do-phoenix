/* libretro.c - front-end libretro do Phoenix 3DO (Parte 18).
 * Liga o nucleo (machine/arm60/clio/madam/sport/vdlp) ao RetroArch: retro_init, retro_load_game, retro_run,
 * retro_get_system_av_info, video, audio e NVRAM.
 * Video: o VDLP (vdlp.c, portado do .exe) vive dentro do ph_machine_t (g_m.vdlp) e entrega RGB24 por faixas de
 * linhas sujas (gancho blit); aqui cada faixa e convertida para XRGB8888/RGB565 no framebuffer g_fb. O fim de
 * quadro e detectado no gancho clio.vdlp_line (FUN_004788b0), porque o gancho present so dispara quando alguma
 * linha mudou.
 *
 * O que NAO existe no nucleo ainda (ver PLANO_PHOENIX.md): DSP (fase 5), controles
 * (joybus no CLIO) e o rasterizador CEL geral (fase 4). O front-end ja chama tudo que existe; quando
 * esses blocos entrarem, basta preencher os ganchos em machine_boot(). */
#include "libretro.h"
#include "machine.h"
#include "vdlp.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LR_MAX_W     VDLP_MAX_WIDTH     /* 384 (PAL); NTSC usa 320 */
#define LR_MAX_H     VDLP_MAX_LINES     /* 288 (PAL); NTSC usa 240 */
#define LR_XRGB8888  0
#define LR_RGB565    1
#define AUDIO_RATE   44100.0
#define NTSC_LINES   263
#define PAL_LINES    313
#define SAMPLES_MAX  4096

static retro_environment_t        env_cb;
static retro_video_refresh_t      video_cb;
static retro_audio_sample_t       audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t         input_poll_cb;
static retro_input_state_t        input_state_cb;
static retro_log_printf_t         log_cb;

static ph_machine_t g_m;
static int          g_have_machine;
static void        *g_fb;                       /* LR_MAX_W * LR_MAX_H * 4 bytes; linhas com passo = largura do VDLP * bytes/pixel */
static int          g_frame_ready;              /* o VDLP fechou o quadro visivel (FUN_004788b0) */
static unsigned     g_frames;                   /* quadros fechados desde o boot */
static int          g_pal;                      /* opcao phoenix_region */
static int          g_fmt = LR_XRGB8888;        /* opcao phoenix_pixel_format */
static char         g_bios_choice[32] = "auto";
static int          g_debug;                    /* opcao phoenix_debug: diagnostico no log (padrao desligado) */
static int          g_hle;                      /* opcao phoenix_hle_audio: HLEs temporarios de audio (padrao desligado) */
static char         g_system_dir[1024];
static char         g_disc_path[1024];
static int16_t      g_audio[SAMPLES_MAX * 2];
static unsigned     g_audio_n;
static uint8_t      g_nvram_keep[PH_NVRAM_BYTES];

static void logf_(int lv, const char *fmt, ...) {
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (log_cb) log_cb(lv, "[Phoenix] %s", buf); else fprintf(stderr, "[Phoenix] %s", buf);
}

static void cd_trace_cb(const char *m)   { logf_(RETRO_LOG_INFO, "CD> %s\n", m); }
static void clio_trace_cb(const char *m) { logf_(RETRO_LOG_INFO, "CLIO> %s\n", m); }

/* ------------------------------------------------------------------ opcoes */
static const struct retro_variable g_vars[] = {
    { "phoenix_region",       "Regiao (reiniciar); NTSC|PAL" },
    { "phoenix_pixel_format", "Formato de video (reiniciar); xrgb8888|rgb565" },
    { "phoenix_bios",         "BIOS (reiniciar); auto|fz10|fz1|goldstar|sanyotry|panafz10" },
    { "phoenix_hle_audio",    "HLE temporario de audio, so p/ diagnostico (reiniciar); off|on" },
    { "phoenix_debug",        "Diagnostico no log (reiniciar); off|on" },
    { NULL, NULL }
};
static void read_options(void) {
    struct retro_variable v;
    v.key = "phoenix_region"; v.value = NULL;
    if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value) g_pal = !strcmp(v.value, "PAL");
    v.key = "phoenix_pixel_format"; v.value = NULL;
    if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value) g_fmt = !strcmp(v.value, "rgb565") ? LR_RGB565 : LR_XRGB8888;
    v.key = "phoenix_hle_audio"; v.value = NULL;
    if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value) g_hle = !strcmp(v.value, "on");
    v.key = "phoenix_debug"; v.value = NULL;
    if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value) g_debug = !strcmp(v.value, "on");
    v.key = "phoenix_bios"; v.value = NULL;
    if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value) { strncpy(g_bios_choice, v.value, sizeof g_bios_choice - 1); g_bios_choice[sizeof g_bios_choice - 1] = 0; }
}

/* ------------------------------------------------------------------ BIOS */
static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 0x400000) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)sz);
    if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); b = NULL; }
    fclose(f); if (b) *n = (size_t)sz; return b;
}
static int try_bios(ph_machine_t *m, const char *dir, const char *name) {
    static const char *sub[] = { "", "3do/", "BIOS/", "3do_phoenix/BIOS/", "Phoenix/" };
    for (unsigned i = 0; i < sizeof sub / sizeof *sub; i++) {
        char p[1400]; snprintf(p, sizeof p, "%s/%s%s", dir, sub[i], name);
        size_t n = 0; uint8_t *b = slurp(p, &n);
        if (!b) continue;
        if (n != 0x100000 && n != 0x200000) { logf_(RETRO_LOG_WARN, "BIOS ignorada (tamanho %zu): %s\n", n, p); free(b); continue; }
        ph_load_bios(m, b, n); free(b);
        logf_(RETRO_LOG_INFO, "BIOS: %s (%zu bytes)\n", p, n);
        if (n == 0x100000) {   /* FONT-ROM (kanji) no 2o banco: jogos japoneses que usam a fonte do sistema mostram texto em branco sem ela */
            static const char *fn[] = { "kanji.bin", "fontrom.bin", "font.bin", "fonts.bin", "kanjifont.bin", "3do_font.bin", "panafz1-kanji.bin", "fz1-font.bin" };
            for (unsigned k = 0; k < sizeof fn / sizeof *fn; k++) for (unsigned j = 0; j < sizeof sub / sizeof *sub; j++) {
                char q[1400]; snprintf(q, sizeof q, "%s/%s%s", dir, sub[j], fn[k]); size_t fnn = 0; uint8_t *fb = slurp(q, &fnn);
                if (!fb) continue;
                if (fnn == 0x100000) { ph_load_font_rom(m, fb, fnn); logf_(RETRO_LOG_INFO, "FONT-ROM: %s\n", q); free(fb); return 1; }
                free(fb);
            }
        }
        return 1;
    }
    return 0;
}
static int find_bios(ph_machine_t *m) {
    static const char *all[] = { "fz10.bin", "fz1.bin", "goldstar.bin", "sanyotry.bin", "panafz10.bin" };
    if (strcmp(g_bios_choice, "auto")) {
        char n[64]; snprintf(n, sizeof n, "%s.bin", g_bios_choice);
        return try_bios(m, g_system_dir, n);
    }
    for (unsigned i = 0; i < sizeof all / sizeof *all; i++) if (try_bios(m, g_system_dir, all[i])) return 1;
    return 0;
}

/* ------------------------------------------------------------------ ganchos do nucleo */
/* Os ganchos clio.vdlp_line_pending (-> vdlp_line) e madam.dma580_write (-> vdlp_set_head) ja vem ligados por ph_init()
 * (machine.c); aqui so entram o fim de quadro, o blit (RGB24 -> formato do frontend) e o audio. */
static void h_vdlp_frame(void *ud) { 
    ph_machine_t *m = ud; 
    vdlp_frame_end(&m->vdlp); 
    g_frame_ready = 1; 
    g_frames++; 
}
static void h_blit(void *ud, uint32_t id, int y0, int count, const uint8_t *rgb, int width) {
    (void)ud; (void)id;
    if (!g_fb || width <= 0 || width > LR_MAX_W || y0 < 0) return;
    if (y0 + count > LR_MAX_H) count = LR_MAX_H - y0;
    for (int y = 0; y < count; y++) {
        const uint8_t *src = rgb + (size_t)y * (size_t)width * 3;
        if (g_fmt == LR_RGB565) {
            uint16_t *dst = (uint16_t *)g_fb + (size_t)(y0 + y) * (size_t)width;
            for (int x = 0; x < width; x++, src += 3) dst[x] = (uint16_t)(((src[0] & 0xf8) << 8) | ((src[1] & 0xfc) << 3) | (src[2] >> 3));
        } else {
            uint32_t *dst = (uint32_t *)g_fb + (size_t)(y0 + y) * (size_t)width;
            for (int x = 0; x < width; x++, src += 3) dst[x] = ((uint32_t)src[0] << 16) | ((uint32_t)src[1] << 8) | src[2];
        }
    }
}

/* ------------------------------------------------------------------ DIAGNOSTICO (travamento apos carregar)
 * KDBG: texto de debug do kernel 3DO (SWI 0x1000e, 1 char por chamada); STAT: uma linha por segundo com PC, IRQs,
 * CEL (desenhados / nao suportados), CD e histograma dos SWIs. Tudo vai para o log do RetroArch. */
#define SWI_SLOTS 64
static struct { uint32_t imm, n, pc, r0, r1, r2, lr; } g_swi[SWI_SLOTS];
static unsigned g_swi_other, g_a7_logged;
/* HLE TEMPORARIO (ate existir DSP real): a inicializacao de audio do jogo grava um valor no knob PlayControl
 * (SWI 0x40000, r0=item, r1=valor) e espera le-lo de volta pela SWI 0x40007 (r1=9). Sem DSP nada volta e o jogo gira
 * para sempre. Aqui devolvemos o ultimo valor gravado no item que recebeu um valor >= 0x8000 (0 enquanto nenhum recebeu). */
static int g_pc_valid; static uint32_t g_pc_item, g_pc_val, g_pc_hits;
/* HLE TEMPORARIO 4: o jogo (0x10af90-0x10b030) zera/liga o PlayControl de um item NOVO (valores 0 e 1) e le de volta ate bater.
 * Como o item novo ainda nao recebeu valor >= 0x8000, o eco antigo devolvia o valor do item velho e o laco nunca saia.
 * Se o ultimo valor gravado foi 0 ou 1 e foi para outro item, devolvemos esse valor. */
static int g_lw_valid; static uint32_t g_lw_item, g_lw_val;
/* HLE TEMPORARIO 2: depois da inicializacao o jogo fica em SWI 0x4001e (r0=item 0x1005e), esperando o valor lido MUDAR
 * (contador de tempo do audio; o DSP ainda nao existe). Devolvemos um contador que avanca a cada amostra de audio. */
static uint32_t g_aud_ticks, g_t1e_hits, g_afolio_acc;
/* DIAGNOSTICO 22/10 (jogo volta para a intro ao iniciar a partida): cauda das ultimas SWIs, texto do kernel e codigo ao redor do PC */
#define SWI_RING 48
static struct { uint32_t imm, pc, r0, r1, r2; } g_ring[SWI_RING];
static unsigned g_ring_n, g_kdbg_n;
static void kdbg_text(uint32_t r0) {                  /* SWI 0x1000e: o argumento (caractere OU ponteiro para texto) vem em r1; r0 e o ponteiro do stream (constante) */
    static char line[200]; static unsigned ln;
    if (g_kdbg_n >= 600) return;
    if (r0 < 0x100u) {
        if (r0 == '\n' || ln >= sizeof line - 1) { line[ln] = 0; g_kdbg_n++; if (ln) logf_(RETRO_LOG_INFO, "KDBG: %s\n", line); ln = 0; }
        else if (r0 >= 0x20u) line[ln++] = (char)r0;
    } else if (r0 < 0x2fff00u) {
        char t[97]; unsigned k;
        for (k = 0; k < 96; k++) { uint8_t ch = ((const uint8_t *)g_m.ram)[(r0 + k) ^ 3u]; if (ch == 0) break; t[k] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '.'; }
        t[k] = 0; g_kdbg_n++;
        if (k >= 3) logf_(RETRO_LOG_INFO, "KDBG: @%08x '%s'\n", r0, t);
    }
}
static int diag_swi(void *u, uint32_t imm) {
    (void)u;
    unsigned i;
    g_ring[g_ring_n % SWI_RING].imm = imm; g_ring[g_ring_n % SWI_RING].pc = g_m.cpu.r[15]; g_ring[g_ring_n % SWI_RING].r0 = g_m.cpu.r[0];
    g_ring[g_ring_n % SWI_RING].r1 = g_m.cpu.r[1]; g_ring[g_ring_n % SWI_RING].r2 = g_m.cpu.r[2]; g_ring_n++;
    if (imm == 0x1000eu) kdbg_text(g_m.cpu.r[1]);   /* r1, nao r0: r0 e o stream */
    for (i = 0; i < SWI_SLOTS && g_swi[i].n && g_swi[i].imm != imm; i++) {}
    if (i == SWI_SLOTS) { g_swi_other++; return 0; }
    g_swi[i].imm = imm; g_swi[i].n++; g_swi[i].pc = g_m.cpu.r[15];
    g_swi[i].r0 = g_m.cpu.r[0]; g_swi[i].r1 = g_m.cpu.r[1]; g_swi[i].r2 = g_m.cpu.r[2]; g_swi[i].lr = g_m.cpu.r[14];
    if (imm == 0x40000) {
        uint32_t it = g_m.cpu.r[0], v = g_m.cpu.r[1];
        if (v >= 0x8000u && v < 0x10000u) { g_pc_valid = 1; g_pc_item = it; }
        if (g_pc_valid && it == g_pc_item) g_pc_val = v;
        g_lw_item = it; g_lw_val = v; g_lw_valid = 1;
    }
    if (g_debug && imm == 0x40007 && g_a7_logged < 3 && (g_swi[i].n == 1 || g_swi[i].n == 5000)) {   /* audio folio, funcao 7: le registradores de hardware */
        uint32_t a = g_m.cpu.r[0] & 0x3ffffcu; char b[400]; int n = 0;
        for (int k = 0; k < 16 && a + 4u * (unsigned)k < 0x300000u; k++) n += snprintf(b + n, sizeof b - (size_t)n, " %08x", g_m.ram[(a >> 2) + (unsigned)k]);
        g_a7_logged++;
        logf_(RETRO_LOG_INFO, "A7 args@%06x:%s | clio3c00:", a, b);
        for (int k = 0; k < 16; k++) logf_(RETRO_LOG_INFO, " %08x", ph_read32(&g_m, 0x03403c00u + 4u * (unsigned)k));
        logf_(RETRO_LOG_INFO, "\n | madam400:");
        for (int k = 0; k < 33; k++) logf_(RETRO_LOG_INFO, " %08x", ph_read32(&g_m, 0x03300400u + 4u * (unsigned)k));
        logf_(RETRO_LOG_INFO, "\n");
    }
    if (g_hle && imm == 0x4001e) {                    /* HLE temporario: contador de tempo do audio (qualquer item: o jogo usa 0x1005e e depois 0x3009e) */
        static uint32_t seen[8]; static unsigned nseen; unsigned q;
        for (q = 0; q < nseen && seen[q] != g_m.cpu.r[0]; q++) {}
        if (q == nseen && nseen < 8) { seen[nseen++] = g_m.cpu.r[0]; logf_(RETRO_LOG_INFO, "HLE-1E: item novo %08x\n", g_m.cpu.r[0]); }
        g_m.cpu.r[0] = g_aud_ticks & 0xffffu;
        if (++g_t1e_hits == 1) logf_(RETRO_LOG_INFO, "HLE-1E: contador de audio ligado (temporario, sem DSP real)\n");
        return 1;
    }
    if (g_hle && imm == 0x40007 && g_m.cpu.r[1] == 9u) {                       /* HLE temporario: eco do PlayControl */
        if (g_lw_valid && g_lw_val <= 1u && !(g_pc_valid && g_lw_item == g_pc_item)) g_m.cpu.r[0] = g_lw_val;
        else g_m.cpu.r[0] = g_pc_valid ? g_pc_val : 0u;
        if (++g_pc_hits == 1) logf_(RETRO_LOG_INFO, "HLE-A7: eco do PlayControl ligado (temporario, sem DSP real)\n");
        return 1;
    }
    return 0;
}
static int g_dumped;
static int g_rl_left_boot, g_rl_fired;   /* usados por diag_second (guarda do dump fixo) e check_relaunch, abaixo */
static void diag_dump(void) {
    g_dumped = 1;                  /* RAM (3 MB, big-endian) + registradores do CLIO, para analise offline */
    char p[1500]; FILE *f; uint8_t *tmp = malloc(0x300000);
    if (!tmp) return;
    for (unsigned i = 0; i < 0xC0000; i++) { uint32_t w = g_m.ram[i]; tmp[4*i] = (uint8_t)(w >> 24); tmp[4*i+1] = (uint8_t)(w >> 16); tmp[4*i+2] = (uint8_t)(w >> 8); tmp[4*i+3] = (uint8_t)w; }
    snprintf(p, sizeof p, "%s/phoenix_dump_ram.bin", g_system_dir);
    if ((f = fopen(p, "wb"))) { fwrite(tmp, 1, 0x300000, f); fclose(f); logf_(RETRO_LOG_INFO, "DUMP: RAM gravada em %s\n", p); }
    else logf_(RETRO_LOG_ERROR, "DUMP: nao consegui gravar %s\n", p);
    snprintf(p, sizeof p, "%s/phoenix_dump_clio.bin", g_system_dir);
    if ((f = fopen(p, "wb"))) { fwrite(g_m.clio.R, 1, 0x10000, f); fclose(f); }
    free(tmp);
}
/* diag_snapshot: nucleo comum do que antes so saia 1x/s dentro de diag_second (REG, SWITAIL, CODEPC).
 * Extraido para poder ser chamado tambem IMEDIATAMENTE pelo gatilho de evento (ver check_relaunch),
 * em vez de esperar ate 1s pelo proximo tick de diag_second e correr o risco de perder o instante exato. */
static void diag_snapshot(const char *tag) {
    logf_(RETRO_LOG_INFO, "REG %s r0-r14: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n", tag,
          g_m.cpu.r[0], g_m.cpu.r[1], g_m.cpu.r[2], g_m.cpu.r[3], g_m.cpu.r[4], g_m.cpu.r[5], g_m.cpu.r[6], g_m.cpu.r[7], g_m.cpu.r[8],
          g_m.cpu.r[9], g_m.cpu.r[10], g_m.cpu.r[11], g_m.cpu.r[12], g_m.cpu.r[13], g_m.cpu.r[14]);
    {   /* ultimas SWIs (da mais antiga para a mais nova) e codigo ao redor do PC: da para desmontar offline (armdis.py) */
        unsigned tot = g_ring_n < SWI_RING ? g_ring_n : SWI_RING, st = g_ring_n - tot;
        for (unsigned k = 0; k < tot; k += 8) {
            char ln[512]; int n = 0;
            for (unsigned j = k; j < tot && j < k + 8; j++) { const unsigned e = (st + j) % SWI_RING;
                n += snprintf(ln + n, sizeof ln - (size_t)n, " %06x@%x(%x,%x,%x)", g_ring[e].imm, g_ring[e].pc, g_ring[e].r0, g_ring[e].r1, g_ring[e].r2); }
            logf_(RETRO_LOG_INFO, "SWITAIL %s #%u:%s\n", tag, k / 8, ln);
        }
        uint32_t pc = g_m.cpu.r[15] & ~3u;
        if (pc >= 0x40u && pc < 0x2fff00u) {
            char ln[600]; int n = 0; const uint32_t a0 = pc - 0x40u;
            for (uint32_t k = 0; k < 32; k++) n += snprintf(ln + n, sizeof ln - (size_t)n, " %08x", g_m.ram[(a0 >> 2) + k]);
            logf_(RETRO_LOG_INFO, "CODEPC %s %06x:%s\n", tag, a0, ln);
        }
    }
}
static void diag_second(unsigned sec) {
    if (!g_debug) return;
    char tag[16]; snprintf(tag, sizeof tag, "t=%us", sec);
    logf_(RETRO_LOG_INFO, "STAT t=%us PC=%08x CPSR=%08x IRQ0=%08x EN0=%08x IRQ1=%08x EN1=%08x cel(drawn=%u unsup=%u poly=%u) cd(S=%02x lba=%d) otherSWI=%u\n",
          sec, g_m.cpu.r[15], g_m.cpu.cpsr, g_m.clio.R[0x40 >> 2], g_m.clio.R[0x48 >> 2], g_m.clio.R[0x60 >> 2], g_m.clio.R[0x68 >> 2],
          g_m.cel.drawn, g_m.cel.unsupported, g_m.cel.poly, g_m.cd.S, g_m.cd.lba, g_swi_other);
    for (unsigned i = 0; i < SWI_SLOTS && g_swi[i].n; i++)
        logf_(RETRO_LOG_INFO, "SWI t=%us imm=%06x n=%u lastpc=%08x lr=%08x r0=%08x r1=%08x r2=%08x\n", sec, g_swi[i].imm, g_swi[i].n, g_swi[i].pc, g_swi[i].lr, g_swi[i].r0, g_swi[i].r1, g_swi[i].r2);
    if (sec >= 25 && sec % 3 == 0) {          /* formatos de CEL desenhados nos ultimos 3 s (para achar o que sai errado) */
        for (unsigned q = 0; q < 48 && g_m.cel.hist[q].n; q++)
            logf_(RETRO_LOG_INFO, "CELFMT t=%us n=%u path=%u flags=%08x pre0=%08x pre1=%08x ppmp=%08x pixc=%08x clip=%08x wr=%06x/%x rd=%06x/%x src=%06x\n", sec, g_m.cel.hist[q].n, g_m.cel.hist[q].path,
                  g_m.cel.hist[q].flags, g_m.cel.hist[q].pre0, g_m.cel.hist[q].pre1, g_m.cel.hist[q].ppmp, g_m.cel.hist[q].pixc,
                  g_m.cel.hist[q].clip, g_m.cel.hist[q].wrb, g_m.cel.hist[q].wrs, g_m.cel.hist[q].rdb, g_m.cel.hist[q].rds, g_m.cel.hist[q].src);
        logf_(RETRO_LOG_INFO, "CELFMT t=%us outros=%u\n", sec, g_m.cel.hist_other);
        memset(g_m.cel.hist, 0, sizeof g_m.cel.hist); g_m.cel.hist_other = 0;
    }
    logf_(RETRO_LOG_INFO, "CELINV t=%us cels=%u sem_pixel=%u (culled=%u fora_da_tela=%u so_transparente=%u degenerado=%u outro=%u)\n", sec,
          g_m.cel.cels_total, g_m.cel.inv_total, g_m.cel.inv_culled, g_m.cel.inv_offscreen, g_m.cel.inv_transp, g_m.cel.inv_degen, g_m.cel.inv_other);
    for (unsigned q = 0; q < g_m.cel.ninv; q++)
        logf_(RETRO_LOG_INFO, "CELINV t=%us ex%u path=%u fl=%08x pre0=%08x pre1=%08x wh=%dx%d pos=%08x,%08x hd=%08x,%08x vd=%08x,%08x hdd=%08x,%08x clip=%08x tri=%u culled=%u\n", sec, q,
              g_m.cel.inv[q].path, g_m.cel.inv[q].flags, g_m.cel.inv[q].pre0, g_m.cel.inv[q].pre1, g_m.cel.inv[q].w, g_m.cel.inv[q].h, g_m.cel.inv[q].xpos, g_m.cel.inv[q].ypos,
              g_m.cel.inv[q].hdx, g_m.cel.inv[q].hdy, g_m.cel.inv[q].vdx, g_m.cel.inv[q].vdy, g_m.cel.inv[q].hddx, g_m.cel.inv[q].hddy, g_m.cel.inv[q].clip, g_m.cel.inv[q].tri, g_m.cel.inv[q].tri_culled);
    g_m.cel.cels_total = g_m.cel.inv_total = g_m.cel.inv_culled = g_m.cel.inv_offscreen = g_m.cel.inv_transp = g_m.cel.inv_degen = g_m.cel.inv_other = 0; g_m.cel.ninv = 0;
    diag_snapshot(tag);
    memset(g_swi, 0, sizeof g_swi); g_swi_other = 0; if (sec % 5 == 0) g_a7_logged = 0;
    if (sec == 52) {               /* codigo do laco da SWI 0x40007 (carregado depois do 20o segundo) em hexa no proprio log */
        static const uint32_t ini[2] = { 0x10af40u, 0x10d180u }, qtd[2] = { 64u, 24u };
        for (int r = 0; r < 2; r++) {
            logf_(RETRO_LOG_INFO, "CODE %06x:", ini[r]);
            for (uint32_t k = 0; k < qtd[r]; k++) logf_(RETRO_LOG_INFO, " %08x", g_m.ram[(ini[r] >> 2) + k]);
            logf_(RETRO_LOG_INFO, "\n");
        }
        if (!g_rl_fired) diag_dump();   /* se o relancamento ja foi pego por check_relaunch, nao sobrescreve o dump bom com um do meio do jogo */
    }
}
/* DIAGNOSTICO 22/10 rodada 2: o dump fixo em sec==52 so pega o estado certo se o relancamento acontecer bem
 * naquele segundo -- na sessao anterior o jogo foi bem mais longe e o dump saiu no meio do jogo normal, sem
 * valor. Troca por um gatilho por EVENTO: dispara exatamente quando a maquina volta pro estado de boot/reset.
 *
 * CORRECAO 22/10 rodada 3: a 1a versao do gatilho (if (frame > 200 && pc < 0x3000)) deu FALSO POSITIVO logo
 * no frame 201, ainda dentro do boot normal -- o log mostrou PC voltando pra ~0x2500-0x2600 varias vezes
 * durante o jogo JA EM ANDAMENTO (ex.: t=11s, lba=1229, nada errado), porque essa faixa baixa tem rotina(s)
 * de driver (CD) chamada(s) via SWI a partir de codigo de jogo normal, nao e exclusiva do boot. PC<endereco
 * e um sinal ruim: qualquer rotina de baixo nivel reusa esses enderecos a vida toda.
 * Os dois sinais trocados abaixo SO aparecem, nos dois logs ja vistos, no boot/reset de verdade, nunca durante
 * o jogo: cd.S==0x80 (status inicial do CLIO, visto so em "CD> CDT poll_read 80"/"poll_write v=80" nos
 * primeiros ~10 quadros) e PC==0x03000000 (vetor de reset da CPU, visto literalmente so no frame=1). */
static void check_relaunch(unsigned frame) {
    if (!g_rl_left_boot) { if (frame > 20 && g_m.cd.S != 0x80u) g_rl_left_boot = 1; return; }
    if (g_rl_fired) return;
    uint32_t pc = g_m.cpu.r[15];
    if (g_m.cd.S == 0x80u || pc == 0x03000000u) {
        g_rl_fired = 1;
        logf_(RETRO_LOG_INFO, "RELANCAMENTO DETECTADO frame=%u PC=%08x cd.S=%02x cd.lba=%d (gatilho: %s)\n",
              frame, pc, g_m.cd.S, g_m.cd.lba, g_m.cd.S == 0x80u ? "cd.S==0x80" : "PC==0x03000000 (vetor de reset)");
        diag_snapshot("RELOAD");
        diag_dump();
    }
}
static void h_audio_out(void *ud, uint32_t s) {                                                 /* L = 16 bits altos, R = baixos (suposicao: sem DSP ainda) */
    g_aud_ticks++;
    /* HLE TEMPORARIO 3: relogio do audio folio. A biblioteca de audio le o tempo em [[0x7bf10]+0x9c], que o folio incrementa
     * ~239,7 vezes/s (44100/184) a partir de uma interrupcao do DSP; sem DSP o valor fica em 0 e os fades de paleta do
     * jogo nunca terminam. Incrementamos esse contador aqui (so depois de o folio existir). Enderecos da BIOS fz1. */
    if (g_hle && g_have_machine && ++g_afolio_acc >= 184u) {
        g_afolio_acc = 0;
        uint32_t kb = g_m.ram[0x7bf10u >> 2];
        if (kb >= 0x1000u && kb < 0x2ffe00u && !(kb & 3u)) g_m.ram[(kb + 0x9cu) >> 2]++;
    }
    (void)ud;
    if (g_audio_n < SAMPLES_MAX) { g_audio[2 * g_audio_n] = (int16_t)(s >> 16); g_audio[2 * g_audio_n + 1] = (int16_t)s; g_audio_n++; }
}

static void machine_free(void) { if (g_have_machine) { ph_free(&g_m); g_have_machine = 0; } }

/* ------------------------------------------------------------------ controles (PBUS) */
/* Dispositivo escolhido por porta no frontend (RetroArch: Controles > Tipo de dispositivo). Padrao: 3DO Joypad no P1, desligado no P2.
 * Guardado aqui porque o frontend pode chamar retro_set_controller_port_device antes do retro_load_game (que zera a maquina). */
#define PH_DEV_JOYPAD RETRO_DEVICE_JOYPAD
static unsigned g_port_dev[2] = { RETRO_DEVICE_JOYPAD, RETRO_DEVICE_NONE };

static void apply_port_devices(void) {
    for (unsigned p = 0; p < 2; p++) joybus_set_connected(&g_m.joybus, p, g_port_dev[p] != RETRO_DEVICE_NONE);
}

/* Descricoes dos botoes no menu de controles do RetroArch (mapeamento igual ao do Opera: ver joybus.c) */
static void set_input_descriptors(void) {
    if (!env_cb) return;
    struct retro_input_descriptor d[2 * 12 + 1];
    unsigned n = 0;
    static const struct { unsigned id; const char *txt; } m[] = {
        { RETRO_DEVICE_ID_JOYPAD_UP,     "D-Pad Up" },
        { RETRO_DEVICE_ID_JOYPAD_DOWN,   "D-Pad Down" },
        { RETRO_DEVICE_ID_JOYPAD_LEFT,   "D-Pad Left" },
        { RETRO_DEVICE_ID_JOYPAD_RIGHT,  "D-Pad Right" },
        { RETRO_DEVICE_ID_JOYPAD_Y,      "A" },
        { RETRO_DEVICE_ID_JOYPAD_B,      "B" },
        { RETRO_DEVICE_ID_JOYPAD_A,      "C" },
        { RETRO_DEVICE_ID_JOYPAD_L,      "L (Left Shoulder)" },
        { RETRO_DEVICE_ID_JOYPAD_R,      "R (Right Shoulder)" },
        { RETRO_DEVICE_ID_JOYPAD_START,  "P (Play/Pause)" },
        { RETRO_DEVICE_ID_JOYPAD_SELECT, "X (Stop)" },
        { RETRO_DEVICE_ID_JOYPAD_X,      "P (Play/Pause)" },
    };
    for (unsigned p = 0; p < 2; p++) {
        if (g_port_dev[p] == RETRO_DEVICE_NONE) continue;
        for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++) {
            d[n].port = p; d[n].device = RETRO_DEVICE_JOYPAD; d[n].index = 0; d[n].id = m[i].id; d[n].description = m[i].txt; n++;
        }
    }
    d[n].description = NULL; d[n].port = d[n].device = d[n].index = d[n].id = 0;
    env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, d);
}


static int machine_boot(void) {
    machine_free();
    if (ph_init(&g_m) != 0) { logf_(RETRO_LOG_ERROR, "falha ao alocar RAM/BIOS\n"); return 0; }
    g_have_machine = 1;
    if (!find_bios(&g_m)) {
        logf_(RETRO_LOG_ERROR, "BIOS 3DO nao encontrada em '%s' (procurei fz10.bin, fz1.bin, goldstar.bin, sanyotry.bin, panafz10.bin; tambem em 3do/, BIOS/, 3do_phoenix/BIOS/)\n", g_system_dir);
        machine_free(); return 0;
    }
    memcpy(g_m.nvram, g_nvram_keep, PH_NVRAM_BYTES);
    cd_trace_hook = g_debug ? cd_trace_cb : NULL;
    clio_trace_hook = g_debug ? clio_trace_cb : NULL;
    if (g_disc_path[0]) {                       /* CD no XBUS ID 0 (PARTE20): a BIOS enxerga o disco pelos registradores 0x03400500.. do CLIO */
        if (ph_cd_insert(&g_m, g_disc_path) != 0) { logf_(RETRO_LOG_ERROR, "nao consegui ler a imagem: %s\n", g_disc_path); machine_free(); return 0; }
        logf_(RETRO_LOG_INFO, "CD: %s (%u setores de %d bytes)\n", g_disc_path, (unsigned)g_m.cd.num_sectors, CD_SECTOR_SIZE);
    }
    if (g_pal && ph_set_pal(&g_m, 1) != 0) { logf_(RETRO_LOG_ERROR, "falha ao criar o VDLP PAL\n"); machine_free(); return 0; }   /* refaz o VDLP 384x288 e poe o CLIO em PAL */
    g_m.clio.vdlp_line = h_vdlp_frame;          /* depois de ph_set_pal: fim de quadro (FUN_004788b0) + marca o quadro pronto */
    g_m.clio.audio_out = h_audio_out;
    g_m.vdlp.blit      = h_blit;
    /* Joybus callbacks */
    if (g_m.joybus_set_callbacks) g_m.joybus_set_callbacks(&g_m.joybus, input_poll_cb, input_state_cb);
    apply_port_devices();
    set_input_descriptors();
    memset(g_fb, 0, (size_t)LR_MAX_W * LR_MAX_H * 4);
    g_frame_ready = 0; g_frames = 0;
    g_m.clio.R[0x28 >> 2] = 0x01;              /* CSTAT: bit PON (power-on). A BIOS testa (CSTAT & 0x43) == 1 para o boot a frio; com 0 ela cai num laco de erro (achado da Parte 18) */
    /* PC=0x03000000 e vram_flag=1 devem sair prontos de ph_init() (REA em FUN_004a7f60, rodada 19b). O machine.c que veio no
     * 3do_phoenix.zip ainda NAO tem isso (PC=0, vram_flag=0): se for o caso, o front-end completa o reset aqui, sem alterar o nucleo. */
    if (g_m.cpu.r[15] == 0) { g_m.cpu.r[15] = 0x03000000u; g_m.vram_flag = 1; }
    g_audio_n = 0;
    g_m.cpu.trace = g_debug;
    g_m.cpu.swi_hook = (g_debug || g_hle) ? diag_swi : NULL;
    return 1;
}

/* ------------------------------------------------------------------ API libretro */
RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_set_environment(retro_environment_t cb) {
    env_cb = cb;
    bool yes = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &yes);
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)g_vars);
    struct retro_log_callback lg;
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &lg)) log_cb = lg.log;
    {   /* tipos de controle por porta (so o joypad 3DO esta implementado) */
        static const struct retro_controller_description pad[] = { { "3DO Joypad", RETRO_DEVICE_JOYPAD } };
        static const struct retro_controller_info ports[] = { { pad, 1 }, { pad, 1 }, { NULL, 0 } };
        cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);
    }
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)       { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)         { audio_cb = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb)             { input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb)           { input_state_cb = cb; if (g_have_machine) joybus_set_callbacks(&g_m.joybus, input_poll_cb, input_state_cb); }

RETRO_API void retro_init(void) {
    const char *sd = NULL;
    g_system_dir[0] = 0;
    if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sd) && sd) { strncpy(g_system_dir, sd, sizeof g_system_dir - 1); g_system_dir[sizeof g_system_dir - 1] = 0; }
    else strcpy(g_system_dir, ".");
    if (!g_fb) g_fb = calloc((size_t)LR_MAX_W * LR_MAX_H, 4);
    memset(g_nvram_keep, 0, sizeof g_nvram_keep);
    g_have_machine = 0;
    machine_set_debug_log(logf_);
}
RETRO_API void retro_deinit(void) {
    machine_free();
    free(g_fb); g_fb = NULL;
}

RETRO_API void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof *info);
    info->library_name     = "Phoenix 3DO";
    info->library_version  = "0.1-dev";
    info->valid_extensions = "iso|chd|cue|bin|img";
    info->need_fullpath    = true;
    info->block_extract    = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info) {
    memset(info, 0, sizeof *info);
    info->geometry.base_width   = g_pal ? 384 : 320;          /* NTSC 320x240, PAL 384x288 (vdlp.c) */
    info->geometry.base_height  = g_pal ? 288 : 240;
    info->geometry.max_width    = LR_MAX_W;
    info->geometry.max_height   = LR_MAX_H;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    /* ARM60 a 12,5 MHz; linha = 0x15a8/7 = 792 ciclos (NTSC, 263 linhas) ou 0x15d7/7 (PAL, 313 linhas): clio.c, FUN_00490890 */
    info->timing.fps         = g_pal ? 12500000.0 / (PAL_LINES * (5591.0 / 7.0)) : 12500000.0 / (NTSC_LINES * 792.0);
    info->timing.sample_rate = AUDIO_RATE;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) {
    if (port >= 2) return;
    g_port_dev[port] = (device == RETRO_DEVICE_NONE) ? RETRO_DEVICE_NONE : RETRO_DEVICE_JOYPAD;
    if (g_have_machine) apply_port_devices();
    set_input_descriptors();
}

RETRO_API void retro_reset(void) {
    if (g_have_machine) memcpy(g_nvram_keep, g_m.nvram, PH_NVRAM_BYTES);
    read_options();
    machine_boot();
}

RETRO_API bool retro_load_game(const struct retro_game_info *game) {
    read_options();
    enum retro_pixel_format pf = (g_fmt == LR_RGB565) ? RETRO_PIXEL_FORMAT_RGB565 : RETRO_PIXEL_FORMAT_XRGB8888;
    if (!env_cb || !env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pf)) {
        pf = (pf == RETRO_PIXEL_FORMAT_XRGB8888) ? RETRO_PIXEL_FORMAT_RGB565 : RETRO_PIXEL_FORMAT_XRGB8888;
        if (!env_cb || !env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pf)) { logf_(RETRO_LOG_ERROR, "o frontend nao aceita XRGB8888 nem RGB565\n"); return false; }
        g_fmt = (pf == RETRO_PIXEL_FORMAT_RGB565) ? LR_RGB565 : LR_XRGB8888;
    }
    g_disc_path[0] = 0;
    if (game && game->path && game->path[0]) {
        FILE *f = fopen(game->path, "rb");
        if (!f) { logf_(RETRO_LOG_ERROR, "nao consegui abrir a imagem: %s\n", game->path); return false; }
        fclose(f);
        strncpy(g_disc_path, game->path, sizeof g_disc_path - 1);
    }
    if (!g_fb) g_fb = calloc((size_t)LR_MAX_W * LR_MAX_H, 4);
    return machine_boot() ? true : false;
}

RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) { (void)type; (void)info; (void)num; return false; }
RETRO_API void retro_unload_game(void) {
    if (g_have_machine) { memcpy(g_nvram_keep, g_m.nvram, PH_NVRAM_BYTES); if (g_debug && !g_dumped) diag_dump(); }
    machine_free();
}

static void flush_audio(void) {
    if (g_audio_n) {
        if (audio_batch_cb) audio_batch_cb(g_audio, g_audio_n);
        else if (audio_cb) for (unsigned i = 0; i < g_audio_n; i++) audio_cb(g_audio[2 * i], g_audio[2 * i + 1]);
    }
    g_audio_n = 0;
}

RETRO_API void retro_run(void) {
    static unsigned frame_dbg = 0;
    if (input_poll_cb) input_poll_cb();
    /* O RetroArch pode definir input_state/input_poll DEPOIS do retro_load_game: a copia guardada no boot ficava NULL e o pacote PBUS
     * nunca mudava. Repassa os callbacks atuais ao joybus a cada quadro. */
    if (g_have_machine) joybus_set_callbacks(&g_m.joybus, input_poll_cb, input_state_cb);
    if (g_m.joybus_poll) g_m.joybus_poll(&g_m.joybus);
    {   /* diagnostico bruto: estado de TODOS os botoes do RetroPad (portas 1 e 2) + teclado, quando muda, e a cada 600 quadros */
        static unsigned lastraw[2] = {0, 0}, lastk = 0, nraw = 0;
        unsigned raw[2] = {0, 0}, k = 0;
        if (input_state_cb) {
            for (unsigned pt = 0; pt < 2; pt++) for (unsigned id = 0; id < 16; id++)
                if (input_state_cb(pt, RETRO_DEVICE_JOYPAD, 0, id)) raw[pt] |= 1u << id;
            k = (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_RETURN) ? 1u : 0) | (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_UP) ? 2u : 0) |
                (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_z) ? 4u : 0) | (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_x) ? 8u : 0);
        }
        if (g_debug && g_have_machine && (((raw[0] != lastraw[0] || raw[1] != lastraw[1] || k != lastk) && nraw < 300) || frame_dbg % 600 == 0)) {
            lastraw[0] = raw[0]; lastraw[1] = raw[1]; lastk = k; nraw++;
            logf_(RETRO_LOG_INFO, "[pad] RAW retropad P1=%04x P2=%04x kbd(Enter,Up,Z,X)=%x state_cb=%s poll_cb=%s frame=%u",
                  raw[0], raw[1], k, input_state_cb ? "ok" : "NULL", input_poll_cb ? "ok" : "NULL", frame_dbg);
        }
    }
    {   /* diagnostico: registra no log do RetroArch cada mudanca do pacote PBUS (so quando algum botao muda; limitado a 300 linhas) */
        static uint8_t last[16] = {0}; static unsigned nlog = 0;
        if (g_debug && g_have_machine && memcmp(last, g_m.joybus.buf, 16) && nlog < 300) {
            memcpy(last, g_m.joybus.buf, 16); nlog++;
            logf_(RETRO_LOG_INFO, "[pad] PBUS: %02x %02x %02x %02x (P1=%02x%02x P2=%02x%02x) frame=%u",
                  last[0], last[1], last[2], last[3], last[0], last[1], last[2], last[3], frame_dbg);
        }
    }
    
    if (g_have_machine) {
        frame_dbg++;
        if (g_debug && (frame_dbg <= 10 || frame_dbg % 60 == 0)) {
            logf_(RETRO_LOG_INFO, "retro_run: frame=%u PC=%08x IRQ0=%08x EN0=%08x cdS=%02x cdR=%02x lba=%d remain=%d",
                  frame_dbg, g_m.cpu.r[15], g_m.clio.R[0x40 >> 2], g_m.clio.R[0x48 >> 2],
                  g_m.cd.S, g_m.cd.R, g_m.cd.lba, g_m.cd.remain);
        }
    }
    
    unsigned w = g_pal ? 384 : 320, h = g_pal ? 288 : 240;
    if (g_have_machine) {
        w = (unsigned)g_m.vdlp.width; h = (unsigned)g_m.vdlp.lines;
        /* roda ate o VDLP fechar o quadro visivel; teto de 2,2 quadros de ciclos para nao travar se a CPU/linhas pararem */
        uint32_t frame_cyc = g_pal ? (uint32_t)(PAL_LINES * 799) : (uint32_t)(NTSC_LINES * 792);
        uint32_t cap = frame_cyc * 22 / 10, spent = 0;
        g_frame_ready = 0;
        while (!g_frame_ready && spent < cap) {
            uint32_t t0 = g_m.total_cycles;
            ph_run(&g_m, 1024);
            spent += g_m.total_cycles - t0;
        }
    }
    if (g_debug && g_have_machine) check_relaunch(frame_dbg);     /* checa TODO quadro, nao so 1x/s: pega o instante exato do relancamento */
    if (video_cb) {
        size_t bpp = (g_fmt == LR_RGB565) ? 2 : 4;
        video_cb(g_have_machine ? g_fb : NULL, w, h, w * bpp);
    }
    flush_audio();
    if (g_debug && g_have_machine) {         /* diagnostico: uma linha STAT por segundo (ate 240) */
        static unsigned nrun, nlog;
        if (++nrun % 60 == 0 && nlog < 240) { nlog++; diag_second(nrun / 60); }
    }
}

RETRO_API size_t retro_serialize_size(void)                       { return g_have_machine ? ph_state_size(&g_m) : 0; }
RETRO_API bool   retro_serialize(void *data, size_t size)         { return g_have_machine && ph_state_save(&g_m, data, size) == 0; }
RETRO_API bool   retro_unserialize(const void *data, size_t size) {
    if (!g_have_machine || ph_state_load(&g_m, data, size) != 0) return false;
    g_audio_n = 0; g_frame_ready = 0;                        /* os ganchos do front-end sobrevivem ao load (ph_state_load religa o host) */
    return true;
}
RETRO_API void   retro_cheat_reset(void) {}
RETRO_API void   retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
RETRO_API unsigned retro_get_region(void)                         { return g_pal ? RETRO_REGION_PAL : RETRO_REGION_NTSC; }

RETRO_API void *retro_get_memory_data(unsigned id) {
    if (id == RETRO_MEMORY_SAVE_RAM) return g_have_machine ? (void *)g_m.nvram : (void *)g_nvram_keep;
    return NULL;
}
RETRO_API size_t retro_get_memory_size(unsigned id) { return id == RETRO_MEMORY_SAVE_RAM ? PH_NVRAM_BYTES : 0; }
