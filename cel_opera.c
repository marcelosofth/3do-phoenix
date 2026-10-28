/* cel_opera.c - motor de CEL (spryte engine) do MADAM, portado do Opera (opera-libretro, libopera/opera_madam.c) para o phoenix_libretro.
 *
 * ESTE ARQUIVO E GERADO por gen_cel_opera.py a partir do opera_madam.c: o trecho do Opera (leitor de CCB, PDEC, PPROC, PPROJ,
 * rasterizadores Line/Scale/Arbitrary, empacotado/literal/LRFORM) vem copiado SEM edicao manual. So o prologo (memoria, flags,
 * leitor de bits) e o epilogo (cola com madam.c/celexec.c do Phoenix) sao escritos aqui. Nao edite o meio a mao: regenere.
 *
 * Por que: validar o motor por jogo contra o phoenix.exe nao converge; o motor do Opera e validado contra o hardware/biblioteca 3DO.
 * Layout de RAM identico ao do Phoenix (palavra de 32 bits em ordem do host, byte = addr^3, meia-palavra = addr^2; RAM 3 MB = DRAM 2 MB + VRAM 1 MB).
 *
 * Licenca do trecho do Opera: "modified GNU LGPL" do FreeDO (www.freedo.org) - uso nao comercial com creditos:
 *   Alexander Troosh, Maxim Grishin, Allen Wright, John Sammons, Felix Lazarev (e os mantenedores do opera-libretro).
 */
#include "cel_opera.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef INLINE
#define INLINE inline
#endif
#ifdef _MSC_VER
#define OPERA_FORCEINLINE __forceinline
#elif defined(__GNUC__)
#define OPERA_FORCEINLINE __attribute__((always_inline)) INLINE
#else
#define OPERA_FORCEINLINE INLINE
#endif
/* o trecho do Opera e de terceiros e nao segue o -Wextra do resto do projeto */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#pragma GCC diagnostic ignored "-Wold-style-declaration"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

#define set_flag(V,F) ((V)|=(F))
#define clr_flag(V,F) ((V)&=~(F))
#define flag_is_set(V,F) (!!((V)&(F)))
#define flag_is_clr(V,F) (!((V)&(F)))

#define FSM_IDLE 1
#define FSM_INPROCESS 2
#define FSM_SUSPENDED 3
#define MADAM_MCTL_CLUTXEN 0x00002000
#define MADAM_MCTL_VSCTXEN 0x00004000
#define MADAM_MCTL_PLAYXEN 0x00008000

/* ---- memoria: aponta para a RAM do Phoenix (3 MB). Acessos fora dela leem 0 / sao ignorados (o Opera nao confere; um ponteiro de
 *      CEL lixo derrubaria o host). ---- */
/* HiRes CEL Rendering (opcao phoenix_high_resolution). RETOQUE DO PHOENIX: no Opera HIRESMODE e constante; aqui e variavel de
 * execucao (0/1), ligada por opera_cel_set_hires() so quando o destino da cadeia (REGCTL3) esta na VRAM.
 * Layout (igual ao do Opera): o pixel (x,y) do framebuffer 2x vive no "plano" q = ((y&1)<<1)|(x&1) na posicao (x>>1,y>>1).
 * Plano 0 = a propria VRAM (o que a CPU/VDLP/jogo enxergam); planos 1..3 = HRP[0..2], copias de 1 MB da VRAM mantidas pelo
 * Phoenix (machine.c/sport.c replicam as escritas que nao vem do motor de CEL). */
static int HIRESMODE;
static uint32_t *HRP[3];
static uint32_t g_vram_base = 0x200000u, g_vram_size = 0x100000u, g_ram_size = CEL_OPERA_RAM_BYTES;   /* "Memory Capacity": VRAM fica logo depois da DRAM */
#define VRAM_BASE g_vram_base
#define VRAM_SIZE g_vram_size
#define RAM_SIZE  g_ram_size
void opera_cel_set_mem(uint32_t dram_bytes, uint32_t vram_bytes) { g_vram_base = dram_bytes; g_vram_size = vram_bytes; g_ram_size = dram_bytes + vram_bytes; }
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define IS_BIG_ENDIAN 1
#else
#define IS_BIG_ENDIAN 0
#endif
static uint8_t *DRAM;

static INLINE uint8_t opera_mem_read8(uint32_t a) {
  if (a >= RAM_SIZE) return 0;
#if IS_BIG_ENDIAN
  return DRAM[a];
#else
  return DRAM[a ^ 3];
#endif
}
static INLINE uint16_t opera_mem_read16(uint32_t a) {
  uint16_t v;
  if (a >= RAM_SIZE) return 0;
#if !IS_BIG_ENDIAN
  a ^= 2;
#endif
  memcpy(&v, &DRAM[a], 2);
  return v;
}
static INLINE uint32_t opera_mem_read32(uint32_t a) {
  uint32_t v;
  if (a > RAM_SIZE - 4u) return 0;
  memcpy(&v, &DRAM[a], 4);
  return v;
}
static INLINE void opera_mem_write16_base(uint32_t a, uint16_t v) {
  if (a >= RAM_SIZE) return;
#if !IS_BIG_ENDIAN
  a ^= 2;
#endif
  memcpy(&DRAM[a], &v, 2);
}
/* acesso de 16 bits a um plano extra (offset dentro da VRAM; mesmo leiaute de palavra do resto da RAM) */
static INLINE int hr_off(uint32_t a, uint32_t *o) {
  if (a < VRAM_BASE || a - VRAM_BASE >= VRAM_SIZE) return 0;
  *o = a - VRAM_BASE;
  return 1;
}
static INLINE void hr_write16(int q, uint32_t o, uint16_t v) {
#if !IS_BIG_ENDIAN
  o ^= 2;
#endif
  memcpy((uint8_t *)HRP[q - 1] + o, &v, 2);
}
static INLINE uint16_t hr_read16(int q, uint32_t o) {
  uint16_t v;
#if !IS_BIG_ENDIAN
  o ^= 2;
#endif
  memcpy(&v, (const uint8_t *)HRP[q - 1] + o, 2);
  return v;
}
/* escrita de pixel 1x (CELs de linha/escala): em modo hires o pixel vale para as 4 subamostras */
static void watch_1x(uint32_t a, uint16_t v);
static INLINE void opera_mem_write16(uint32_t a, uint16_t v) {
  watch_1x(a, v);
  opera_mem_write16_base(a, v);
  if (HIRESMODE) {
    uint32_t o;
    if (hr_off(a, &o)) { hr_write16(1, o, v); hr_write16(2, o, v); hr_write16(3, o, v); }
  }
}

/* ---- leitor de bits (opera_bitop.c) ---- */
struct BitReaderBig { uint32_t buf; uint32_t point; int32_t bitpoint; int32_t bitset; };
static void BitReaderBig_AttachBuffer(struct BitReaderBig *bit, uint32_t buff)
{
  bit->buf      = buff;
  bit->point    = 0;
  bit->bitpoint = 0;
}

static void BitReaderBig_Skip(struct BitReaderBig *bit, uint32_t bits)
{
  bits         += bit->bitpoint;
  bit->point   += (bits >> 3);
  bit->bitpoint = bits & 7;
}

static void BitReaderBig_SetBitRate(struct BitReaderBig *bit, uint8_t bits)
{
  bit->bitset = bits;
  if(bit->bitset > 32)
    bit->bitset = 32;
  if(!bit->bitset)
    bit->bitset = 1;
};

static uint32_t BitReaderBig_Read(struct BitReaderBig *bit, uint8_t bits)
{
  int32_t bitcnt;
  const static uint8_t mas[] = {0,1,3,7,15,31,63,127,255};
  uint32_t retval = 0;
  BitReaderBig_SetBitRate(bit, bits);

  bitcnt  = bit->bitset;
  if(!bit->buf)
    return retval;

  if((8 - bit->bitpoint) > bit->bitset)
    {
      retval    = opera_mem_read8(bit->buf + bit->point);
      retval  >>= 8 - bit->bitpoint - bit->bitset;
      retval   &= mas[bit->bitset];
      bit->bitpoint += bit->bitset;
      return retval;
    }

  if (bit->bitpoint)
    {
      retval = opera_mem_read8(bit->buf + bit->point) & mas[8 - bit->bitpoint];
      bit->point++;
      bitcnt -= 8 - bit->bitpoint;
    }

  while(bitcnt>=8)
    {
      retval <<= 8;
      retval  |= opera_mem_read8(bit->buf + bit->point);
      bit->point++;
      bitcnt-=8;
    }

  if(bitcnt)
    {
      retval <<= bitcnt;
      retval |= opera_mem_read8(bit->buf + bit->point) >> (8 - bitcnt);
    }

  bit->bitpoint = bitcnt;

  return retval;
}

static struct BitReaderBig bitoper;


/* ===== opera_madam.c: tipos, constantes e registradores do CEL (linhas 52-483) ===== */

#define MakeRGB15(r,g,b)  (((uint32_t)(r)<<10)|((uint32_t)(g)<<5)|(uint32_t)(b))
#define RGB15_BLACK       MakeRGB15(0,0,0)
#define RGB15_NEAR_BLACK  MakeRGB15(1,0,0)
#define RGB15_MAGENTA     MakeRGB15(0x1b,0,0x1b)
#define IS_RGB15_BLACK(P) (((P)&0x7FFF) == RGB15_BLACK)

/* === CCB control word flags === */
#define CCB_SKIP        0x80000000
#define CCB_LAST        0x40000000
#define CCB_NPABS       0x20000000
#define CCB_SPABS       0x10000000
#define CCB_PPABS       0x08000000
#define CCB_LDSIZE      0x04000000
#define CCB_LDPRS       0x02000000
#define CCB_LDPPMP      0x01000000
#define CCB_LDPLUT      0x00800000
#define CCB_CCBPRE      0x00400000
#define CCB_YOXY        0x00200000
#define CCB_ACSC        0x00100000
#define CCB_ALSC        0x00080000
#define CCB_ACW         0x00040000
#define CCB_ACCW        0x00020000
#define CCB_TWD         0x00010000
#define CCB_LCE         0x00008000
#define CCB_ACE         0x00004000
#define CCB_reserved13  0x00002000
#define CCB_MARIA       0x00001000
#define CCB_PXOR        0x00000800
#define CCB_USEAV       0x00000400
#define CCB_PACKED      0x00000200
#define CCB_POVER_MASK  0x00000180
#define CCB_PLUTPOS     0x00000040
#define CCB_BGND        0x00000020
#define CCB_NOBLK       0x00000010
#define CCB_PLUTA_MASK  0x0000000F

#define CCB_POVER_SHIFT  7
#define CCB_PLUTA_SHIFT  0

#define PMODE_PDC   ((0x00000000)<<CCB_POVER_SHIFT) /* Normal */
#define PMODE_ZERO  ((0x00000002)<<CCB_POVER_SHIFT)
#define PMODE_ONE   ((0x00000003)<<CCB_POVER_SHIFT)

/* === CECONTROL flags === */
#define B15POS_MASK   0xC0000000
#define B0POS_MASK    0x30000000
#define SWAPHV        0x08000000
#define ASCALL        0x04000000
#define CECONTROL_u25 0x02000000
#define CFBDSUB       0x01000000
#define CFBDLSB_MASK  0x00C00000
#define PDCLSB_MASK   0x00300000

#define B15POS_SHIFT  30
#define B0POS_SHIFT   28
#define CFBD_SHIFT    22
#define PDCLSB_SHIFT  20

/* B15POS_MASK definitions */
#define B15POS_0    0x00000000
#define B15POS_1    0x40000000
#define B15POS_PDC  0xC0000000

/* B0POS_MASK definitions */
#define B0POS_0     0x00000000
#define B0POS_1     0x10000000
#define B0POS_PPMP  0x20000000
#define B0POS_PIXC  B0POS_PPMP
#define B0POS_PDC   0x30000000

/* CFBDLSB_MASK definitions */
#define CFBDLSB_0      0x00000000
#define CFBDLSB_CFBD0  0x00400000
#define CFBDLSB_CFBD4  0x00800000
#define CFBDLSB_CFBD5  0x00C00000

/* PDCLSB_MASK definitions */
#define PDCLSB_0     0x00000000
#define PDCLSB_PDC0  0x00100000
#define PDCLSB_PDC4  0x00200000
#define PDCLSB_PDC5  0x00300000

/* === Cel first preamble word flags === */
#define PRE0_LITERAL    0x80000000
#define PRE0_BGND       0x40000000
#define PREO_reservedA  0x30000000
#define PRE0_SKIPX_MASK 0x0F000000
#define PREO_reservedB  0x00FF0000
#define PRE0_VCNT_MASK  0x0000FFC0
#define PREO_reservedC  0x00000020
#define PRE0_LINEAR     0x00000010
#define PRE0_REP8       0x00000008
#define PRE0_BPP_MASK   0x00000007

#define PRE0_SKIPX_SHIFT 24
#define PRE0_VCNT_SHIFT  6
#define PRE0_BPP_SHIFT   0

/* PRE0_BPP_MASK definitions */
#define PRE0_BPP_RESERVED_0 0x00000000
#define PRE0_BPP_1          0x00000001
#define PRE0_BPP_2          0x00000002
#define PRE0_BPP_4          0x00000003
#define PRE0_BPP_6          0x00000004
#define PRE0_BPP_8          0x00000005
#define PRE0_BPP_16         0x00000006
#define PRE0_BPP_RESERVED_7 0x00000007

#define PRE0_SKIPX(pre0_) (((pre0_) & PRE0_SKIPX_MASK) >> PRE0_SKIPX_SHIFT)

/* Subtract this value from the actual vertical source line count */
#define PRE0_VCNT_PREFETCH    1

/* === Cel second preamble word flags === */
#define PRE1_WOFFSET8_MASK   0xFF000000
#define PRE1_WOFFSET10_MASK  0x03FF0000
#define PRE1_NOSWAP          0x00004000
#define PRE1_TLLSB_MASK      0x00003000
#define PRE1_LRFORM          0x00000800
#define PRE1_TLHPCNT_MASK    0x000007FF

#define PRE1_WOFFSET8_SHIFT   24
#define PRE1_WOFFSET10_SHIFT  16
#define PRE1_TLLSB_SHIFT      12
#define PRE1_TLHPCNT_SHIFT    0

#define PRE1_TLLSB_0     0x00000000
#define PRE1_TLLSB_PDC0  0x00001000 /* Normal */
#define PRE1_TLLSB_PDC4  0x00002000
#define PRE1_TLLSB_PDC5  0x00003000

/* Subtract this value from the actual word offset */
#define PRE1_WOFFSET_PREFETCH 2
/* Subtract this value from the actual pixel count */
#define PRE1_TLHPCNT_PREFETCH 1

/* === Packed cel data control tokens === */
#define PACK_EOL          0x00000000
#define PACK_LITERAL      0x00000001
#define PACK_TRANSPARENT  0x00000002
#define PACK_PACKED       0x00000003

#define PPMP_0_SHIFT 0
#define PPMP_1_SHIFT 16

#define PPMPC_1S_MASK  0x00008000
#define PPMPC_MS_MASK  0x00006000
#define PPMPC_MF_MASK  0x00001C00
#define PPMPC_SF_MASK  0x00000300
#define PPMPC_2S_MASK  0x000000C0
#define PPMPC_AV_MASK  0x0000003E
#define PPMPC_2D_MASK  0x00000001

