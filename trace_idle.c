/* trace_idle.c (v2) — a v1 supunha que a trava era um spin-loop simples em pc=0x284e8 (igual ao
 * visto no log do RetroArch). O teste mostrou que NAO e: nessa execucao headless a CPU so passa
 * por 0x284e0-0x28500 uma vez, bem cedo (cyc=41216), sai dali rapido, e so trava DE VERDADE bem
 * depois, quando o lba para de mudar (ultima leitura em lba=84) -- enquanto isso a task de
 * polling do MCTL/PLAYXEN continua rodando normal em background (ela nao e a culpada).
 *
 * Esta versao:
 *  1) continua logando toda escrita no MCTL (0x03300008) e toda mudanca de lba, como antes.
 *  2) detecta quando o lba fica parado por N iteracoes seguidas (janelas de 0x40000 ciclos) e,
 *     a partir dali, liga um HISTOGRAMA de PC visitado (hash aberto, ate HIST_CAP enderecos
 *     distintos) por um numero de iteracoes configuravel -- pra achar o(s) endereco(s) onde a
 *     CPU realmente passa a maior parte do tempo depois que o boot para, mesmo que nao seja um
 *     laco de 1 instrucao so.
 *  3) ao final da janela de amostragem, imprime os top-N enderecos por numero de fetches, o que
 *     mostra o loop/rotina real do travamento (ou confirma que o tempo se espalha por muitos
 *     enderecos -- o que indicaria outra coisa, tipo um laco maior ou varias tasks revezando).
 *
 * Uso:   trace_idle <bios.bin> <imagem.iso|.chd> [iteracoes=3000] [janelas_paradas_pra_amostrar=3] [iteracoes_de_amostra=20]
 * Build: gcc -O2 -std=c11 -I. -Ilibretro-common/include -o trace_idle trace_idle.c machine.c \
 *        arm60.c clio.c madam.c sport.c vdlp.c cel.c celexec.c cd.c cd_chd.c dsp.c joybus.c \
 *        chdobj/*.o -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"

#define MCTL_ADDR 0x03300008u
#define HIST_CAP 4096   /* enderecos distintos que o histograma consegue guardar */

typedef struct { uint32_t addr; uint64_t count; } hslot_t;
static hslot_t hist[HIST_CAP];

static ph_machine_t m;
static uint32_t (*o_fetch)(void*,uint32_t);
static void     (*o_wr32)(void*,uint32_t,uint32_t);

static int mctl_writes = 0;
static int32_t last_lba = -2;
static int lba_prints = 0;

static int sampling = 0;         /* histograma ligado? */
static int sample_iters_left = 0;
static int stall_reported = 0;

static void hist_add(uint32_t pc) {
    uint32_t h = (pc >> 2) % HIST_CAP;
    for (int probe = 0; probe < HIST_CAP; probe++) {
        uint32_t idx = (h + probe) % HIST_CAP;
        if (hist[idx].count == 0) { hist[idx].addr = pc; hist[idx].count = 1; return; }
        if (hist[idx].addr == pc) { hist[idx].count++; return; }
    }
    /* tabela cheia: ignora (nao deve acontecer com HIST_CAP=4096 numa janela curta) */
}

static void report_stall_entry(void) {
    stall_reported = 1;
    memset(hist, 0, sizeof(hist));   /* zera: nao misturar com histograma de uma pausa anterior */
    printf("\n### LBA PAROU DE MUDAR -- ligando histograma de PC. cyc=%u lba=%d\n", m.total_cycles, m.cd.lba);
    printf("    S=%02x R=%02x state=%u remain=%d\n", m.cd.S, m.cd.R, m.cd.state, m.cd.remain);
    printf("    CLIO IRQ0 pend=%08x en=%08x   IRQ1 pend=%08x en=%08x\n",
           m.clio.R[0x40>>2], m.clio.R[0x48>>2], m.clio.R[0x60>>2], m.clio.R[0x68>>2]);
    printf("    r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x\n",
           m.cpu.r[0],m.cpu.r[1],m.cpu.r[2],m.cpu.r[3],m.cpu.r[4],m.cpu.r[5],m.cpu.r[6],m.cpu.r[7]);
    printf("    r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x sp=%08x lr=%08x cpsr=%08x pc=%08x\n",
           m.cpu.r[8],m.cpu.r[9],m.cpu.r[10],m.cpu.r[11],m.cpu.r[12],m.cpu.r[13],m.cpu.r[14],m.cpu.cpsr,m.cpu.r[15]);
}

