/* machine.h - barramento + laco da CPU do Phoenix 3DO (ARM60 <-> RAM/BIOS/NVRAM/MADAM/CLIO).
 * Fonte: FUN_0049fea0 (read32), FUN_0049fda0 (write32), FUN_004a0030 (fetch), FUN_004a0130 (read8),
 * FUN_004a0530 (write8), FUN_004a0090/004a0340 (blocos: so aceleracao) e FUN_004a0920 (laco).
 * Ver PARTE11 (mapa) e PARTE12 (fetch/read8/write8/blocos decodificados por objdump direto no .exe,
 * sem Ghidra/REA -- fecha os 5 itens que a Parte 11 deixou em aberto por falta de decompilacao). */
#ifndef PH_MACHINE_H
#define PH_MACHINE_H
#include "arm60.h"
#include "clio.h"
#include "madam.h"
#include "sport.h"
#include "vdlp.h"
#include "celexec.h"
#include "cd.h"

#define PH_BADACCE5 0xbadacce5u
#define PH_BIOS_BYTES 0x200000u          /* 2 bancos de 1 MB (CLIO reg 0x84 bit 2 escolhe o banco em 0x03000000) */
#define PH_NVRAM_BYTES 0x8000u

#include "dsp.h"  /* para dsp_t */
#include "joybus.h" /* para joybus_t */