#define PPMPC_MS_SHIFT  13
#define PPMPC_MF_SHIFT  10
#define PPMPC_SF_SHIFT  8
#define PPMPC_2S_SHIFT  6
#define PPMPC_AV_SHIFT  1

/* PPMPC_1S_MASK definitions */
#define PPMPC_1S_PDC   0x00000000
#define PPMPC_1S_CFBD  0x00008000

/* PPMPC_MS_MASK definitions */
#define PPMPC_MS_CCB         0x00000000
#define PPMPC_MS_PIN         0x00002000
#define PPMPC_MS_PDC         0x00004000
#define PPMPC_MS_PDC_MFONLY  0x00006000

/* PPMPC_MF_MASK definitions */
#define PPMPC_MF_1  0x00000000
#define PPMPC_MF_2  0x00000400
#define PPMPC_MF_3  0x00000800
#define PPMPC_MF_4  0x00000C00
#define PPMPC_MF_5  0x00001000
#define PPMPC_MF_6  0x00001400
#define PPMPC_MF_7  0x00001800
#define PPMPC_MF_8  0x00001C00

/* PPMPC_SF_MASK definitions */
#define PPMPC_SF_2   0x00000100
#define PPMPC_SF_4   0x00000200
#define PPMPC_SF_8   0x00000300
#define PPMPC_SF_16  0x00000000

/* PPMPC_2S_MASK definitions */
#define PPMPC_2S_0     0x00000000
#define PPMPC_2S_CCB   0x00000040
#define PPMPC_2S_CFBD  0x00000080
#define PPMPC_2S_PDC   0x000000C0

/* PPMPC_2D_MASK definitions */
#define PPMPC_2D_1  0x00000000
#define PPMPC_2D_2  0x00000001

#define ALIGNED_24BIT_MASK (0x00FFFFFFu & ~3u)

#pragma pack(push,1)

struct cp1btag_s
{
  uint16_t c:1;
  uint16_t pad:15;
};

typedef struct cp1btag_s cp1btag_t;

struct cp2btag_s
{
  uint16_t c:2;
  uint16_t pad:14;
};

typedef struct cp2btag_s cp2btag_t;

struct cp4btag_s
{
  uint16_t c:4;
  uint16_t pad:12;
};

typedef struct cp4btag_s cp4btag_t;

struct cp6btag_s
{
  uint16_t c:5;
  uint16_t pw:1;
  uint16_t pad:10;
};

typedef struct cp6btag_s cp6btag_t;

struct cp8btag_s
{
  uint16_t c:5;
  uint16_t mpw:1;
  uint16_t m:2;
  uint16_t pad:8;
};

typedef struct cp8btag_s cp8btag_t;

struct cp16btag_s
{
  uint16_t c:5;
  uint16_t mb:3;
  uint16_t mg:3;
  uint16_t mr:3;
  uint16_t pad:1;
  uint16_t pw:1;
};

typedef struct cp16btag_s cp16btag_t;

struct up8btag_s
{
  uint16_t b:2;
  uint16_t g:3;
  uint16_t r:3;
  uint16_t pad:8;
};

typedef struct up8btag_s up8btag_t;

struct up16btag_s
{
  uint16_t bw:1;
  uint16_t b:4;
  uint16_t g:5;
  uint16_t r:5;
  uint16_t p:1;
};

typedef struct up16btag_s up16btag_t;

struct res16btag_s
{
  uint16_t b:5;
  uint16_t g:5;
  uint16_t r:5;
  uint16_t p:1;
};

typedef struct res16btag_s res16btag_t;

union pdeco_u
{
  uint32_t    raw;
  cp1btag_t   c1b;
  cp2btag_t   c2b;
  cp4btag_t   c4b;
  cp6btag_t   c6b;
  cp8btag_t   c8b;
  cp16btag_t  c16b;
  up8btag_t   u8b;
  up16btag_t  u16b;
  res16btag_t r16b;
};

typedef union pdeco_u pdeco_t;

struct avtag_s
{
  uint8_t NEG:1;
  uint8_t XTEND:1;
  uint8_t nCLIP:1;
  uint8_t dv3:2;
  uint8_t pad:3;
};

typedef struct avtag_s avtag_t;

union AVS_u
{
  avtag_t  avsignal;
  uint32_t raw;
};

typedef union AVS_u AVS_t;

struct pixctag_s
{
  uint8_t dv2:1;
  uint8_t av:5;
  uint8_t s2:2;
  uint8_t dv1:2;
  uint8_t mxf:3;
  uint8_t ms:2;
  uint8_t s1:1;
};

typedef struct pixctag_s pixctag_t;

union PXC_u
{
  pixctag_t meaning;
  uint32_t  raw;
};

typedef union PXC_u PXC_t;

#define MADAM_REGISTER_COUNT        2048
#define MADAM_REGISTER_EXTRA_COUNT  64
#define MADAM_REGISTER_SAVE_COUNT   (MADAM_REGISTER_COUNT + MADAM_REGISTER_EXTRA_COUNT)
#define MADAM_PLUT_COUNT            32

#define MADAM_REG_ID          0x000
#define MADAM_REG_MSYSBITS    0x004
#define MADAM_REG_MCTL        0x008
#define MADAM_REG_STATBITS    0x028

#define MADAM_REG_CLIO_FIFO_FIRST  0x400
#define MADAM_REG_CLIO_FIFO_LAST   0x53F

#define MADAM_REG_DMA8_DEST      0x570
#define MADAM_REG_DMA8_LEN       0x574
#define MADAM_REG_DMA8_PROGRESS  0x578
#define MADAM_DMA8_IDLE_LEN      0xFFFFFFFC

#define MADAM_REG_VDL_HEAD  0x580

#define MADAM_REG_SPRSTRT   0x100
#define MADAM_REG_SPRSTOP   0x104
#define MADAM_REG_SPRCNTU   0x108
#define MADAM_REG_SPRPAUS   0x10C

#define MADAM_REG_CECONTROL  0x110
#define MADAM_REG_REGCTL0    0x130
#define MADAM_REG_REGCTL1    0x134
#define MADAM_REG_REGCTL2    0x138
#define MADAM_REG_REGCTL3    0x13C
#define MADAM_REG_XYPOSH     0x140
#define MADAM_REG_XYPOSL     0x144
#define MADAM_REG_LINEDXYH   0x148
#define MADAM_REG_LINEDXYL   0x14C
#define MADAM_REG_DXYH       0x150
#define MADAM_REG_DXYL       0x154
#define MADAM_REG_DDXYL      0x158
#define MADAM_REG_DDXYH      0x15C

#define MADAM_REG_CURRENTCCB  0x5A0
#define MADAM_REG_NEXTCCB     0x5A4
#define MADAM_REG_PLUTDATA    0x5A8
#define MADAM_REG_PDATA       0x5AC
#define MADAM_REG_ENGAFETCH   0x5B0
#define MADAM_REG_ENGALEN     0x5B4
#define MADAM_REG_ENGBFETCH   0x5B8
#define MADAM_REG_ENGBLEN     0x5BC

#define MADAM_REG_MATRIX_INPUT     0x600
#define MADAM_REG_MATRIX_VECTOR    0x640
#define MADAM_REG_MATRIX_OUTPUT    0x660
#define MADAM_REG_MATRIX_NFRAC_HI  0x680
#define MADAM_REG_MATRIX_NFRAC_LO  0x684
#define MADAM_REG_MATRIX_CONTROL   0x7FC

#define MADAM_MATRIX_ROW_STRIDE      0x10
#define MADAM_MATRIX_REGISTER_STRIDE 0x04
#define MADAM_MATRIX_INPUT_REG(ROW,COL)                         \
  (MADAM_REG_MATRIX_INPUT + ((ROW) * MADAM_MATRIX_ROW_STRIDE) + \
   ((COL) * MADAM_MATRIX_REGISTER_STRIDE))
#define MADAM_MATRIX_VECTOR_REG(IDX)                                    \
  (MADAM_REG_MATRIX_VECTOR + ((IDX) * MADAM_MATRIX_REGISTER_STRIDE))
#define MADAM_MATRIX_OUTPUT_REG(IDX)                                    \
  (MADAM_REG_MATRIX_OUTPUT + ((IDX) * MADAM_MATRIX_REGISTER_STRIDE))

#define MADAM_SYSBITS_RED 0x20

#pragma pack(pop)

struct madam_s
{
  uint32_t mregs[MADAM_REGISTER_SAVE_COUNT];
  uint16_t PLUT[MADAM_PLUT_COUNT];
  int32_t  rmod;
  int32_t  wmod;
  int32_t  clipx;
  int32_t  clipy;
  uint32_t FSM;
};

typedef struct madam_s madam_t;

#define ME_MODE_HARDWARE 0
#define ME_MODE_SOFTWARE 1
#define MADAM_ID_RED_HARDWARE   0x01000000
#define MADAM_ID_GREEN_HARDWARE 0x01020000
#define MADAM_GREENWW           0x01020001
#define MADAM_ID_GREEN_SOFTWARE MADAM_GREENWW

static madam_t MADAM;
/* ===== opera_madam.c: helpers e globais (linhas 621-633, 636-763) ===== */
#define PACKED   true
#define UNPACKED false

static bool    TestInitVisual(bool const packed);
static bool    InitLineMap(void);
static void    InitScaleMap(void);
static void    InitArbitraryMap(void);
static void    TexelDrawLine(uint16_t CURPIX, uint16_t LAMV, int32_t xcur, int32_t ycur, int32_t cnt);
static int32_t TexelDrawScale(uint16_t CURPIX, uint16_t LAMV, int32_t xcur, int32_t ycur, int32_t deltax, int32_t deltay);
static int32_t TexelDrawArbitrary(uint16_t CURPIX, uint16_t LAMV, int32_t xA, int32_t yA, int32_t xB, int32_t yB, int32_t xC, int32_t yC, int32_t xD, int32_t yD);
static void    DrawPackedCel(void);
static void    DrawLiteralCel(void);
static void    DrawLRCel(void);

static struct
{
  uint32_t plutaCCBbits;
  uint32_t pixelBitsMask;
  uint32_t rawVHBits;
} pdec;

static struct
{
  uint32_t pmode;
  uint32_t pmodeORmask;
  uint32_t pmodeANDmask;
  int      Transparent;
} pproj;

static uint32_t  CCBFLAGS;
static uint32_t  PIXC;
static uint32_t  PRE0;
static uint32_t  PRE1;
static int32_t   SPRWI;
static int32_t   SPRHI;
static int g_row_top;   /* 1 = o texel atual esta na fileira do CEL que fica no topo da tela (ver ROW_TOP) */
#define ROW_TOP(i) (VDY1616 >= 0 ? ((i) == 0) : ((i) == SPRHI - 1))

static uint32_t  PXOR1;
static uint32_t  PXOR2;

static uint32_t const BPP[8] = {1,1,2,4,6,8,16,1};

static uint8_t PSCALAR[8][4][32];

static uint16_t MAPu8b[256+64];
static uint16_t MAPc8bAMV[256+64];
static uint16_t MAPc16bAMV[8*8*8+64];

/*
  See section "Spryte Rendering Process" of patent
  WO09410642A1 - Method For Controlling A Spryte Rendering Processor
*/

// CelEngine STATBits
#define STATBITS	MADAM.mregs[MADAM_REG_STATBITS]

#define SPRON		0x10
#define SPRPAU		0x20

// CelEngine Registers
#define CECONTROL	MADAM.mregs[MADAM_REG_CECONTROL]
#define REGCTL0		MADAM.mregs[MADAM_REG_REGCTL0]
#define REGCTL1		MADAM.mregs[MADAM_REG_REGCTL1]
#define REGCTL2		MADAM.mregs[MADAM_REG_REGCTL2]
#define REGCTL3		MADAM.mregs[MADAM_REG_REGCTL3]
#define XYPOSH          MADAM.mregs[MADAM_REG_XYPOSH]
#define XYPOSL          MADAM.mregs[MADAM_REG_XYPOSL]
#define LINEDXYH        MADAM.mregs[MADAM_REG_LINEDXYH]
#define LINEDXYL        MADAM.mregs[MADAM_REG_LINEDXYL]
#define DXYH            MADAM.mregs[MADAM_REG_DXYH]
#define DXYL            MADAM.mregs[MADAM_REG_DXYL]
#define DDXYH           MADAM.mregs[MADAM_REG_DDXYH]
#define DDXYL           MADAM.mregs[MADAM_REG_DDXYL]

#define CURRENTCCB	MADAM.mregs[MADAM_REG_CURRENTCCB]
#define NEXTCCB		MADAM.mregs[MADAM_REG_NEXTCCB]
#define PLUTDATA	MADAM.mregs[MADAM_REG_PLUTDATA]
#define PDATA		MADAM.mregs[MADAM_REG_PDATA]
#define ENGAFETCH	MADAM.mregs[MADAM_REG_ENGAFETCH]
#define ENGALEN		MADAM.mregs[MADAM_REG_ENGALEN]
#define	ENGBFETCH	MADAM.mregs[MADAM_REG_ENGBFETCH]
#define ENGBLEN		MADAM.mregs[MADAM_REG_ENGBLEN]

static
INLINE
const
int32_t
PDV(const int32_t x_)
{
  return (((x_ - 1) & 3) + 1);
}

static
INLINE
int32_t
DecodeREGCTLMod(uint32_t val_)
{
  int32_t mod;

  /* REGCTL MOD bits encode pixel-width groups; XY2OFF needs byte stride. */
  mod = 0;
  if(val_ & 0x01)
    mod += (32 << 2);
  if(val_ & 0x02)
    mod += (512 << 2);
  if(val_ & 0x04)
    mod += (256 << 2);
  if(val_ & 0x08)
    mod += (1024 << 2);
  if(val_ & 0x10)
    mod += (64 << 2);
  if(val_ & 0x20)
    mod += (128 << 2);
  if(val_ & 0x40)
    mod += (256 << 2);
  if(val_ & 0x80)
    mod += (1024 << 2);

  return mod;
}

static
INLINE
const
int32_t
XY2OFF(const int32_t x_,
       const int32_t y_,
       const int32_t m_)
{
  return (((y_ >> 1) * m_) + ((y_ & 1) << 1) + (x_ << 2));
}

static
INLINE
const
bool
clipping(const int32_t x_,
         const int32_t y_)
{
  return (((uint32_t)x_ > MADAM.clipx) ||
          ((uint32_t)y_ > MADAM.clipy));
}

/* ===== opera_madam.c: estado do CEL, XYPOS, PLUT (linhas 1006-1075) ===== */
enum texel_draw_func_e
  {
    TEXEL_DRAW_FUNC_LINE,
    TEXEL_DRAW_FUNC_SCALE,
    TEXEL_DRAW_FUNC_ARBITRARY
  };
typedef enum texel_draw_func_e texel_draw_func_e;

static int32_t  HDDX1616;
static int32_t  HDDY1616;
static int32_t  HDX1616;
static int32_t  HDY1616;
static int32_t  VDX1616;
static int32_t  VDY1616;
static int32_t  XPOS1616;
static int32_t  YPOS1616;
static uint32_t CEL_ORIGIN_VH_VALUE;
static texel_draw_func_e TEXEL_DRAW_FUNC;
static int32_t  TEXTURE_WI_START;
static int32_t  TEXTURE_HI_START;
static int32_t  TEXEL_INCX;
static int32_t  TEXEL_INCY;
static int32_t  TEXTURE_WI_LIM;
static int32_t  TEXTURE_HI_LIM;