static void print_histogram(void) {
    /* ordenacao simples por contagem (poucos slots ocupados, bubble ta bom) */
    hslot_t tmp[HIST_CAP]; int n = 0;
    for (int i = 0; i < HIST_CAP; i++) if (hist[i].count) tmp[n++] = hist[i];
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (tmp[j].count > tmp[i].count) { hslot_t t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t; }
    printf("\n### HISTOGRAMA DE PC DURANTE O TRAVAMENTO (%d enderecos distintos vistos)\n", n);
    int top = n < 24 ? n : 24;
    for (int i = 0; i < top; i++)
        printf("    pc=%08x  %llu fetches\n", tmp[i].addr, (unsigned long long)tmp[i].count);
}

static uint32_t w_fetch(void *u, uint32_t a) {
    if (sampling) hist_add(a);
    return o_fetch(u, a);
}

static void w_wr32(void *u, uint32_t a, uint32_t v) {
    if (a == MCTL_ADDR) {
        mctl_writes++;
        if (mctl_writes <= 20 || sampling)
            printf("MCTL W v=%08x %s  pc=%08x cyc=%u lba=%d  (ocorrencia #%d)\n",
                   v, (v & 0x8000u) ? "[PLAYXEN]" : "", m.cpu.r[15], m.total_cycles, m.cd.lba, mctl_writes);
    }
    o_wr32(u, a, v);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "uso: %s <bios> <imagem> [iteracoes=3000] [janelas_paradas=3] [iteracoes_amostra=20]\n", argv[0]);
        return 1;
    }
    int it = argc > 3 ? atoi(argv[3]) : 3000;
    int stall_windows = argc > 4 ? atoi(argv[4]) : 3;
    int sample_iters = argc > 5 ? atoi(argv[5]) : 20;

    if (ph_init(&m)) { fprintf(stderr, "ph_init falhou\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "nao abriu bios %s\n", argv[1]); return 1; }
    static uint8_t b[0x200000];
    size_t n = fread(b, 1, sizeof b, f); fclose(f);
    ph_load_bios(&m, b, n);
    m.clio.R[0x28 >> 2] = 1;   /* CSTAT=1, igual ao libretro.c */
    if (ph_cd_insert(&m, argv[2])) { fprintf(stderr, "nao abriu imagem %s\n", argv[2]); return 1; }

    o_fetch = m.cpu.fetch; o_wr32 = m.cpu.wr32;
    m.cpu.fetch = w_fetch; m.cpu.wr32 = w_wr32;

    int unchanged_windows = 0;
    for (int i = 0; i < it; i++) {
        ph_run(&m, 0x40000);

        if (m.cd.lba != last_lba) {
            if (lba_prints < 400)
                printf("lba -> %d (cyc=%u pc=%08x S=%02x)\n", m.cd.lba, m.total_cycles, m.cpu.r[15], m.cd.S);
            last_lba = m.cd.lba; lba_prints++;
            unchanged_windows = 0;
            stall_reported = 0;   /* rearma: queremos o histograma da TRAVA FINAL, nao da 1a pausa do boot */
        } else {
            unchanged_windows++;
        }

        if (!stall_reported && unchanged_windows >= stall_windows) {
            report_stall_entry();
            sampling = 1;
            sample_iters_left = sample_iters;
        } else if (sampling) {
            sample_iters_left--;
            if (sample_iters_left <= 0) { sampling = 0; print_histogram(); }
        }
    }
    if (sampling) print_histogram();   /* acabou antes da janela fechar: imprime o que tem */

    printf("\n--- fim (%d iteracoes) ---\n", it);
    printf("mctl_writes=%d lba_final=%d cyc_final=%u stall_detectado=%d\n",
           mctl_writes, m.cd.lba, m.total_cycles, stall_reported);
    return 0;
}