typedef struct ph_machine ph_machine_t;
struct ph_machine {
    arm60_t cpu;                 /* machine+0x48 */
    clio_t  clio;                /* machine+0x1000dc8 */
    madam_t madam;               /* machine+0x1d8 (Parte 13); usado quando o gancho madam_* correspondente e NULL */
    sport_t sport;               /* machine+0x10974f0 (Parte 13); VRAM = ram+2 MB; usado quando dev32_* e NULL */
    celexec_t cel;               /* motor de CEL (PARTE19): percorre a cadeia de CCBs e desenha na RAM */
    cd_t    cd;                  /* xbusMeiCdDrive (PARTE20): XBUS ID 0, leitor da imagem do disco */
    dsp_t   dsp;                 /* DSP dsp3DO (PARTE5): passo por amostra, memoria, reset */
    joybus_t joybus;             /* Joybus (controles) (PARTE5/13): PBUS DMA8, dev32 0x03200000 */
    xdev_t  cd_dev;              /* vtable xdev_t do CLIO apontando para cd_* (clio.dev[0]) */
    vdlp_t  vdlp;                /* clio+0x122d0 = machine+0x10ee0a0 (PARTE18); VDL na VRAM, 320x240 NTSC / 384x288 PAL */
    uint32_t *ram;               /* (dram+vram)/4 palavras (stock 0xC0000): valor = inteiro big-endian do 3DO (byte = idx^3) */
    uint32_t *bios;              /* 0x80000 palavras, idem */
    uint8_t  nvram[PH_NVRAM_BYTES];
    uint32_t stat_bad_rd, bad_rd_addr, stat_bad_wr, bad_wr_addr, stat_bad_fetch, bad_fetch_addr, stat_sport, stat_madam_mat;   /* AUDITORIA: acessos a regioes nao mapeadas */
    /* DIAGNOSTICO (PARTE19b): vigia a regiao do VDL (head..head+0x80); anel das ultimas 32 escritas com PC/LR */
    uint32_t wlo, whi, wn, wfreeze;
    struct { uint32_t pc, lr, a, v, sz; } wring[32];
    /* DIAGNOSTICO (PARTE19c): primeiras escritas do valor-lixo 0xd6d66728 / 0xcd5d0000 (origem das coordenadas absurdas) */
    uint32_t vn, vprinted;
    struct { uint32_t pc, lr, a, v, r0, r1, r2, r3; } vring[24];
    /* DIAGNOSTICO PARTE19h: vigia o contador de tempo do audio folio ([kb+0x9c]) */
    uint32_t tk_addr, tk_n, tk_cnt;
    struct { uint32_t pc, lr, old, v; } tkring[24];
    /* DIAGNOSTICO (PARTE19d): primeiras leituras de 32 bits em regiao nao mapeada (devolvem 0xbadacce5) */
    uint32_t bn, bprinted; uint32_t bcount;
    struct { uint32_t pc, lr, a, cnt; } bring[24];
    uint32_t slice;              /* machine+0x1297558: ciclos entre ticks do CLIO */
    uint32_t stall;              /* machine+0x2c: ciclos que o ARM fica parado pelo CEL */
    uint32_t total_cycles;       /* machine+0x28 */
    uint8_t  vram_flag;          /* machine+0x3c: se 0, a LEITURA de 32 bits na regiao 2 chama vram_touch */
    int32_t  clk_mult;           /* machine+0x40: ticks do CLIO = clk_mult*ciclos>>4 (16 = 1:1) */
    uint32_t clk_q16;            /* "CPU Overclock": ticks do CLIO por ciclo da CPU em ponto fixo 16.16 (65536/fator); 0 = desligado (vale clk_mult, como sempre) */
    uint32_t clk_acc;            /* resto fracionario (16 bits) do calculo acima, carregado de um passo para o outro */
    int      break_valid; uint32_t break_pc;   /* machine+0x159f560 vs PC: acerta => consome a fatia inteira */
    void *ud;
    /* "Memory Capacity" (host, fora do savestate): DRAM em 0, VRAM logo depois dela (como o Opera). Stock = 2 MB + 1 MB */
    uint32_t dram_bytes, vram_bytes, ram_bytes;
    /* HiRes CEL Rendering (host; tudo de `ud` ate o fim NAO entra no savestate): planos extras da VRAM (copias de 1 MB, mesmo leiaute de palavras) */
    uint32_t *hr[3];
    int      hires;
    /* ganchos que SUBSTITUEM os blocos internos (para testes/depuracao). NULL = usa madam.c / sport.c (Parte 13) */
    uint32_t (*madam_peek)(void *ud, uint32_t addr);               /* FUN_0048b2a0 */
    uint32_t (*madam_poke)(void *ud, uint32_t addr, uint32_t v);   /* FUN_0048adb0; retorno so vale p/ 0x03300100 */
    uint32_t (*dev32_read)(void *ud, uint32_t off);                /* FUN_004714d0 (0x03200000, 8 KB) */
    void     (*dev32_write)(void *ud, uint32_t off, uint32_t v);   /* FUN_004714e0 */
    int      (*cel_busy)(void *ud);                                /* *(madam+0x10000ec) == 0x10 */
    void     (*cel_run)(void *ud, uint32_t n);                     /* FUN_004894c0 */
    void     (*vram_touch)(void *ud);                              /* FUN_0058e9f0: so na leitura de 32 bits da regiao 2 e com vram_flag==0 (a escrita NAO chama) */
    void     (*slice_begin)(void *ud);                             /* vtable +0xd0 */
    void     (*slice_end)(void *ud);                               /* vtable +0xb0 / +0xd8 */
    /* Joybus (controles): o 3DO tem UM control port; os pads entram encadeados na unica
     * cadeia PBUS, lida pela DMA8 do MCTL (c_mctl). Nao ha registradores joybus na CPU. */
    void     (*joybus_set_callbacks)(void *j, retro_input_poll_t poll_cb, retro_input_state_t state_cb);
    void     (*joybus_poll)(void *j);                                /* retro_run: poll + refaz a cadeia */
    void     (*joybus_get_pbus_buffer)(void *j, uint32_t port, uint32_t *out); /* copia a cadeia (<= 16 bytes); devolve o nº valido */
};

int  ph_init(ph_machine_t *m);                  /* aloca RAM/BIOS (stock: 2 MB DRAM + 1 MB VRAM); slice=128, clk_mult=16 */
int  ph_init_mem(ph_machine_t *m, unsigned dram_mb, unsigned vram_mb);   /* idem com "Memory Capacity": DRAM 2/4/8/14/15 MB, VRAM 1/2 MB; -1 se invalido ou sem memoria */
int  ph_mem_cfg_valid(unsigned dram_mb, unsigned vram_mb);
void ph_free(ph_machine_t *m);
/* NVRAM de 32 KB: o BIOS espera um volume 3DO "linked mem" ja formatado (como o `format` oficial / opera_nvram_init). Em branco, mostra
 * "NVRAM Full: Configuration not saved!". formatted = tem rotulo de disco valido; format = apaga tudo e cria o volume vazio. */