static
INLINE
void
LoadXYPosFromRegisters(void)
{
  XPOS1616 = (int32_t)((XYPOSH & 0xFFFF0000) | ((XYPOSL >> 16) & 0xFFFF));
  YPOS1616 = (int32_t)(((XYPOSH & 0x0000FFFF) << 16) | (XYPOSL & 0xFFFF));
}

static
INLINE
void
StoreXYPosToRegisters(int32_t const x_,
                      int32_t const y_)
{
  XYPOSH   = (((uint32_t)x_ & 0xFFFF0000) | (((uint32_t)y_ >> 16) & 0xFFFF));
  XYPOSL   = ((((uint32_t)x_ & 0xFFFF) << 16) | ((uint32_t)y_ & 0xFFFF));
  XPOS1616 = x_;
  YPOS1616 = y_;
}

static
INLINE
void
StoreXYPosFromDrawHeight(int32_t const x_,
                         int32_t const y_,
                         int32_t const vdx_,
                         int32_t const vdy_,
                         int32_t const height_)
{
  StoreXYPosToRegisters((int32_t)((uint32_t)x_ + ((uint32_t)vdx_ * (uint32_t)height_)),
                        (int32_t)((uint32_t)y_ + ((uint32_t)vdy_ * (uint32_t)height_)));
}

static
void
LoadPLUT(uint32_t pnt_,
         int32_t  n_)
{
  int i;

  for(i = 0; i < n_; i++)
    MADAM.PLUT[i] = opera_mem_read16((((pnt_ >> 1) + i)) << 1);
}

static void cel_opera_stat(void);
static uint32_t CEL_GUARD;
#ifndef CEL_GUARD_MAX
#define CEL_GUARD_MAX 0x10000u   /* CCBs por cadeia (uma cadeia real tem poucas centenas) */
#endif
/* ===== opera_madam.c: opera_madam_cel_handle (linhas 1076-1296) ===== */
static void
cel_opera_handle(void)
{
  CCBFLAGS = 0;
  set_flag(STATBITS,SPRON);

  /*
    When the CPU writes to the SPRSTRT address, after
    the spryte engine gains control of the system data bus
    118, 120, the DMA engine of Figs. 3 and 4 loads in the
    first six words from the first SCoB beginning from the
    system memory address specified in the NEXT SCOB
    register in the DMA stack 312.
  */
  CEL_GUARD = 0;
  while(NEXTCCB && flag_is_clr(CCBFLAGS,CCB_LAST) && (++CEL_GUARD < CEL_GUARD_MAX))
    {
      /*
        To accomplish this, the address of the first word to load is
        read out of the NEXT SCOB register and provided to the memory
        address lines via source multiplexer 314. The address is also
        incremented by adder/clipper 320 and written back via
        multiplexer 310 into the CURRENT SCOB register in DMA stack
        312. All six words are read from memory in this manner, the
        CURRENT SCOB register maintaining the address of each next
        word to load.
      */
      CURRENTCCB = (NEXTCCB & ALIGNED_24BIT_MASK);
      if(CURRENTCCB >= RAM_SIZE)
        break;

      /*
        The first SCoB word read, FLAGS, is written into a 32-bit
        hardware register in address manipulator chip 106.
      */
      CCBFLAGS = opera_mem_read32(CURRENTCCB);
      CURRENTCCB += 4;

      /*
        The next SCoB word read, NEXTPTR, is written into the NEXT
        SCOB register in DMA stack 312.
      */
      NEXTCCB = (opera_mem_read32(CURRENTCCB) & ALIGNED_24BIT_MASK);
      if(flag_is_clr(CCBFLAGS,CCB_NPABS))
        NEXTCCB += (CURRENTCCB + 4);
      CURRENTCCB += 4;

      /*
        SOURCEPTR is written into the SPRYTE DATA ADDRESS register of
        DMA stack 312,
      */
      PDATA = (opera_mem_read32(CURRENTCCB) & ALIGNED_24BIT_MASK);
      if(flag_is_clr(CCBFLAGS,CCB_SPABS))
        PDATA += (CURRENTCCB + 4);
      CURRENTCCB += 4;

      /*
        and PIPPTR is written into the PIP ADDRESS register in DMA
        stack 312.
      */
      PLUTDATA = (opera_mem_read32(CURRENTCCB) & ALIGNED_24BIT_MASK);
      if(flag_is_clr(CCBFLAGS,CCB_PPABS))
        PLUTDATA += (CURRENTCCB + 4);
      CURRENTCCB += 4;

      /*
        XPOS and YPOS are written to two memory mapped hardware
        registers XYPOSH and XYPOSL, each having the format of
        x,y. That is, the high-order 16 bits from XPOS and the
        high-order 16 bits from YPOS are written to the high- and
        low-order half words, respectively, of XYPOSH, and the
        low-order 16 bits of XPOS and the low-order 16 bits of YPOS
        are written to the high and low half words, respectively, of
        XYPOSL.

        Note that if the SKIP bit of the FLAGS word equals one,
        indicating that the present SCoB is to he skipped, or if the
        YOXY bit is zero, then the X and Y values are not written to
        the hardware.
      */
      if(flag_is_clr(CCBFLAGS,CCB_SKIP))
        {
          if(flag_is_set(CCBFLAGS,CCB_YOXY))
            StoreXYPosToRegisters(opera_mem_read32(CURRENTCCB + 0),
                                  opera_mem_read32(CURRENTCCB + 4));
          else
            LoadXYPosFromRegisters();
        }

      CURRENTCCB += 8;

      if(flag_is_set(CCBFLAGS,CCB_PXOR))
        {
          PXOR1 = 0;
          PXOR2 = 0x1F1F1F1F;
        }
      else
        {
          PXOR1 = 0xFFFFFFFF;
          PXOR2 = 0;
        }

      /*
        Get the VH value for this cel. This is done in case the
        cel later decides to use the position as the source of
        its VH values in the projector.
      */
      CEL_ORIGIN_VH_VALUE = ((XPOS1616 & 0x1) | ((YPOS1616 & 0x1) << 15));

      if(flag_is_set(CCBFLAGS,CCB_LDSIZE))
        {
          HDX1616     = ((int32_t)opera_mem_read32(CURRENTCCB)) >> 4;
          CURRENTCCB += 4;
          HDY1616     = ((int32_t)opera_mem_read32(CURRENTCCB)) >> 4;
          CURRENTCCB += 4;
          VDX1616     = opera_mem_read32(CURRENTCCB);
          CURRENTCCB += 4;
          VDY1616     = opera_mem_read32(CURRENTCCB);
          CURRENTCCB += 4;
        }

      if(flag_is_set(CCBFLAGS,CCB_LDPRS))
        {
          HDDX1616    = ((int32_t)opera_mem_read32(CURRENTCCB)) >> 4;
          CURRENTCCB += 4;
          HDDY1616    = ((int32_t)opera_mem_read32(CURRENTCCB)) >> 4;
          CURRENTCCB += 4;
        }

      if(flag_is_set(CCBFLAGS,CCB_LDPPMP))
        {
          PIXC        = opera_mem_read32(CURRENTCCB);
          CURRENTCCB += 4;
        }

      if(flag_is_set(CCBFLAGS,CCB_CCBPRE))
        {
          PRE0        = opera_mem_read32(CURRENTCCB);
          CURRENTCCB += 4;
          if(flag_is_clr(CCBFLAGS,CCB_PACKED))
            {
              PRE1        = opera_mem_read32(CURRENTCCB);
              CURRENTCCB += 4;
            }
        }
      else
        {
          PRE0   = opera_mem_read32(PDATA);
          PDATA += 4;
          if(flag_is_clr(CCBFLAGS,CCB_PACKED))
            {
              PRE1   = opera_mem_read32(PDATA);
              PDATA += 4;
            }
        }

      /* PDEC data compute */
      {
        /* pdec.mode = PRE0 & PRE0_BPP_MASK; */
        switch(PRE0 & PRE0_BPP_MASK)
          {
            /*
              WO1994010642A1 describes BPP=0 in the low-bit-depth PIP load
              group with BPP=1 and BPP=2, rather than as a CEL no-op.
            */
          case PRE0_BPP_RESERVED_0:
          case PRE0_BPP_1:
            pdec.plutaCCBbits  = ((CCBFLAGS & 0x0F) * 4);
            pdec.pixelBitsMask = 1; /* 1 bit */
            break;
          case PRE0_BPP_RESERVED_7:
            continue;
          case PRE0_BPP_2:
            pdec.plutaCCBbits  = ((CCBFLAGS & 0x0E) * 4);
            pdec.pixelBitsMask = 3; /* 2 bit */
            break;
          case PRE0_BPP_4:
          default:
            pdec.plutaCCBbits  = ((CCBFLAGS & 0x08) * 4);
            pdec.pixelBitsMask = 15; /* 4 bit */
            break;
          }

        pproj.pmode        = (CCBFLAGS & CCB_POVER_MASK);
        pproj.pmodeORmask  = ((pproj.pmode == PMODE_ONE ) ? 0x8000 : 0x0000);
        pproj.pmodeANDmask = ((pproj.pmode != PMODE_ZERO) ? 0xFFFF : 0x7FFF);
      }

      if(flag_is_set(CCBFLAGS,CCB_LDPLUT))
        {
          switch(PRE0 & PRE0_BPP_MASK)
            {
            case PRE0_BPP_RESERVED_0:
            case PRE0_BPP_1:
              LoadPLUT(PLUTDATA,2);
              break;
            case PRE0_BPP_2:
              LoadPLUT(PLUTDATA,4);
              break;
            case PRE0_BPP_4:
              LoadPLUT(PLUTDATA,16);
              break;
            default:
              LoadPLUT(PLUTDATA,32);
            };
        }

      if(flag_is_set(CCBFLAGS,CCB_SKIP))
        continue;

      cel_opera_stat();
      if(flag_is_set(CCBFLAGS,CCB_PACKED))
        DrawPackedCel();
      else if(flag_is_set(PRE1,PRE1_LRFORM) && (BPP[PRE0 & PRE0_BPP_MASK] == 16))
        DrawLRCel();
      else
        DrawLiteralCel();
    }

  if(flag_is_set(CCBFLAGS,CCB_LAST))
    NEXTCCB = 0;
  clr_flag(STATBITS,SPRON);
  MADAM.FSM = FSM_IDLE;
}

/* ===== opera_madam.c: decodificador/projetor/rasterizador (linhas 1410-1871, 1878-2734, 2750-fim) ===== */
/*
  https://3dodev.com?do=search&id=start&q=UNCLSB
  https://3dodev.com?do=search&id=start&q=PDCLSB
*/
static
uint16_t
PDEC_SubstituteBlueLSB(const uint16_t pixel_,
                       const uint32_t mode_)
{
  uint16_t blueLSB;

  switch(mode_ & 0x3)
    {
    default:
    case 0:
      blueLSB = 0;
      break;
    case 1:
      blueLSB = (pixel_ & 0x1);
      break;
    case 2:
      blueLSB = ((pixel_ >> 4) & 0x1);
      break;
    case 3:
      blueLSB = ((pixel_ >> 5) & 0x1);
      break;
    }

  return ((pixel_ & ~0x1) | blueLSB);
}

static
uint32_t
PDEC(const uint32_t  pixel_,
     uint16_t       *amv_)
{
  pdeco_t pix1;
  uint16_t resamv;
  uint16_t pres;

  pix1.raw = pixel_;

  switch(PRE0 & PRE0_BPP_MASK)
    {
    default:
    case PRE0_BPP_RESERVED_0:
    case PRE0_BPP_1:
    case PRE0_BPP_2:
    case PRE0_BPP_4:
      pres   = MADAM.PLUT[(pdec.plutaCCBbits + ((pix1.raw & pdec.pixelBitsMask) * 2)) >> 1];
      resamv = 0x49;
      break;

    case PRE0_BPP_6:
      /* pmode = pix1.c6b.pw; ??? */
      pres   = MADAM.PLUT[pix1.c6b.c];
      pres   = (pres & 0x7FFF) + (pix1.c6b.pw << 15);
      resamv = 0x49;
      break;

    case PRE0_BPP_8:
      if(flag_is_set(PRE0,PRE0_LINEAR))
        {
          pres   = MAPu8b[pix1.raw & 0xFF];
          resamv = 0x49;
        }
      else
        {
          pres   = MADAM.PLUT[pix1.c8b.c];
          resamv = MAPc8bAMV[pix1.raw & 0xFF];
        }
      break;

    case PRE0_BPP_16:
    case PRE0_BPP_RESERVED_7:
      if(flag_is_set(PRE0,PRE0_LINEAR))
        {
          pres   = pix1.raw;
          resamv = 0x49;
        }
      else
        {
          pres   = MADAM.PLUT[pix1.c16b.c];
          pres   = ((pres & 0x7FFF) | (pixel_ & 0x8000));
          resamv = MAPc16bAMV[(pix1.raw >> 5) & 0x1FF];
        }

      break;
    }

  *amv_ = resamv;

  /* Saving bit 0 (blue lsb) and bit 15 (p-mode) */
  pdec.rawVHBits = (pres & 0x8001);

  /* Transparency is determined before projector blue-LSB substitution. */
  pproj.Transparent = (flag_is_clr(CCBFLAGS,CCB_BGND) &&
                       IS_RGB15_BLACK(pres));

  if(flag_is_clr(CCBFLAGS,CCB_PACKED))
    pres = PDEC_SubstituteBlueLSB(pres,((PRE1 & PRE1_TLLSB_MASK) >> PRE1_TLLSB_SHIFT));
  else
    pres = PDEC_SubstituteBlueLSB(pres,((CECONTROL & PDCLSB_MASK) >> PDCLSB_SHIFT));

  /* conceptual end of decoder */

  /*
    TODO: Do PROJECTOR functions now?
    They'll be done before using the PROCESSOR.

    if(!(PRE1&PRE1_NOSWAP) && (CECONTROL&(1<<27)))
    pres = (pres&0x7ffe)|((pres&0x8000)>>15)|((pres&1)<<15);

    if(!(CECONTROL & 0x80000000))
    pres = (pres&0x7fff)|((CECONTROL>>15)&0x8000);

    pres=(pres|pdec.pmodeORmask)&pdec.pmodeANDmask;
  */

  return pres;
}

static
INLINE
uint8_t
BitReaderBig_ReadLRByte(const struct BitReaderBig *bit_,
                        const uint32_t             point_)
{
  return opera_mem_read8(bit_->buf + ((point_ & 1) + ((point_ >> 1) << 2)));
}

static
uint32_t
BitReaderBig_ReadLRForm(struct BitReaderBig *bit_,
                        const uint8_t        bits_)
{
  uint32_t i;
  uint32_t retval;

  retval = 0;
  BitReaderBig_SetBitRate(bit_,bits_);

  if(!bit_->buf)
    return retval;

  for(i = 0; i < (uint32_t)bit_->bitset; i++)
    {
      retval <<= 1;
      retval  |= ((BitReaderBig_ReadLRByte(bit_,bit_->point) >> (7 - bit_->bitpoint)) & 1);

      bit_->bitpoint++;
      if(bit_->bitpoint == 8)
        {
          bit_->bitpoint = 0;
          bit_->point++;
        }
    }

  return retval;
}