int  ph_nvram_formatted(const uint8_t *nv);
void ph_nvram_format(uint8_t *nv);
int  ph_set_hires(ph_machine_t *m, int on);     /* "HiRes CEL Rendering": CELs de 3D em 2x + saida do VDLP em 2x (ver cel_opera.c); vale na hora; 0 = ok */
int  ph_cd_insert(ph_machine_t *m, const char *path);   /* carrega a imagem (ISO 2048 B/setor), monta o TOC e reinicia a unidade; 0 = ok */
void ph_cd_eject(ph_machine_t *m);                        /* tira o disco e reinicia a unidade */
int  ph_set_pal(ph_machine_t *m, int pal);      /* troca NTSC/PAL: clio.pal + refaz o VDLP (muda o tamanho da imagem); 0 = ok */
int  ph_set_region(ph_machine_t *m, int mode);  /* VDLP_MODE_NTSC / PAL2 (384x288) / PAL1 (320x288): clio.pal + refaz o VDLP; 0 = ok */
void ph_set_cpu_overclock(ph_machine_t *m, double factor);   /* 1.0 = 12,5 MHz (padrao); a CPU roda 'factor' vezes mais ciclos por tick do CLIO */
void ph_set_matrix_software(ph_machine_t *m, int software);  /* "MADAM Matrix Engine": 1 = o ID do MADAM anuncia matriz por software (bit 0), o SO usa as SWIs 0x5xxxx em ARM */
void ph_set_random_seed(ph_machine_t *m, uint32_t seed);     /* semente do gerador aleatorio do CLIO/DSP (LCG de 31 bits) */
void ph_load_font_rom(ph_machine_t *m, const uint8_t *img, size_t n);   /* FONT-ROM (kanji, 1 MB) no 2o banco da ROM (CLIO 0x84 bit 2); so se a BIOS carregada tem 1 MB */
void ph_load_bios(ph_machine_t *m, const uint8_t *img, size_t n);   /* imagem em ordem big-endian */
uint32_t ph_read32(ph_machine_t *m, uint32_t a);
uint32_t ph_fetch (ph_machine_t *m, uint32_t a);
uint8_t  ph_read8 (ph_machine_t *m, uint32_t a);
void     ph_write32(ph_machine_t *m, uint32_t a, uint32_t v);
void     ph_write8 (ph_machine_t *m, uint32_t a, uint8_t v);
/* FUN_004a0090/004a0340: ponteiros de host para bloco (so aceleracao de LDM/STM; devolvem NULL se o
 * bloco nao cabe inteiro numa unica regiao suportada). Nao usados pelo arm60.c hoje -- o LDM/STM atual
 * (palavra a palavra via ph_read32/ph_write32) e equivalente em efeito observavel, ver PARTE10/PARTE12. */
const uint32_t *ph_block_read_ptr (ph_machine_t *m, uint32_t a, uint32_t len);
uint32_t       *ph_block_write_ptr(ph_machine_t *m, uint32_t a, uint32_t len);
void     ph_soft_reset(ph_machine_t *m, uint32_t req);  /* CSTAT 0x10 (reset) / 0x20 (limpa DIPIR): reinicia so a CPU */
void     ph_run(ph_machine_t *m, int quantum);  /* FUN_004a0920 (quantum do .exe = 0x40000 ciclos) */
/* savestate: serializa TODA a maquina (CPU, CLIO, MADAM, SPORT, CEL, CD, DSP, VDLP, RAM 3 MB, NVRAM). O host (ponteiros/ganchos/arquivo do disco)
 * nao entra: ph_state_load religa tudo a partir da maquina atual, entao o MESMO disco/BIOS devem estar carregados. 0 = ok. */
size_t   ph_state_size(const ph_machine_t *m);
int      ph_state_save(const ph_machine_t *m, void *buf, size_t size);
int      ph_state_load(ph_machine_t *m, const void *buf, size_t size);
void     machine_set_debug_log(void (*cb)(int level, const char *fmt, ...));
#endif