static
uint32_t
PPROJ_OUTPUT(uint32_t pproc_output_,
             uint32_t pframe_input_)
{
  int32_t  b15mode;
  int32_t  b0mode;
  uint32_t VHOutput;

  /*
    CCB_PLUTPOS flag
    Determine projector's originating source of VH values.
  */

  if(flag_is_set(CCBFLAGS,CCB_PLUTPOS)) /* Use pixel decoder output. */
    VHOutput = pdec.rawVHBits;
  else /* Use VH values determined from the CEL's origin. */
    VHOutput = CEL_ORIGIN_VH_VALUE;

  /*
    SWAPHV flag
    Swap the H and V values now if requested.
  */
  if(flag_is_set(CECONTROL,SWAPHV))
    {
      /* TODO: I have read that PRE1 is only set for unpacked CELs.
         So... should this be ignored if using packed CELs? I don't
         know. */
      if(flag_is_clr(PRE1,PRE1_NOSWAP))
        VHOutput = ((VHOutput >> 15) | ((VHOutput & 1) << 15));
    }

#if 0
  /*
    CFBDSUB flag
    Substitute the VH values from the frame buffer if requested.
  */
  if(flag_is_set(CECONTROL,CFBDSUB))
    {
      /* TODO: This should be re-enabled sometime. However, it currently
         causes the wing commander 3 movies to screw up again! There
         must be some missing mbehavior elsewhere. */
      VHOutput = (pframe_input & 0x8001);
    }
#endif

  /*
    B15POS_MASK settings
    Substitute the V value explicitly if requested.
  */
  b15mode = (CECONTROL & B15POS_MASK);
  switch(b15mode)
    {
    case B15POS_PDC:
    default:
      /* do nothing */
      break;
    case B15POS_0:
      VHOutput = (VHOutput & ~0x8000);
      break;
    case B15POS_1:
      VHOutput |= 0x8000;
      break;
    }

  /*
    B15POS_MASK settings
    Substitute the H value explicitly if requested.
  */
  b0mode = (CECONTROL & B0POS_MASK);
  switch(b0mode)
    {
    case B0POS_PDC:
      /* do nothing  */
      break;
    case B0POS_PIXC:
      /* Use LSB from pixel processor output. */
      VHOutput = ((VHOutput & ~0x1) | (pproc_output_ & 0x1));
      break;
    case B0POS_0:
      VHOutput = (VHOutput & ~0x1);
      break;
    case B0POS_1:
      VHOutput |= 0x01;
      break;
    }

  return ((pproc_output_ & 0x7FFE) | VHOutput);
}

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef CLAMP
#define CLAMP(x, a, b) (MIN(b,MAX(x,a)))
#endif

static
OPERA_FORCEINLINE
int8_t
clamp_i8(const int8_t x_,
         const int8_t min_,
         const int8_t max_)
{
  return CLAMP(x_,min_,max_);
}

static
uint32_t
PPROC(uint32_t pixel_,
      uint32_t fpix_,
      uint32_t amv_)
{
  AVS_t AV;
  PXC_t pixc;

  pdeco_t input1;
  pdeco_t out;
  pdeco_t pix1;

#pragma pack(push,1)
  union
  {
    uint32_t raw;
    struct
    {
      int8_t R;
      int8_t B;
      int8_t G;
      int8_t a;
    };
  } color1, color2, AOP, BOP;
#pragma pack(pop)

  /*
    Set PMODE according to the values set up in the CCBFLAGS word.
    (This merely uses masks here because it's faster).
    This is a duty of the PROJECTOR, but we'll do it here because its
    easier.
  */
  pixel_ = ((pixel_ | pproj.pmodeORmask) & pproj.pmodeANDmask);

  pixc.raw = (PIXC & 0xFFFF);
  if(pixel_ & 0x8000)
    pixc.raw = (PIXC >> 16);

  /*
    now let's select the sources
    1. av
    2. input1
    3. input2
    pixc.raw = 0;
  */

  if(flag_is_set(CCBFLAGS,CCB_USEAV))
    {
      AV.raw = pixc.meaning.av;
    }
  else
    {
      AV.avsignal.dv3   = 0;
      AV.avsignal.nCLIP = 0;
      AV.avsignal.XTEND = 0;
      AV.avsignal.NEG   = 0;
    }

  if(!pixc.meaning.s1)
    input1.raw = pixel_;
  else
    input1.raw = fpix_;

  switch(pixc.meaning.s2)
    {
    case 0:
      color2.raw = 0;
      break;
    case 1:
      color2.R = color2.G = color2.B = (pixc.meaning.av >> AV.avsignal.dv3);
      break;
    case 2:
      pix1.raw = fpix_;
      color2.R = ((pix1.r16b.r) >> AV.avsignal.dv3);
      color2.G = ((pix1.r16b.g) >> AV.avsignal.dv3);
      color2.B = ((pix1.r16b.b) >> AV.avsignal.dv3);
      break;
    case 3:
      pix1.raw = pixel_;
      color2.R = (pix1.r16b.r >> AV.avsignal.dv3);
      color2.G = (pix1.r16b.g >> AV.avsignal.dv3);
      color2.B = (pix1.r16b.b >> AV.avsignal.dv3);
      break;
    }

  switch(pixc.meaning.ms)
    {
    case 0:
      color1.R = PSCALAR[pixc.meaning.mxf][pixc.meaning.dv1][input1.r16b.r];
      color1.G = PSCALAR[pixc.meaning.mxf][pixc.meaning.dv1][input1.r16b.g];
      color1.B = PSCALAR[pixc.meaning.mxf][pixc.meaning.dv1][input1.r16b.b];
      break;
    case 1:
      color1.R = PSCALAR[(amv_ >> 6) & 7][pixc.meaning.dv1][input1.r16b.r];
      color1.G = PSCALAR[(amv_ >> 3) & 7][pixc.meaning.dv1][input1.r16b.g];
      color1.B = PSCALAR[(amv_ >> 0) & 7][pixc.meaning.dv1][input1.r16b.b];
      break;
    case 2:
      pix1.raw = pixel_;
      color1.R = PSCALAR[pix1.r16b.r >> 2][pix1.r16b.r & 3][input1.r16b.r];
      color1.G = PSCALAR[pix1.r16b.g >> 2][pix1.r16b.g & 3][input1.r16b.g];
      color1.B = PSCALAR[pix1.r16b.b >> 2][pix1.r16b.b & 3][input1.r16b.b];
      break;
    case 3:
      pix1.raw = pixel_;
      color1.R = PSCALAR[pix1.r16b.r >> 2][pixc.meaning.dv1][input1.r16b.r];
      color1.G = PSCALAR[pix1.r16b.g >> 2][pixc.meaning.dv1][input1.r16b.g];
      color1.B = PSCALAR[pix1.r16b.b >> 2][pixc.meaning.dv1][input1.r16b.b];
      break;
    }

  /* Use this to render magenta for testing. */
#if 0
  return RGB15_MAGENTA;
#endif

  /*
    ok -- we got the sources -- now RGB processing
  */

  /* AOP/BOP calculation */
  AOP.raw     = (color1.raw & PXOR1);
  color1.raw &= PXOR2;

  if(AV.avsignal.NEG)
    BOP.raw = (color2.raw ^ 0x00FFFFFF);
  else
    BOP.raw = (color2.raw ^ color1.raw);

  if(AV.avsignal.XTEND)
    {
      BOP.R = ((BOP.R << 3) >> 3);
      BOP.B = ((BOP.B << 3) >> 3);
      BOP.G = ((BOP.G << 3) >> 3);
    }

  color2.R = ((AOP.R + BOP.R + AV.avsignal.NEG) >> pixc.meaning.dv2);
  color2.G = ((AOP.G + BOP.G + AV.avsignal.NEG) >> pixc.meaning.dv2);
  color2.B = ((AOP.B + BOP.B + AV.avsignal.NEG) >> pixc.meaning.dv2);

  if(!AV.avsignal.nCLIP)
    {
      color2.R = clamp_i8(color2.R,0,31);
      color2.G = clamp_i8(color2.G,0,31);
      color2.B = clamp_i8(color2.B,0,31);
    }

  out.raw    = 0;
  out.r16b.r = color2.R;
  out.r16b.g = color2.G;
  out.r16b.b = color2.B;

  /* TODO: Is this something the PROJECTOR should do? */
  if(flag_is_clr(CCBFLAGS,CCB_NOBLK) && IS_RGB15_BLACK(out.raw))
    out.raw = RGB15_NEAR_BLACK;

  /*
    if(!(PRE1 & PRE1_NOSWAP) && (CECONTROL & (1 << 27)))
    out.raw = ((out.raw & 0x7FFE) |
    ((out.raw & 0x8000) >> 15) |
    ((out.raw & 1) << 15));
  */

  /*
    if(!(CECONTROL & 0x80000000))
    out.raw = ((out.raw & 0x7FFF) | ((CECONTROL >> 15) & 0x8000));
  */

  return out.raw;
}


static
INLINE
void
process_pixel(int32_t x_,
              int32_t y_,
              uint32_t curpix_,
              uint32_t lawv_)
{
  int32_t p;
  int32_t fp;

  fp = opera_mem_read16(REGCTL2 + XY2OFF(x_,y_,MADAM.rmod));
  p  = PPROC(curpix_,fp,lawv_);
  p  = PPROJ_OUTPUT(p,fp);
  opera_mem_write16(REGCTL3 + XY2OFF(x_,y_,MADAM.wmod),p);
}

static
INLINE
uint32_t
PackedSkipPixels(const uint32_t  type_,
                 const uint32_t  pixel_count_,
                 uint32_t       *skip_pixels_,
                 const uint32_t  bpp_)
{
  uint32_t skipped;

  if((*skip_pixels_ == 0) || (pixel_count_ == 0) || (type_ == PACK_EOL))
    return pixel_count_;

  skipped = (*skip_pixels_ < pixel_count_) ? *skip_pixels_ : pixel_count_;

  switch(type_)
    {
    case PACK_LITERAL:
      BitReaderBig_Skip(&bitoper,bpp_ * skipped);
      break;
    case PACK_PACKED:
      if(skipped == pixel_count_)
        BitReaderBig_Skip(&bitoper,bpp_);
      break;
    default:
      break;
    }

  *skip_pixels_ -= skipped;

  return pixel_count_ - skipped;
}

static
void
DrawPackedCel(void)
{
  int row;
  bool eor;
  uint16_t CURPIX;
  uint16_t LAMV;
  int32_t lastaddr;
  int32_t xcur;
  int32_t ycur;
  int32_t xvert;
  int32_t yvert;
  int32_t xdown;
  int32_t ydown;
  int32_t hdx;
  int32_t hdy;
  int32_t origin_x;
  int32_t origin_y;
  int32_t origin_vdx;
  int32_t origin_vdy;
  uint32_t bpp;
  uint32_t type;
  uint32_t pdata;
  uint32_t nrows;
  uint32_t offset;
  uint32_t offsetl;
  uint32_t skipx;

  pdata   = PDATA;
  nrows   = ((PRE0 & PRE0_VCNT_MASK) >> PRE0_VCNT_SHIFT);
  bpp     = BPP[PRE0 & PRE0_BPP_MASK];
  offsetl = ((bpp < 8) ? 1 : 2);
  skipx   = PRE0_SKIPX(PRE0);
  SPRHI   = nrows + 1;

  origin_x   = XPOS1616;
  origin_y   = YPOS1616;
  origin_vdx = VDX1616;
  origin_vdy = VDY1616;

  if(TestInitVisual(PACKED))
    {
      StoreXYPosFromDrawHeight(origin_x,origin_y,origin_vdx,origin_vdy,SPRHI);
      return;
    }

  xvert = XPOS1616;
  yvert = YPOS1616;

  switch(TEXEL_DRAW_FUNC)
    {
    case TEXEL_DRAW_FUNC_LINE:
      {
        uint32_t pixel_count;
        for(row = 0; row < TEXTURE_HI_LIM; row++)
          {
            int wcnt;
            int scipw;
            uint32_t src_skip;

            BitReaderBig_AttachBuffer(&bitoper,pdata);
            offset = BitReaderBig_Read(&bitoper,(offsetl << 3));

            lastaddr  = (pdata + ((offset + 2) << 2));
            eor       = false;
            xcur      = xvert;
            ycur      = yvert;
            xvert    += VDX1616;
            yvert    += VDY1616;

            if(TEXTURE_HI_START)
              {
                TEXTURE_HI_START--;
                pdata = lastaddr;
                continue;
              }

            src_skip = skipx;
            scipw = TEXTURE_WI_START;
            wcnt  = scipw;

            /* while not end of row */
            while(!eor)
              {
                type = BitReaderBig_Read(&bitoper,2);
                if((int32_t)(bitoper.point + pdata) >= (lastaddr))
                  type = PACK_EOL;

                pixel_count = BitReaderBig_Read(&bitoper,6) + 1;

                if(src_skip)
                  {
                    pixel_count = PackedSkipPixels(type,pixel_count,&src_skip,bpp);
                    if(!pixel_count)
                      continue;
                  }

                if(scipw)
                  {
                    if(type == PACK_EOL)
                      break;
                    if(scipw >= (int32_t)(pixel_count))
                      {
                        scipw -= (pixel_count);
                        if(HDX1616)
                          xcur += (HDX1616 * pixel_count);
                        if(HDY1616)
                          ycur += (HDY1616 * pixel_count);
                        if(type == PACK_LITERAL)
                          BitReaderBig_Skip(&bitoper,bpp*pixel_count);
                        else if(type == PACK_PACKED)
                          BitReaderBig_Skip(&bitoper,bpp);
                        continue;
                      }
                    else
                      {
                        if(HDX1616)
                          xcur += (HDX1616 * scipw);
                        if(HDY1616)
                          ycur += (HDY1616 * scipw);
                        pixel_count -= scipw;
                        if(type == PACK_LITERAL)
                          BitReaderBig_Skip(&bitoper,bpp*scipw);
                        scipw = 0;
                      }
                  }

                /*
                  if(wcnt >= TEXTURE_WI_LIM)
                  break;
                */
                wcnt += pixel_count;
                if(wcnt > TEXTURE_WI_LIM)
                  {
                    pixel_count -= (wcnt - TEXTURE_WI_LIM);
                    /*
                      if(pixel_count >> 31)
                      break;
                    */
                  }

                switch(type)
                  {
                  case PACK_EOL:
                    eor = true;
                    break;
                  case PACK_LITERAL:
                    {
                      int pix;
                      for(pix = 0; pix < pixel_count; pix++)
                        {
                          CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);
                          if(!pproj.Transparent)
                            process_pixel(xcur >> 16,ycur >> 16,CURPIX,LAMV);

                          xcur += HDX1616;
                          ycur += HDY1616;
                        }
                    }
                    break;
                  case PACK_TRANSPARENT:
                    if(HDX1616)
                      xcur += (HDX1616 * pixel_count);
                    if(HDY1616)
                      ycur += (HDY1616 * pixel_count);
                    break;
                  case PACK_PACKED:
                    CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);

                    if(!pproj.Transparent)
                      TexelDrawLine(CURPIX,LAMV,xcur,ycur,pixel_count);

                    if(HDX1616)
                      xcur += (HDX1616 * pixel_count);
                    if(HDY1616)
                      ycur += (HDY1616 * pixel_count);
                    break;
                  }

                if(wcnt >= TEXTURE_WI_LIM)
                  break;
              }

            pdata = lastaddr;
          }
      }
      break;
    case TEXEL_DRAW_FUNC_SCALE:
      {
        int row;
        uint32_t src_skip;

        for(row = 0; row < SPRHI; row++)
          {
            BitReaderBig_AttachBuffer(&bitoper,pdata);
            offset = BitReaderBig_Read(&bitoper,(offsetl << 3));

            lastaddr = (pdata + ((offset + 2) << 2));

            eor = false;

            src_skip = skipx;
            xcur   = xvert;
            ycur   = yvert;
            xvert += VDX1616;
            yvert += VDY1616;

            /* while not end of row */
            while(!eor)
              {
                int32_t pixel_count;

                type = BitReaderBig_Read(&bitoper,2);
                if((bitoper.point + pdata) >= lastaddr)
                  type = PACK_EOL;

                pixel_count = (BitReaderBig_Read(&bitoper,6) + 1);

                if(src_skip)
                  {
                    pixel_count = (int32_t)PackedSkipPixels(type,(uint32_t)pixel_count,&src_skip,bpp);
                    if(!pixel_count)
                      continue;
                  }

                switch(type)
                  {
                  case PACK_EOL:
                    eor = true;
                    break;
                  case PACK_LITERAL:
                    while(pixel_count)
                      {
                        pixel_count--;
                        CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);

                        if(!pproj.Transparent)
                          {
                            if(TexelDrawScale(CURPIX,
                                              LAMV,
                                              xcur >> 16,
                                              ycur >> 16,
                                              ((xcur + (HDX1616 + VDX1616)) >> 16),
                                              ((ycur + (HDY1616 + VDY1616)) >> 16)))
                              break;
                          }

                        xcur += HDX1616;
                        ycur += HDY1616;
                      }
                    break;
                  case PACK_TRANSPARENT:
                    xcur  += (HDX1616 * pixel_count);
                    ycur  += (HDY1616 * pixel_count);
                    pixel_count  = 0;
                    break;
                  case PACK_PACKED:
                    CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);
                    if(!pproj.Transparent)
                      {
                        if(flag_is_set(CCBFLAGS,CCB_MARIA))
                          {
                            while(pixel_count)
                              {
                                if(TexelDrawScale(CURPIX,
                                                  LAMV,
                                                  xcur >> 16,
                                                  ycur >> 16,
                                                  ((xcur + HDX1616 + VDX1616) >> 16),
                                                  ((ycur + HDY1616 + VDY1616) >> 16)))
                                  break;

                                pixel_count--;
                                xcur += HDX1616;
                                ycur += HDY1616;
                              }
                            break;
                          }
                        else
                          {
                            if(TexelDrawScale(CURPIX,
                                              LAMV,
                                              xcur >> 16,
                                              ycur >> 16,
                                              ((xcur + (HDX1616 * pixel_count) + VDX1616) >> 16),
                                              ((ycur + (HDY1616 * pixel_count) + VDY1616) >> 16)))
                              break;
                          }

                      }

                    xcur += (HDX1616 * pixel_count);
                    ycur += (HDY1616 * pixel_count);
                    pixel_count = 0;
                    break;
                  }

                if(pixel_count)
                  break;
              }

            pdata = lastaddr;
          }
      }
      break;
    case TEXEL_DRAW_FUNC_ARBITRARY:
      {
        int row;
        uint32_t pixel_count;
        uint32_t src_skip;
        for(row = 0; row < SPRHI; row++)
          {
            BitReaderBig_AttachBuffer(&bitoper,pdata);
            offset = BitReaderBig_Read(&bitoper,(offsetl << 3));

            lastaddr = (pdata + ((offset + 2) << 2));

            g_row_top = ROW_TOP(row);

            eor = false;

            src_skip = skipx;
            xcur = xvert;
            ycur = yvert;
            hdx  = HDX1616;
            hdy  = HDY1616;

            xvert   += VDX1616;
            yvert   += VDY1616;
            HDX1616 += HDDX1616;
            HDY1616 += HDDY1616;

            xdown = xvert;
            ydown = yvert;

            /* while not end of row */
            while(!eor)
              {
                type = BitReaderBig_Read(&bitoper,2);
                if((bitoper.point + pdata) >= lastaddr)
                  type = PACK_EOL;

                pixel_count = (BitReaderBig_Read(&bitoper,6) + 1);

                if(src_skip)
                  {
                    pixel_count = PackedSkipPixels(type,pixel_count,&src_skip,bpp);
                    if(!pixel_count)
                      continue;
                  }

                switch(type)
                  {
                  case PACK_EOL:
                    eor = true;
                    break;
                  case PACK_LITERAL:
                    while(pixel_count)
                      {
                        CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);
                        pixel_count--;

                        if(!pproj.Transparent)
                          {
                            if(TexelDrawArbitrary(CURPIX,
                                                  LAMV,
                                                  xcur,
                                                  ycur,
                                                  xcur + hdx,
                                                  ycur + hdy,
                                                  xdown + HDX1616,
                                                  ydown + HDY1616,
                                                  xdown,
                                                  ydown))
                              break;
                          }

                        xcur  += hdx;
                        ycur  += hdy;
                        xdown += HDX1616;
                        ydown += HDY1616;
                      }
                    break;
                  case PACK_TRANSPARENT:
                    xcur  += (hdx * pixel_count);
                    ycur  += (hdy * pixel_count);
                    xdown += (HDX1616 * pixel_count);
                    ydown += (HDY1616 * pixel_count);
                    pixel_count = 0;
                    break;
                  case PACK_PACKED:
                    CURPIX = PDEC(BitReaderBig_Read(&bitoper,bpp),&LAMV);

                    if(!pproj.Transparent)
                      {
                        while(pixel_count)
                          {
                            pixel_count--;
                            if(TexelDrawArbitrary(CURPIX,
                                                  LAMV,
                                                  xcur,
                                                  ycur,
                                                  xcur + hdx,
                                                  ycur + hdy,
                                                  xdown + HDX1616,
                                                  ydown + HDY1616,
                                                  xdown,
                                                  ydown))
                              break;
                            xcur  += hdx;
                            ycur  += hdy;
                            xdown += HDX1616;
                            ydown += HDY1616;
                          }
                      }
                    else
                      {
                        xcur  += (hdx * pixel_count);
                        ycur  += (hdy * pixel_count);
                        xdown += (HDX1616 * pixel_count);
                        ydown += (HDY1616 * pixel_count);
                        pixel_count = 0;
                      }
                    break;
                  }

                if(pixel_count)
                  break;
              }

            pdata = lastaddr;
          }
      }
      break;
    }

  SPRWI++;

  StoreXYPosFromDrawHeight(origin_x,origin_y,origin_vdx,origin_vdy,SPRHI);
}

static
void
DrawLiteralCel(void)
{
  int32_t xcur;
  int32_t ycur;
  int32_t xvert;
  int32_t yvert;
  int32_t xdown;
  int32_t ydown;
  int32_t hdx;
  int32_t hdy;
  int32_t origin_x;
  int32_t origin_y;
  int32_t origin_vdx;
  int32_t origin_vdy;
  uint16_t CURPIX;
  uint16_t LAMV;
  bool     lrform;
  uint32_t bpp;
  uint32_t offset;
  uint32_t offsetl;
  uint32_t row_bytes;
  uint32_t skipx;
  uint32_t source_base;
  uint32_t source_width;

  bpp     = BPP[PRE0 & PRE0_BPP_MASK];
  lrform  = flag_is_set(PRE1,PRE1_LRFORM);
  offsetl = ((bpp < 8) ? 1 : 2);
  offset  = ((offsetl == 1) ?
             ((PRE1 & PRE1_WOFFSET8_MASK) >> PRE1_WOFFSET8_SHIFT):
             ((PRE1 & PRE1_WOFFSET10_MASK) >> PRE1_WOFFSET10_SHIFT));
  row_bytes = ((offset + 2) << 2);
  skipx        = PRE0_SKIPX(PRE0);
  source_base  = PDATA;
  source_width = (1 + (PRE1 & PRE1_TLHPCNT_MASK));

  SPRHI = (1 + ((PRE0 & PRE0_VCNT_MASK) >> PRE0_VCNT_SHIFT));
  /* LRFORM describes source row-pair layout, not just pixel depth. AITD2
     uses a 1-bpp LRFORM CEL as a priority mask over LR-format bitmap data. */
  if(lrform)
    SPRHI <<= 1;

  origin_x   = XPOS1616;
  origin_y   = YPOS1616;
  origin_vdx = VDX1616;
  origin_vdy = VDY1616;

  /* TLHPCNT includes SKIPX.  The first unskipped source pixel maps to
     the original destination edge, and exhausted spans draw nothing. */
  if(skipx >= source_width)
    {
      SPRWI = 0;
      HDX1616 = (int32_t)((uint32_t)HDX1616 +
                          ((uint32_t)HDDX1616 * (uint32_t)SPRHI));
      HDY1616 = (int32_t)((uint32_t)HDY1616 +
                          ((uint32_t)HDDY1616 * (uint32_t)SPRHI));
      StoreXYPosFromDrawHeight(origin_x,origin_y,origin_vdx,origin_vdy,SPRHI);
      return;
    }

  SPRWI = (int32_t)(source_width - skipx);

  if(TestInitVisual(UNPACKED))
    {
      StoreXYPosFromDrawHeight(origin_x,origin_y,origin_vdx,origin_vdy,SPRHI);
      return;
    }

  xvert = XPOS1616;
  yvert = YPOS1616;

  switch(TEXEL_DRAW_FUNC)
    {
    case TEXEL_DRAW_FUNC_LINE:
      {
        uint32_t i;

        xvert += (TEXTURE_HI_START * VDX1616);
        yvert += (TEXTURE_HI_START * VDY1616);

        if(SPRWI > TEXTURE_WI_LIM)
          SPRWI = TEXTURE_WI_LIM;

        for(i = TEXTURE_HI_START; i < TEXTURE_HI_LIM; i++)
          {
            uint32_t j;

            BitReaderBig_AttachBuffer(&bitoper,
                                      source_base + (lrform ?
                                                     XY2OFF(0,i,row_bytes) :
                                                     (row_bytes * i)));
            xcur = (xvert + TEXTURE_WI_START * HDX1616);
            ycur = (yvert + TEXTURE_WI_START * HDY1616);
            BitReaderBig_Skip(&bitoper,(bpp * skipx));
            if(TEXTURE_WI_START)
              BitReaderBig_Skip(&bitoper,(bpp * TEXTURE_WI_START));

            xvert += VDX1616;
            yvert += VDY1616;

            for(j = TEXTURE_WI_START; j < SPRWI; j++)
              {
                CURPIX = PDEC(lrform ?
                              BitReaderBig_ReadLRForm(&bitoper,bpp) :
                              BitReaderBig_Read(&bitoper,bpp),&LAMV);

                if(!pproj.Transparent)
                  process_pixel(xcur >> 16,ycur >> 16,CURPIX,LAMV);

                xcur += HDX1616;
                ycur += HDY1616;
              }
          }
      }
      break;
    case TEXEL_DRAW_FUNC_SCALE:
      {
        uint32_t i;
        uint32_t j;

        for(i = 0; i < SPRHI; i++)
          {
            BitReaderBig_AttachBuffer(&bitoper,
                                      source_base + (lrform ?
                                                     XY2OFF(0,i,row_bytes) :
                                                     (row_bytes * i)));
            xcur   = xvert;
            ycur   = yvert;
            xvert += VDX1616;
            yvert += VDY1616;
            BitReaderBig_Skip(&bitoper,(bpp * skipx));

            for(j = 0; j < SPRWI; j++)
              {
                CURPIX = PDEC(lrform ?
                              BitReaderBig_ReadLRForm(&bitoper,bpp) :
                              BitReaderBig_Read(&bitoper,bpp),&LAMV);

                if(!pproj.Transparent)
                  {
                    if(TexelDrawScale(CURPIX,
                                      LAMV,
                                      xcur >> 16,
                                      ycur >> 16,
                                      ((xcur + HDX1616 + VDX1616) >> 16),
                                      ((ycur + HDY1616 + VDY1616) >> 16)))
                      break;
                  }

                xcur += HDX1616;
                ycur += HDY1616;
              }
          }
      }
      break;
    case TEXEL_DRAW_FUNC_ARBITRARY:
    default:
      {
        uint32_t i;
        uint32_t j;

        for(i = 0; i < SPRHI; i++)
          {
            BitReaderBig_AttachBuffer(&bitoper,
                                      source_base + (lrform ?
                                                     XY2OFF(0,i,row_bytes) :
                                                     (row_bytes * i)));

            g_row_top = ROW_TOP(i);
            xcur = xvert;
            ycur = yvert;
            hdx  = HDX1616;
            hdy  = HDY1616;

            xvert   += VDX1616;
            yvert   += VDY1616;
            HDX1616 += HDDX1616;
            HDY1616 += HDDY1616;

            BitReaderBig_Skip(&bitoper,(bpp * skipx));

            xdown = xvert;
            ydown = yvert;

            for(j = 0; j < SPRWI; j++)
              {
                CURPIX = PDEC(lrform ?
                              BitReaderBig_ReadLRForm(&bitoper,bpp) :
                              BitReaderBig_Read(&bitoper,bpp),&LAMV);

                if(!pproj.Transparent)
                  {
                    if(TexelDrawArbitrary(CURPIX,
                                          LAMV,
                                          xcur,
                                          ycur,
                                          xcur + hdx,
                                          ycur + hdy,
                                          xdown + HDX1616,
                                          ydown + HDY1616,
                                          xdown,
                                          ydown))
                      break;
                  }

                xcur  += hdx;
                ycur  += hdy;
                xdown += HDX1616;
                ydown += HDY1616;
              }
          }
      }
      break;
    }

  StoreXYPosFromDrawHeight(origin_x,origin_y,origin_vdx,origin_vdy,SPRHI);
}

static
void
DrawLRCel(void)
{
  int32_t i;
  int32_t j;
  int32_t xcur;
  int32_t ycur;
  int32_t xvert;
  int32_t yvert;
  int32_t xdown;
  int32_t ydown;
  int32_t hdx;
  int32_t hdy;
  int32_t origin_x;
  int32_t origin_y;
  int32_t origin_vdx;
  int32_t origin_vdy;
  uint16_t CURPIX;
  uint16_t LAMV;
  uint32_t bpp;
  uint32_t offset;
  uint32_t offsetl;

  bpp      = BPP[PRE0 & PRE0_BPP_MASK];
  offsetl  = ((bpp < 8) ? 1 : 2);
  offset   = ((offsetl == 1) ?
              ((PRE1 & PRE1_WOFFSET8_MASK)  >> PRE1_WOFFSET8_SHIFT) :
              ((PRE1 & PRE1_WOFFSET10_MASK) >> PRE1_WOFFSET10_SHIFT));
  offset  += 2;

  SPRWI = (1 + (PRE1 & PRE1_TLHPCNT_MASK));
  SPRHI = ((((PRE0 & PRE0_VCNT_MASK) >> PRE0_VCNT_SHIFT) << 1) + 2); /* doom fix */

  origin_x   = XPOS1616;
  origin_y   = YPOS1616;
  origin_vdx = VDX1616;
  origin_vdy = VDY1616;

  if(TestInitVisual(UNPACKED))
    {
      StoreXYPosFromDrawHeight(origin_x,origin_y,origin_vdx,origin_vdy,SPRHI);
      return;
    }

  xvert = XPOS1616;
  yvert = YPOS1616;

  switch(TEXEL_DRAW_FUNC)
    {
    case TEXEL_DRAW_FUNC_LINE:
      xvert += (TEXTURE_HI_START * VDX1616);
      yvert += (TEXTURE_HI_START * VDY1616);
      /*
        if(SPRHI > TEXTURE_HI_LIM)
        SPRHI = TEXTURE_HI_LIM;
      */

      if(SPRWI > TEXTURE_WI_LIM)
        SPRWI = TEXTURE_WI_LIM;

      for(i = TEXTURE_HI_START; i < TEXTURE_HI_LIM; i++)
        {
          xcur   = (xvert + TEXTURE_WI_START * HDX1616);
          ycur   = (yvert + TEXTURE_WI_START * HDY1616);
          xvert += VDX1616;
          yvert += VDY1616;

          for(j = TEXTURE_WI_START; j < SPRWI; j++)
            {
              CURPIX = PDEC(opera_mem_read16((PDATA + XY2OFF(j,i,offset << 2))),&LAMV);

              if(!pproj.Transparent)
                {
                  uint32_t pixel;
                  uint32_t framePixel;

                  framePixel = opera_mem_read16((REGCTL2+XY2OFF(xcur >> 16,ycur>>16,MADAM.rmod)));

                  pixel = PPROC(CURPIX,framePixel,LAMV);
                  pixel = PPROJ_OUTPUT(pixel,framePixel);
                  opera_mem_write16((REGCTL3+XY2OFF(xcur >> 16,ycur >> 16,MADAM.wmod)),pixel);
                }

              xcur += HDX1616;
              ycur += HDY1616;
            }
        }
      break;
    case TEXEL_DRAW_FUNC_SCALE:
      {
        for(i = 0; i < SPRHI; i++)
          {
            xcur   = xvert;
            ycur   = yvert;
            xvert += VDX1616;
            yvert += VDY1616;

            for(j = 0; j < SPRWI; j++)
              {
                CURPIX = PDEC(opera_mem_read16((PDATA+XY2OFF(j,i,offset<<2))),&LAMV);

                if(!pproj.Transparent)
                  {
                    if(TexelDrawScale(CURPIX,
                                      LAMV,
                                      xcur >> 16,
                                      ycur >> 16,
                                      ((xcur+HDX1616+VDX1616)>>16),
                                      ((ycur+HDY1616+VDY1616)>>16)))
                      break;
                  }

                xcur += HDX1616;
                ycur += HDY1616;
              }
          }
      }
      break;
    case TEXEL_DRAW_FUNC_ARBITRARY:
    default:
      for(i = 0; i < SPRHI; i++)
        {
          g_row_top = ROW_TOP(i);
          xcur     = xvert;
          ycur     = yvert;
          xvert   += VDX1616;
          yvert   += VDY1616;
          xdown    = xvert;
          ydown    = yvert;
          hdx      = HDX1616;
          hdy      = HDY1616;
          HDX1616 += HDDX1616;
          HDY1616 += HDDY1616;

          for(j = 0; j < SPRWI; j++)
            {
              CURPIX = PDEC(opera_mem_read16((PDATA+XY2OFF(j,i,offset<<2))),&LAMV);

              if(!pproj.Transparent)
                {
                  if(TexelDrawArbitrary(CURPIX,
                                        LAMV,
                                        xcur,
                                        ycur,
                                        xcur + hdx,
                                        ycur + hdy,
                                        xdown + HDX1616,
                                        ydown + HDY1616,
                                        xdown,
                                        ydown))
                    break;
                }

              xcur  += hdx;
              ycur  += hdy;
              xdown += HDX1616;
              ydown += HDY1616;
            }
        }
      break;
    }

  StoreXYPosFromDrawHeight(origin_x,origin_y,origin_vdx,origin_vdy,SPRHI);
}

static
INLINE
uint32_t
TexelCCWTest(int64_t hdx, int64_t hdy, int64_t vdx, int64_t vdy)
{
  if (((hdx + vdx) * (hdy - vdy) + vdx * vdy - hdx * hdy) < 0)
    return CCB_ACCW;
  return CCB_ACW;
}

static
bool
QuadCCWTest(int32_t wdt_)
{
  float wdt;
  uint32_t tmp;

  if(flag_is_set(CCBFLAGS,CCB_ACCW) && flag_is_set(CCBFLAGS,CCB_ACW))
    return false;

  wdt = (float)wdt_;
  tmp = TexelCCWTest(HDX1616, HDY1616, VDX1616, VDY1616);
  if (tmp != TexelCCWTest(HDX1616, HDY1616, VDX1616 + HDDX1616*wdt, VDY1616 + HDDY1616*wdt))
    return false;
  if (tmp != TexelCCWTest(HDX1616 + HDDX1616*SPRHI, HDY1616 + HDDY1616*SPRHI, VDX1616, VDY1616))
    return false;
  if (tmp != TexelCCWTest(HDX1616 + HDDX1616*SPRHI, HDY1616 + HDDY1616*SPRHI, VDX1616 + HDDX1616*SPRHI * wdt, VDY1616 + HDDY1616*SPRHI * wdt))
    return false;
  if(tmp == (CCBFLAGS & (CCB_ACCW | CCB_ACW)))
    return true;
  return false;
}

static
OPERA_FORCEINLINE
int32_t
ABS(const int32_t val_)
{
  return ((val_ > 0) ? val_ : -val_);
}

static
bool
TestInitVisual(bool const packed_)
{
  int32_t xpoints[4];
  int32_t ypoints[4];

  if(flag_is_clr(CCBFLAGS,CCB_ACCW) && flag_is_clr(CCBFLAGS,CCB_ACW))
    return true;

  if(packed_)
    {
      xpoints[0] = (XPOS1616 >> 16);
      xpoints[1] = ((XPOS1616 + VDX1616 * SPRHI) >> 16);
      if((xpoints[0] < 0) &&
         (xpoints[1] < 0) &&
         (HDX1616   <= 0) &&
         (HDDX1616  <= 0))
        return true;
      if((xpoints[0] > MADAM.clipx) &&
         (xpoints[1] > MADAM.clipx) &&
         (HDX1616   >= 0)           &&
         (HDDX1616  >= 0))
        return true;

      ypoints[0] = (YPOS1616 >> 16);
      ypoints[1] = ((YPOS1616 + VDY1616 * SPRHI) >> 16);
      if((ypoints[0] < 0) &&
         (ypoints[1] < 0) &&
         (HDY1616   <= 0) &&
         (HDDY1616  <= 0))
        return true;
      if((ypoints[0] > MADAM.clipy) &&
         (ypoints[1] > MADAM.clipy) &&
         (HDY1616   >= 0)           &&
         (HDDY1616  >= 0))
        return true;
    }
  else
    {
      xpoints[0] = (XPOS1616 >> 16);
      xpoints[1] = (XPOS1616+HDX1616*SPRWI)>>16;
      xpoints[2] = (XPOS1616+VDX1616*SPRHI)>>16;
      xpoints[3] = ((XPOS1616+VDX1616*SPRHI+
                     (HDX1616+HDDX1616*SPRHI)*SPRWI) >> 16);
      if((xpoints[0] < 0) &&
         (xpoints[1] < 0) &&
         (xpoints[2] < 0) &&
         (xpoints[3] < 0))
        return true;
      if((xpoints[0] > MADAM.clipx) &&
         (xpoints[1] > MADAM.clipx) &&
         (xpoints[2] > MADAM.clipx) &&
         (xpoints[3] > MADAM.clipx))
        return true;

      ypoints[0] = (YPOS1616 >> 16);
      ypoints[1] = ((YPOS1616+HDY1616*SPRWI) >> 16);
      ypoints[2] = ((YPOS1616+VDY1616*SPRHI) >> 16);
      ypoints[3] = ((YPOS1616+VDY1616*SPRHI+
                     (HDY1616+HDDY1616*SPRHI)*SPRWI) >> 16);
      if((ypoints[0] < 0) &&
         (ypoints[1] < 0) &&
         (ypoints[2] < 0) &&
         (ypoints[3] < 0))
        return true;
      if((ypoints[0] > MADAM.clipy) &&
         (ypoints[1] > MADAM.clipy) &&
         (ypoints[2] > MADAM.clipy) &&
         (ypoints[3] > MADAM.clipy))
        return true;
    }

  if((HDDX1616 == 0) && (HDDY1616 == 0))
    {
      if((HDX1616 == 0) && (VDY1616 == 0))
        {
          if(((HDY1616 < 0) && (VDX1616 > 0)) ||
             ((HDY1616 > 0) && (VDX1616 < 0)))
            {
              if(flag_is_set(CCBFLAGS,CCB_ACW))
                {
                  if((ABS(HDY1616) == 0x10000) &&
                     (ABS(VDX1616) == 0x10000) &&
                     !((YPOS1616|XPOS1616)&0xffff))
                    {
                      return InitLineMap();
                    }
                  else
                    {
                      InitScaleMap();
                      return false;
                    }
                }
            }
          else
            {
              if(flag_is_set(CCBFLAGS,CCB_ACCW))
                {
                  if((ABS(HDY1616) == 0x10000) &&
                     (ABS(VDX1616) == 0x10000) &&
                     !((YPOS1616|XPOS1616)&0xffff))
                    {
                      return InitLineMap();
                    }
                  else
                    {
                      InitScaleMap();
                      return false;
                    }
                }
            }

          return true;
        }
      else if((HDY1616 == 0) && (VDX1616 == 0))
        {
          if(((HDX1616 < 0) && (VDY1616 > 0)) ||
             ((HDX1616 > 0) && (VDY1616 < 0)))
            {
              if(flag_is_set(CCBFLAGS,CCB_ACCW))
                {
                  if((ABS(HDX1616) == 0x10000) &&
                     (ABS(VDY1616) == 0x10000) &&
                     !((YPOS1616|XPOS1616)&0xffff))
                    {
                      return InitLineMap();
                    }
                  else
                    {
                      InitScaleMap();
                      return false;
                    }
                }
            }
          else
            {
              if(flag_is_set(CCBFLAGS,CCB_ACW))
                {
                  if((ABS(HDX1616) == 0x10000) &&
                     (ABS(VDY1616) == 0x10000) &&
                     !((YPOS1616|XPOS1616)&0xffff))
                    {
                      return InitLineMap();
                    }
                  else
                    {
                      InitScaleMap();
                      return false;
                    }
                }
            }

          return true;
        }
    }

  if(QuadCCWTest(packed_ ? 2048 : SPRWI))
    return true;

  InitArbitraryMap();

  return false;
}

static
bool
InitLineMap(void)
{
  TEXEL_DRAW_FUNC  = TEXEL_DRAW_FUNC_LINE;
  TEXTURE_WI_START = 0;
  TEXTURE_HI_START = 0;
  TEXTURE_HI_LIM   = SPRHI;

  if((HDX1616 < 0) || (VDX1616 < 0))
    XPOS1616 -= 0x8000;
  if((HDY1616 < 0) || (VDY1616 < 0))
    YPOS1616 -= 0x8000;

  if(VDX1616 < 0)
    {
      if(((XPOS1616 - ((SPRHI - 1) << 16)) >> 16) < 0)
        TEXTURE_HI_LIM = ((XPOS1616 >> 16) + 1);
      if(TEXTURE_HI_LIM > SPRHI)
        TEXTURE_HI_LIM = SPRHI;
    }
  else if(VDX1616 > 0)
    {
      if(((XPOS1616 + (SPRHI << 16)) >> 16) > MADAM.clipx)
        TEXTURE_HI_LIM = (MADAM.clipx - (XPOS1616>>16) + 1);
    }

  if(VDY1616 < 0)
    {
      if(((YPOS1616 - ((SPRHI - 1) << 16)) >> 16) < 0)
        TEXTURE_HI_LIM = ((YPOS1616 >> 16) + 1);
      if(TEXTURE_HI_LIM > SPRHI)
        TEXTURE_HI_LIM = SPRHI;
    }
  else if(VDY1616 > 0)
    {
      if(((YPOS1616 + (SPRHI << 16)) >> 16) > MADAM.clipy)
        TEXTURE_HI_LIM = (MADAM.clipy - (YPOS1616 >> 16) + 1);
    }

  if(HDX1616 < 0)
    TEXTURE_WI_LIM = ((XPOS1616 >> 16) + 1);
  else if(HDX1616 > 0)
    TEXTURE_WI_LIM = (MADAM.clipx - (XPOS1616 >> 16) + 1);

  if(HDY1616 < 0)
    TEXTURE_WI_LIM = ((YPOS1616 >> 16) + 1);
  else if(HDY1616 > 0)
    TEXTURE_WI_LIM = (MADAM.clipy - (YPOS1616 >> 16) + 1);

  if(XPOS1616 < 0)
    {
      if(HDX1616 < 0)
        return true;
      else if(HDX1616 > 0)
        TEXTURE_WI_START = -(XPOS1616 >> 16);

      if(VDX1616 < 0)
        return true;
      else if(VDX1616 > 0)
        TEXTURE_HI_START = -(XPOS1616 >> 16);
    }
  else if((XPOS1616 >> 16) > MADAM.clipx)
    {
      if(HDX1616 > 0)
        return true;
      else if(HDX1616 < 0)
        TEXTURE_WI_START = ((XPOS1616 >> 16) - MADAM.clipx);

      if(VDX1616 > 0)
        return true;
      else if(VDX1616 < 0)
        TEXTURE_HI_START = ((XPOS1616 >> 16) - MADAM.clipx);
    }

  if(YPOS1616 < 0)
    {
      if(HDY1616 < 0)
        return true;
      else if(HDY1616 > 0)
        TEXTURE_WI_START = -(YPOS1616 >> 16);

      if(VDY1616 < 0)
        return true;
      else if(VDY1616 > 0)
        TEXTURE_HI_START = -(YPOS1616 >> 16);
    }
  else if((YPOS1616 >> 16) > MADAM.clipy)
    {
      if(HDY1616 > 0)
        return true;
      else if(HDY1616 < 0)
        TEXTURE_WI_START = ((YPOS1616 >> 16) - MADAM.clipy);

      if(VDY1616 > 0)
        return true;
      else if(VDY1616 < 0)
        TEXTURE_HI_START = ((YPOS1616 >> 16) - MADAM.clipy);
    }

  /*
    if(TEXTURE_WI_START<((PRE0>>24)&0xf))
    TEXTURE_WI_START=((PRE0>>24)&0xf);
    TEXTURE_WI_START+=(PRE0>>24)&0xf;
    if(TEXTURE_WI_START<0)TEXTURE_WI_START=0;
    if(TEXTURE_HI_START<0)TEXTURE_HI_START=0;
    if(TEXTURE_HI_LIM>SPRHI)TEXTURE_HI_LIM=SPRHI;
  */

  if(TEXTURE_WI_LIM <= 0)
    return true;

  return false;
}

static
INLINE
void
InitScaleMap(void)
{
  int32_t deltax;
  int32_t deltay;

  TEXEL_DRAW_FUNC = TEXEL_DRAW_FUNC_SCALE;

  if((HDX1616 < 0) || (VDX1616 < 0))
    XPOS1616 -= 0x8000;
  if((HDY1616 < 0) || (VDY1616 < 0))
    YPOS1616 -= 0x8000;

  deltax = (HDX1616 + VDX1616);
  deltay = (HDY1616 + VDY1616);

  TEXEL_INCX = ((deltax < 0) ? -1 : 1);
  TEXEL_INCY = ((deltay < 0) ? -1 : 1);

  TEXTURE_WI_START = 0;
  TEXTURE_HI_START = 0;
}

static
INLINE
void
InitArbitraryMap(void)
{
  TEXEL_DRAW_FUNC  = TEXEL_DRAW_FUNC_ARBITRARY;
  TEXTURE_WI_START = 0;
  TEXTURE_HI_START = 0;
}

static
void
TexelDrawLine(uint16_t CURPIX_,
              uint16_t LAMV_,
              int32_t  xcur_,
              int32_t  ycur_,
              int32_t  cnt_)
{
  int32_t i;
  uint32_t curr;
  uint32_t pixel;

  xcur_ >>= 16;
  ycur_ >>= 16;
  curr = 0xFFFFFFFF;

  for(i = 0; i < cnt_; i++, xcur_ += (HDX1616 >> 16), ycur_ += (HDY1616 >> 16))
    {
      uint32_t next;

      next = opera_mem_read16(REGCTL2 + XY2OFF(xcur_,ycur_,MADAM.rmod));
      if(next != curr)
        {
          curr  = next;
          pixel = PPROC(CURPIX_,next,LAMV_);
          pixel = PPROJ_OUTPUT(pixel,next);
        }

      opera_mem_write16(REGCTL3 + XY2OFF(xcur_,ycur_,MADAM.wmod),pixel);
    }
}

/* Diagnostico HiRes (so registra; nao muda o desenho): quads 3D com altura <= 1 linha em 2x. */
#define THIN_RING 256
static int g_probe_y2 = -1;
void opera_cel_probe_row(int y2) { g_probe_y2 = y2; }
static cel_thin_t g_thin[THIN_RING];
static unsigned g_thin_n, g_thin_rd;
/* Sonda de escrita: pixel 1x x=20, linhas 2x y2-6..y2+7 (cobre diferenca de base entre a linha do VDLP e o buffer do CEL). */
static void watch_note(int path, int32_t x, int32_t y, uint16_t pix) {
  cel_thin_t *t = &g_thin[g_thin_n++ % THIN_RING];
  memset(t, 0, sizeof *t);
  t->x[0] = x; t->y[0] = y; t->x[1] = path; t->x[2] = pix;
  t->ccb = CURRENTCCB; t->flags = (uint32_t)CCBFLAGS; t->regctl3 = REGCTL3; t->kind = 2;
}
static void watch_1x(uint32_t a, uint16_t v) {
  int32_t yy;
  if(!HIRESMODE || g_probe_y2 < 0)
    return;
  for(yy = (g_probe_y2 >> 1) - 3; yy <= (g_probe_y2 >> 1) + 3; yy++)
    if(a == REGCTL3 + XY2OFF(20, yy, MADAM.wmod))
      watch_note(2, 20, yy * 2, v);
}

/* ---- Diagnostico v6: cobertura por linha 2x (so conta com opera_cel_cov_enable(1)) ---- */
#define COV_SLOTS 9
#define COV_ROWS  512
#define COV_BK    20
#define COV_QRING 32768
typedef struct { int32_t x[4], y[4]; int16_t slot; } cov_q_t;
static int g_cov_on;
static uint32_t g_cov_ccb[COV_SLOTS], g_cov_dst[COV_SLOTS];
static uint16_t g_cov[COV_SLOTS][COV_ROWS][COV_BK];
static cov_q_t g_cov_q[COV_QRING];
static unsigned g_cov_qn;
void opera_cel_cov_enable(int on) { g_cov_on = on; }
static int cov_slot(void) {
  int s;
  uint32_t c = (uint32_t)CURRENTCCB;
  for(s = 1; s < COV_SLOTS; s++)
    {
      if(g_cov_ccb[s] == c) return s;
      if(g_cov_ccb[s] == 0) { g_cov_ccb[s] = c; g_cov_dst[s] = (uint32_t)REGCTL3; return s; }
    }
  return 0;
}
static void cov_note(int32_t x, int32_t y) {
  int s, b;
  if(y < 0 || y >= COV_ROWS || x < 0) return;
  b = x >> 5; if(b >= COV_BK) b = COV_BK - 1;
  if(g_cov[0][y][b] < 65535) g_cov[0][y][b]++;
  s = cov_slot();
  if(s && g_cov[s][y][b] < 65535) g_cov[s][y][b]++;
}
static void cov_quad(int32_t xa, int32_t ya, int32_t xb, int32_t yb, int32_t xc, int32_t yc, int32_t xd, int32_t yd) {
  cov_q_t *q = &g_cov_q[g_cov_qn++ % COV_QRING];
  q->x[0] = xa; q->x[1] = xb; q->x[2] = xc; q->x[3] = xd;
  q->y[0] = ya; q->y[1] = yb; q->y[2] = yc; q->y[3] = yd;
  q->slot = (int16_t)cov_slot();
}
unsigned opera_cel_cov_report(cel_hole_t *out, unsigned max) {
  unsigned n = 0; int s, r, b;
  for(s = 0; s < COV_SLOTS && n < max; s++)
    {
      unsigned per = 0, tot = 0;
      if(s && !g_cov_ccb[s]) continue;
      for(r = 0; r < COV_ROWS; r++) for(b = 0; b < COV_BK; b++) tot += g_cov[s][r][b];
      if(tot < 500) continue;
      for(r = 1; r < COV_ROWS - 1 && per < (s ? 3u : 6u) && n < max; r++)
        {
          int nb = 0, b0 = -1;
          for(b = 0; b < COV_BK; b++)
            if(g_cov[s][r][b] == 0 && g_cov[s][r-1][b] >= 8 && g_cov[s][r+1][b] >= 8) { if(b0 < 0) b0 = b; nb++; }
          if(nb)
            {
              cel_hole_t *h = &out[n++];
              unsigned i, k = g_cov_qn < COV_QRING ? g_cov_qn : COV_QRING;
              memset(h, 0, sizeof *h);
              h->ccb = s ? g_cov_ccb[s] : 0; h->dst = s ? g_cov_dst[s] : 0; h->row = r; h->nbk = nb; h->bk0 = b0;
              for(i = 0; i < k && h->nq < 6; i++)
                {
                  const cov_q_t *q = &g_cov_q[(g_cov_qn - 1 - i) % COV_QRING];
                  int32_t mn = q->y[0], mx = q->y[0]; int j;
                  if(s && q->slot != s) continue;
                  for(j = 1; j < 4; j++) { if(q->y[j] < mn) mn = q->y[j]; if(q->y[j] > mx) mx = q->y[j]; }
                  if(mn <= r + 1 && mx >= r - 1)
                    {
                      for(j = 0; j < 4; j++) { h->q[h->nq][j] = q->x[j]; h->q[h->nq][4 + j] = q->y[j]; }
                      h->nq++;
                    }
                }
              per++;
            }
        }
    }
  memset(g_cov, 0, sizeof g_cov); memset(g_cov_ccb, 0, sizeof g_cov_ccb); memset(g_cov_dst, 0, sizeof g_cov_dst); g_cov_qn = 0;
  return n;
}
unsigned opera_cel_thin_pop(cel_thin_t *out, unsigned max) {
  unsigned n = 0;
  if (g_thin_n - g_thin_rd > THIN_RING) g_thin_rd = g_thin_n - THIN_RING;
  while (g_thin_rd != g_thin_n && n < max) out[n++] = g_thin[g_thin_rd++ % THIN_RING];
  return n;
}

static
INLINE
uint16_t
readPIX(int32_t x_,
        int32_t y_)
{
  uint32_t src = REGCTL2;

  if(HIRESMODE)
    {
      const int q = ((y_ & 1) << 1) + (x_ & 1);
      uint32_t o;
      src += XY2OFF(x_ >> 1,y_ >> 1,MADAM.rmod);
      /* Phoenix: os planos 1..3 espelham a VRAM (1 MB cada). Leitura fora da VRAM (ex.: textura/buffer na DRAM) cai no plano 0. */
      if(q && hr_off(src,&o))
        return hr_read16(q,o);
    }
  else
    {
      src += XY2OFF(x_,y_,MADAM.rmod);
    }

  return opera_mem_read16(src);
}

static
INLINE
void
writePIX(int32_t  x_,
         int32_t  y_,
         uint16_t p_)
{
  uint32_t src = REGCTL3;

  if(HIRESMODE)
    {
      const int q = ((y_ & 1) << 1) + (x_ & 1);
      uint32_t o;
      if(g_probe_y2 >= 0 && (x_ >> 1) == 20 && y_ >= g_probe_y2 - 6 && y_ <= g_probe_y2 + 7)
        watch_note(1, x_, y_, p_);
      src += XY2OFF(x_ >> 1,y_ >> 1,MADAM.wmod);
      if(g_cov_on && hr_off(src,&o))
        cov_note(x_, y_);
      if(q)
        {
          /* Phoenix: planos 1..3 = copias da VRAM mantidas pelo nucleo (o Opera usa src + q*VRAM_SIZE, alem da RAM) */
          if(hr_off(src,&o))
            hr_write16(q,o,p_);
          return;
        }
    }
  else
    {
      src += XY2OFF(x_,y_,MADAM.wmod);
    }

  opera_mem_write16_base(src,p_);
}

static
int32_t
TexelDrawScale(uint16_t CURPIX_,
               uint16_t LAMV_,
               int32_t  xcur_,
               int32_t  ycur_,
               int32_t  deltax_,
               int32_t  deltay_)
{
  int32_t x;
  int32_t y;

  if((HDX1616 < 0) && (deltax_ < 0) && (xcur_ < 0))
    return -1;
  else if((HDY1616 < 0) && (deltay_ < 0) && (ycur_ < 0))
    return -1;
  else if((HDX1616 > 0) && (deltax_ > MADAM.clipx) && (xcur_ > MADAM.clipx))
    return -1;
  else if((HDY1616 > 0) && (deltay_ > MADAM.clipy) && (ycur_ > MADAM.clipy))
    return -1;

  if(xcur_ == deltax_)
    return 0;

  if((TEXEL_INCX > 0 ? deltax_ < xcur_ : deltax_ > xcur_) ||
     (TEXEL_INCY > 0 ? deltay_ < ycur_ : deltay_ > ycur_))
    return 0;   /* anti-laco (Phoenix): sentido incoerente so ocorre com dados lixo */

  for(y = ycur_; y != deltay_; y += TEXEL_INCY)
    {
      for(x = xcur_; x != deltax_; x += TEXEL_INCX)
        {
          if(clipping(x,y))
            continue;

          process_pixel(x,y,CURPIX_,LAMV_);
          if(flag_is_set(CCBFLAGS,CCB_MARIA))
            return 0;
        }
    }

  return 0;
}

/* Correcao da "emenda" do HiRes (telhado abrindo e mostrando o fundo): cada texel e um quad cobrindo as linhas 2x [miny,maxy).
 * Dois texels vizinhos cujas bordas diferem por menos de 1 linha nativa caem na mesma linha em 1x (sem buraco), mas em 2x
 * a de cima pode terminar numa linha PAR (maxy par, exclusivo) e a de baixo comecar numa IMPAR, deixando a linha par sem
 * ninguem (o fundo aparece so nas linhas pares, como visto nos planos 0/1 vs 2/3). Aqui o quad pequeno (1..3 linhas 2x) com
 * maxy par ganha UMA linha extra embaixo (repete a ultima linha); o proximo texel, desenhado depois, sobrescreve se for dono dela.
 * Desligue com -DPH_HIRES_SEAMFIX=0. */
#ifndef PH_HIRES_SEAMFIX
#define PH_HIRES_SEAMFIX 0   /* v5: nao resolveu (os dumps mostraram outra causa); mantido so por compatibilidade */
#endif
/* v7 - FECHA O BURACO DO TELHADO. Causa (dump 25/10, CCB do telhado em y=170,6; linha 2x 340 = linha nativa 170 par):
 * em 1x um CEL cobre as linhas nativas [floor(topo), floor(base)), entao o topo y=170,6 comeca na linha nativa 170 inteira (2x: 340 e 341).
 * Em 2x o topo trunca para 341 (impar) e a linha 340 nao tem dono: o CEL de cima termina antes e o fundo (ceu) aparece numa unica linha 2x.
 * Correcao: nos texels da fileira de TOPO do CEL cujo miny 2x e impar, a primeira linha desenhada passa a ser miny-1 (usa as bordas de miny),
 * exatamente a linha que o 1x cobriria. Desligue com -DPH_HIRES_TOPFIX=0. */
#ifndef PH_HIRES_TOPFIX
#define PH_HIRES_TOPFIX 1
#endif
int opera_cel_topfix(void) { return PH_HIRES_TOPFIX; }   /* 1 se o conserto do topo do telhado (v7) foi compilado; aparece no log de opcoes */
static
int32_t
TexelDrawArbitrary(uint16_t CURPIX_,
                   uint16_t LAMV_,
                   int32_t  xA_,
                   int32_t  yA_,
                   int32_t  xB_,
                   int32_t  yB_,
                   int32_t  xC_,
                   int32_t  yC_,
                   int32_t  xD_,
                   int32_t  yD_)
{
  int32_t x;
  int32_t y;
  int32_t yd;
  int32_t clipyt;
  int32_t miny;
  int32_t maxy;
  int32_t maxx;
  int32_t maxxt;
  int32_t maxyt;
  int32_t tmp;
  uint32_t curr;
  uint32_t next;
  uint32_t pixel;
  int32_t xpoints[4];
  int32_t updowns[4];

  curr = -1;
  xA_ >>= (16 - HIRESMODE);
  xB_ >>= (16 - HIRESMODE);
  xC_ >>= (16 - HIRESMODE);
  xD_ >>= (16 - HIRESMODE);
  yA_ >>= (16 - HIRESMODE);
  yB_ >>= (16 - HIRESMODE);
  yC_ >>= (16 - HIRESMODE);
  yD_ >>= (16 - HIRESMODE);

  if((xA_ == xB_) && (xB_ == xC_) && (xC_ == xD_))
    return 0;

  maxxt = ((MADAM.clipx + 1) << HIRESMODE);
  maxyt = ((MADAM.clipy + 1) << HIRESMODE);

  if((HDX1616 < 0) && (HDDX1616 < 0))
    {
      if((xA_ < 0) && (xB_ < 0) && (xC_ < 0) && (xD_ < 0))
        return -1;
    }

  if((HDX1616 > 0) && (HDDX1616 > 0))
    {
      if((xA_ >= maxxt) && (xB_ >= maxxt) && (xC_ >= maxxt) && (xD_ >= maxxt))
        return -1;
    }

  if((HDY1616 < 0) && (HDDY1616 < 0))
    {
      if((yA_ < 0) && (yB_ < 0) && (yC_ < 0) && (yD_ < 0))
        return -1;
    }

  if((HDY1616 > 0) && (HDDY1616 > 0))
    {
      if((yA_ >= maxyt) && (yB_ >= maxyt) && (yC_ >= maxyt) && (yD_ >= maxyt))
        return -1;
    }

  miny = maxy = yA_;

  if(miny > yB_)
    miny = yB_;
  if(miny > yC_)
    miny = yC_;
  if(miny > yD_)
    miny = yD_;
  if(maxy < yB_)
    maxy = yB_;
  if(maxy < yC_)
    maxy = yC_;
  if(maxy < yD_)
    maxy = yD_;

  if(HIRESMODE && g_cov_on)
    cov_quad(xA_, yA_, xB_, yB_, xC_, yC_, xD_, yD_);

  if(HIRESMODE)
    {
      int32_t mnx = xA_, mxx = xA_;
      int kind = -1;
      if(xB_ < mnx)
        mnx = xB_;
      if(xB_ > mxx)
        mxx = xB_;
      if(xC_ < mnx)
        mnx = xC_;
      if(xC_ > mxx)
        mxx = xC_;
      if(xD_ < mnx)
        mnx = xD_;
      if(xD_ > mxx)
        mxx = xD_;
      if(((maxy - miny) <= 1) && ((mxx - mnx) >= 8))
        kind = 0;
      else if((g_probe_y2 >= 0) && (miny <= g_probe_y2 + 2) && (maxy >= g_probe_y2 - 2) && ((mxx - mnx) >= 48))
        kind = 1;
      if(kind >= 0)
        {
          cel_thin_t *t = &g_thin[g_thin_n++ % THIN_RING];
          t->x[0] = xA_; t->x[1] = xB_; t->x[2] = xC_; t->x[3] = xD_;
          t->y[0] = yA_; t->y[1] = yB_; t->y[2] = yC_; t->y[3] = yD_;
          t->ccb = CURRENTCCB; t->flags = (uint32_t)CCBFLAGS; t->regctl3 = REGCTL3;
          t->w = mxx - mnx; t->h = maxy - miny; t->kind = kind;
        }
    }

  y = miny;
#if PH_HIRES_TOPFIX
  if(HIRESMODE && g_row_top && (miny & 1) && miny > 0 && maxy > miny)
    y = miny - 1;       /* linha que o 1x cobriria (ver comentario acima) */
#endif
  if(y < 0)
    y = 0;
  clipyt = maxyt;
  if(maxy < maxyt)
    maxyt = maxy;
#if PH_HIRES_SEAMFIX
  if(HIRESMODE && (maxy - miny) >= 1 && (maxy - miny) <= 3 && !(maxy & 1) && maxyt == maxy && (maxy + 1) <= clipyt)
    maxyt = maxy + 1;   /* linha extra (ver comentario acima) */
#endif

  for(yd = y; yd < maxyt; yd++)
    {
      int cnt_cross = 0;
      y = (yd >= maxy) ? (maxy - 1) : ((yd < miny) ? miny : yd);   /* linhas extras (topo/base): usam as bordas da linha vizinha do quad */
      if((y < yB_) && (y >= yA_))
        {
          xpoints[cnt_cross] = (int32_t)((((xB_-xA_)*(y-yA_))/(yB_-yA_))+xA_);
          updowns[cnt_cross++] = 1;
        }
      else if((y >= yB_) && (y < yA_))
        {
          xpoints[cnt_cross] = (int32_t)((((xA_-xB_)*(y-yB_))/(yA_-yB_))+xB_);
          updowns[cnt_cross++] = 0;
        }

      if((y < yC_) && (y >= yB_))
        {
          xpoints[cnt_cross] = (int32_t)((((xC_-xB_)*(y-yB_))/(yC_-yB_))+xB_);
          updowns[cnt_cross++] = 1;
        }
      else if((y >= yC_) && (y < yB_))
        {
          xpoints[cnt_cross] = (int32_t)((((xB_-xC_)*(y-yC_))/(yB_-yC_))+xC_);
          updowns[cnt_cross++] = 0;
        }

      if((y < yD_) && (y >= yC_))
        {
          xpoints[cnt_cross] = (int32_t)((((xD_-xC_)*(y-yC_))/(yD_-yC_))+xC_);
          updowns[cnt_cross++] = 1;
        }
      else if((y >= yD_) && (y < yC_))
        {
          xpoints[cnt_cross] = (int32_t)((((xC_-xD_)*(y-yD_))/(yC_-yD_))+xD_);
          updowns[cnt_cross++] = 0;
        }

      if(cnt_cross & 1)
        {
          if((y < yA_) && (y >= yD_))
            {
              xpoints[cnt_cross] = (int32_t)((((xA_-xD_)*(y-yD_))/(yA_-yD_))+xD_);
              updowns[cnt_cross] = 1;
            }
          else if((y >= yA_) && (y < yD_))
            {
              xpoints[cnt_cross] = (int32_t)((((xD_-xA_)*(y-yA_))/(yD_-yA_))+xA_);
              updowns[cnt_cross] = 0;
            }
        }

      if(cnt_cross != 0)
        {
          if(xpoints[0] > xpoints[1])
            {
              xpoints[1] += xpoints[0];
              xpoints[0]  = xpoints[1] - xpoints[0];
              xpoints[1]  = xpoints[1] - xpoints[0];

              tmp        = updowns[0];
              updowns[0] = updowns[1];
              updowns[1] = tmp;
            }

          if(cnt_cross > 2)
            {
              if((flag_is_set(CCBFLAGS,CCB_ACW)  && (updowns[2] == 0)) ||
                 (flag_is_set(CCBFLAGS,CCB_ACCW) && (updowns[2] == 1)))
                {
                  x = xpoints[2];
                  if(x < 0)
                    x = 0;

                  maxx = xpoints[3] + 1;
                  if(maxx > maxxt)
                    maxx = maxxt;

                  for(; x < maxx; x++)
                    {
                      next = readPIX(x,yd);
                      if(next != curr)
                        {
                          curr  = next;
                          pixel = PPROC(CURPIX_,next,LAMV_);
                          pixel = PPROJ_OUTPUT(pixel,next);
                        }
                      writePIX(x,yd,pixel);
                      if(flag_is_set(CCBFLAGS,CCB_MARIA))
                        return 0;
                    }
                }
            }

          if((flag_is_set(CCBFLAGS,CCB_ACW)  && (updowns[0] == 0)) ||
             (flag_is_set(CCBFLAGS,CCB_ACCW) && (updowns[0] == 1)))
            {
              x = xpoints[0];
              if(x < 0)
                x = 0;

              maxx = xpoints[1] + 1;
              if(maxx > maxxt)
                maxx = maxxt;

              for(; x < maxx; x++)
                {
                  next = readPIX(x,yd);
                  if(next != curr)
                    {
                      curr  = next;
                      pixel = PPROC(CURPIX_,next,LAMV_);
                      pixel = PPROJ_OUTPUT(pixel,next);
                    }
                  writePIX(x,yd,pixel);
                  if(flag_is_set(CCBFLAGS,CCB_MARIA))
                    return 0;
                }
            }
        }
    }

  return 0;
}


/* ===================================================================================================================
 * Cola com o Phoenix (escrita aqui; nao vem do Opera)
 * =================================================================================================================== */
static opera_cel_t *G_OP;     /* contexto da execucao em curso (so para as estatisticas) */

static void cel_opera_stat(void) {
  opera_cel_t *o = G_OP;
  uint32_t bpp = PRE0 & PRE0_BPP_MASK;
  o->n_drawn++;
  o->bpp_n[bpp]++;
  if (CCBFLAGS & CCB_PACKED) o->n_packed++; else o->n_unpacked++;
  if (!(CCBFLAGS & CCB_PACKED) && (PRE1 & PRE1_LRFORM) && BPP[bpp] == 16) o->n_lrform++;
  if (HDDX1616 || HDDY1616) o->n_hdd++;
  if (!((HDX1616 == 0x10000 || HDX1616 == -0x10000 || HDX1616 == 0) && (VDY1616 == 0x10000 || VDY1616 == -0x10000 || VDY1616 == 0) &&
        HDDX1616 == 0 && HDDY1616 == 0)) o->n_scaled++;
  o->flags_seen |= CCBFLAGS; o->pre0_seen |= PRE0; o->pre1_seen |= PRE1;
  if ((PIXC & 0xffffu) != 0x1f00u || (PIXC >> 16) != 0x1f00u) o->ppmp_nonid++;
  if (CECONTROL) o->pixc_nonzero++;
}

static int g_tables_ready;
static void cel_opera_init_tables(void) {
  int32_t i, j, n;
  if (g_tables_ready) return;
  g_tables_ready = 1;
  for (i = 0; i < 32; i++)
    for (j = 0; j < 8; j++)
      for (n = 0; n < 4; n++)
        PSCALAR[j][n][i] = ((i * (j + 1)) >> PDV(n));
  for (i = 0; i < 256; i++) {
    pdeco_t pix1; pdeco_t pix2; uint16_t pres; uint16_t resamv;
    pix1.raw = i;
    pix2.r16b.b = (pix1.u8b.b << 3) + (pix1.u8b.b << 1) + (pix1.u8b.b >> 1);
    pix2.r16b.g = (pix1.u8b.g << 2) + (pix1.u8b.g >> 1);
    pix2.r16b.r = (pix1.u8b.r << 2) + (pix1.u8b.r >> 1);
    pres = pix2.raw; pres &= 0x7fff;
    MAPu8b[i] = pres;
    resamv = ((pix1.c8b.m << 1) + pix1.c8b.mpw);
    resamv = ((resamv << 6) + (resamv << 3) + resamv);
    MAPc8bAMV[i] = resamv;
  }
  for (i = 0; i < (8*8*8); i++) {
    pdeco_t pix1;
    pix1.raw = i << 5;
    MAPc16bAMV[i] = ((pix1.c16b.mr << 6) + (pix1.c16b.mg << 3) + pix1.c16b.mb);
  }
}

/* Registradores do Phoenix (madam_t): cel_w[i] = registrador 0x100+4*i; dma_arr[0..3] = CURRENTCCB, NEXTCCB, PLUTDATA, PDATA (0x5a0..0x5ac).
 *   cel_w[4]=CECONTROL(0x110)  [8]=PIXC do CCB (LDPPMP; "ppmp" no Phoenix)  [12..15]=REGCTL0..3 (0x130..0x13c)
 *   cel_w[16..23]= x, y, vdx, vdy, hdx, hdy, hddx, hddy (formato do .exe: x/y/vdx/vdy 16.16, hdx/hdy/hddx/hddy 12.20 = valor do CCB)
 *   cel_w[0x20..0x3f] = PLUT (16 bits cada)
 * O estado que o Opera guarda em variaveis (HDX.., PIXC, PRE1) fica em opera_cel_t. Se a CPU gravar direto num desses registradores
 * (valor diferente do ultimo que escrevemos), o valor do registrador vence - igual ao .exe, que le o estado dos registradores a cada cadeia. */
#define OPC_NSH 9
static const int opc_sh_idx[OPC_NSH] = { 8, 16, 17, 18, 19, 20, 21, 22, 23 };

uint32_t opera_cel_run(opera_cel_t *o, uint32_t *dma_arr, uint32_t *cel_w, uint32_t *ram) {
  int i;
  cel_opera_init_tables();
  DRAM = (uint8_t *)ram;
  G_OP = o;
  memset(&MADAM, 0, sizeof MADAM);
  memset(&bitoper, 0, sizeof bitoper);
  bitoper.bitset = 1;
  MADAM.FSM = FSM_IDLE;

  /* ---- estado persistente: do contexto, a menos que a CPU tenha mexido nos registradores ---- */
  {
    int poked = !o->shadow_ok;
    for (i = 0; i < OPC_NSH && !poked; i++) if (cel_w[opc_sh_idx[i]] != o->shadow[i]) poked = 1;
    if (poked) {
      o->pixc = cel_w[8];
      o->xpos = (int32_t)cel_w[16]; o->ypos = (int32_t)cel_w[17];
      o->vdx = (int32_t)cel_w[18];  o->vdy = (int32_t)cel_w[19];
      o->hdx = ((int32_t)cel_w[20]) >> 4; o->hdy = ((int32_t)cel_w[21]) >> 4;
      o->hddx = ((int32_t)cel_w[22]) >> 4; o->hddy = ((int32_t)cel_w[23]) >> 4;
    }
  }
  PIXC = o->pixc; PRE1 = o->pre1;
  HDX1616 = o->hdx; HDY1616 = o->hdy; VDX1616 = o->vdx; VDY1616 = o->vdy; HDDX1616 = o->hddx; HDDY1616 = o->hddy;
  StoreXYPosToRegisters(o->xpos, o->ypos);

  CURRENTCCB = dma_arr[0]; NEXTCCB = dma_arr[1]; PLUTDATA = dma_arr[2]; PDATA = dma_arr[3];
  CECONTROL = cel_w[4];
  REGCTL0 = cel_w[12]; REGCTL1 = cel_w[13]; REGCTL2 = cel_w[14]; REGCTL3 = cel_w[15];
  HIRESMODE = (HRP[0] && REGCTL3 >= VRAM_BASE && REGCTL3 < VRAM_BASE + VRAM_SIZE) ? 1 : 0;   /* Phoenix: so quando se desenha na VRAM */
  MADAM.rmod = DecodeREGCTLMod(REGCTL0 & 0xFF);
  MADAM.wmod = DecodeREGCTLMod((REGCTL0 >> 8) & 0xFF);
  MADAM.clipx = (REGCTL1 >> 0) & 0x3FF;
  MADAM.clipy = (REGCTL1 >> 16) & 0x3FF;
  for (i = 0; i < MADAM_PLUT_COUNT; i++) MADAM.PLUT[i] = (uint16_t)cel_w[0x20 + i];

  cel_opera_handle();

  /* ---- devolve ao Phoenix ---- */
  dma_arr[0] = CURRENTCCB; dma_arr[1] = NEXTCCB; dma_arr[2] = PLUTDATA; dma_arr[3] = PDATA;
  for (i = 0; i < MADAM_PLUT_COUNT; i++) cel_w[0x20 + i] = MADAM.PLUT[i];
  o->pixc = PIXC; o->pre1 = PRE1;
  o->hdx = HDX1616; o->hdy = HDY1616; o->vdx = VDX1616; o->vdy = VDY1616; o->hddx = HDDX1616; o->hddy = HDDY1616;
  o->xpos = XPOS1616; o->ypos = YPOS1616;
  cel_w[8]  = o->pixc;
  cel_w[16] = (uint32_t)o->xpos; cel_w[17] = (uint32_t)o->ypos;
  cel_w[18] = (uint32_t)o->vdx;  cel_w[19] = (uint32_t)o->vdy;
  cel_w[20] = (uint32_t)o->hdx << 4; cel_w[21] = (uint32_t)o->hdy << 4;
  cel_w[22] = (uint32_t)o->hddx << 4; cel_w[23] = (uint32_t)o->hddy << 4;
  for (i = 0; i < OPC_NSH; i++) o->shadow[i] = cel_w[opc_sh_idx[i]];
  o->shadow_ok = 1;
  o->runs++;
  G_OP = NULL;
  return 0;
}

/* Liga/desliga o HiRes CEL Rendering. p1..p3 = planos extras (1 MB cada, mesmo leiaute de palavras da RAM do Phoenix); NULL desliga. */
void opera_cel_set_hires(uint32_t *p1, uint32_t *p2, uint32_t *p3)
{
  HRP[0] = p1; HRP[1] = p2; HRP[2] = p3;
  if(!p1 || !p2 || !p3) { HRP[0] = HRP[1] = HRP[2] = NULL; HIRESMODE = 0; }
}
