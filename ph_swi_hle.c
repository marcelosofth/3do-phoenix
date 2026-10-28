/* ph_swi_hle.c - ver ph_swi_hle.h. Convencao de memoria: m->ram guarda cada palavra como o inteiro big-endian do 3DO,
 * entao um vetor/matriz 16.16 no endereco A (multiplo de 4) e simplesmente ((int32_t *)m->ram)[A >> 2]. */
#include "ph_swi_hle.h"
#include "ph_fixedpoint.h"

#define PH_HLE_RAM_BYTES m->ram_bytes   /* DRAM + VRAM (m->ram) */

/* ponteiro de host para n palavras a partir de a; NULL se desalinhado ou fora da RAM */
static int32_t *W(ph_machine_t *m, uint32_t a, uint32_t n) {
    if ((a & 3u) || a >= PH_HLE_RAM_BYTES || n > (PH_HLE_RAM_BYTES - a) / 4u) return NULL;
    return (int32_t *)m->ram + (a >> 2);
}

int ph_swi_hle(ph_machine_t *m, uint32_t imm) {
    uint32_t *r = m->cpu.r;
    int32_t *d, *s1, *s2, *mt;
    uint32_t n;
    if (imm < 0x50000u || imm > 0x50012u) return 0;
    switch (imm) {
    case 0x50000:   /* MulVec3Mat33_F16(dest, vec, mat) */
        if (!(d = W(m, r[0], 3)) || !(s1 = W(m, r[1], 3)) || !(mt = W(m, r[2], 9))) return 0;
        MulVec3Mat33_F16(d, s1, (frac16 (*)[3])mt); return 1;
    case 0x50001:   /* MulMat33Mat33_F16(dest, src1, src2) */
        if (!(d = W(m, r[0], 9)) || !(s1 = W(m, r[1], 9)) || !(s2 = W(m, r[2], 9))) return 0;
        MulMat33Mat33_F16((frac16 (*)[3])d, (frac16 (*)[3])s1, (frac16 (*)[3])s2); return 1;
    case 0x50002:   /* MulManyVec3Mat33_F16(dest, src, mat, count) */
        n = r[3]; if ((int32_t)n < 0 || n > 0x100000u) return 0;
        if (!(d = W(m, r[0], 3 * n)) || !(s1 = W(m, r[1], 3 * n)) || !(mt = W(m, r[2], 9))) return 0;
        MulManyVec3Mat33_F16((frac16 (*)[3])d, (frac16 (*)[3])s1, (frac16 (*)[3])mt, (int32_t)n); return 1;
    case 0x50005:   /* MulManyF16(dest, src1, src2, count) */
        n = r[3]; if ((int32_t)n < 0 || n > 0x100000u) return 0;
        if (!(d = W(m, r[0], n)) || !(s1 = W(m, r[1], n)) || !(s2 = W(m, r[2], n))) return 0;
        MulManyF16(d, s1, s2, (int32_t)n); return 1;
    case 0x50006:   /* MulScalerF16(dest, src, scaler, count) */
        n = r[3]; if ((int32_t)n < 0 || n > 0x100000u) return 0;
        if (!(d = W(m, r[0], n)) || !(s1 = W(m, r[1], n))) return 0;
        MulScalerF16(d, s1, (frac16)r[2], (int32_t)n); return 1;
    case 0x50007:   /* MulVec4Mat44_F16(dest, vec, mat) */
        if (!(d = W(m, r[0], 4)) || !(s1 = W(m, r[1], 4)) || !(mt = W(m, r[2], 16))) return 0;
        MulVec4Mat44_F16(d, s1, (frac16 (*)[4])mt); return 1;
    case 0x50008:   /* MulMat44Mat44_F16(dest, src1, src2) */
        if (!(d = W(m, r[0], 16)) || !(s1 = W(m, r[1], 16)) || !(s2 = W(m, r[2], 16))) return 0;
        MulMat44Mat44_F16((frac16 (*)[4])d, (frac16 (*)[4])s1, (frac16 (*)[4])s2); return 1;
    case 0x50009:   /* MulManyVec4Mat44_F16(dest, src, mat, count) */
        n = r[3]; if ((int32_t)n < 0 || n > 0xC0000u) return 0;
        if (!(d = W(m, r[0], 4 * n)) || !(s1 = W(m, r[1], 4 * n)) || !(mt = W(m, r[2], 16))) return 0;
        MulManyVec4Mat44_F16((frac16 (*)[4])d, (frac16 (*)[4])s1, (frac16 (*)[4])mt, (int32_t)n); return 1;
    case 0x5000C:   /* Dot3_F16(v1, v2) -> r0 */
        if (!(s1 = W(m, r[0], 3)) || !(s2 = W(m, r[1], 3))) return 0;
        r[0] = (uint32_t)Dot3_F16(s1, s2); return 1;
    case 0x5000E:   /* Cross3_F16(dest, v1, v2) */
        if (!(d = W(m, r[0], 3)) || !(s1 = W(m, r[1], 3)) || !(s2 = W(m, r[2], 3))) return 0;
        Cross3_F16(d, s1, s2); return 1;
    case 0x5000F:   /* AbsVec3_F16(vec) -> r0 */
        if (!(s1 = W(m, r[0], 3))) return 0;
        r[0] = (uint32_t)AbsVec3_F16(s1); return 1;
    case 0x50010:   /* AbsVec4_F16(vec) -> r0 */
        if (!(s1 = W(m, r[0], 4))) return 0;
        r[0] = (uint32_t)AbsVec4_F16(s1); return 1;
    case 0x50011:   /* MulVec3Mat33DivZ_F16(dest, vec, mat, n) */
        if (!(d = W(m, r[0], 3)) || !(s1 = W(m, r[1], 3)) || !(mt = W(m, r[2], 9))) return 0;
        MulVec3Mat33DivZ_F16(d, s1, (frac16 (*)[3])mt, (frac16)r[3]); return 1;
    case 0x50012: { /* MulManyVec3Mat33DivZ_F16(struct mmv3m33d *): dest, src, mat, n, count em palavras consecutivas */
        int32_t *q = W(m, r[0], 5);
        if (!q) return 0;
        n = (uint32_t)q[4]; if (n > 0x100000u) return 0;
        if (!(d = W(m, (uint32_t)q[0], 3 * n)) || !(s1 = W(m, (uint32_t)q[1], 3 * n)) || !(mt = W(m, (uint32_t)q[2], 9))) return 0;
        MulManyVec3Mat33DivZ_F16((frac16 (*)[3])d, (frac16 (*)[3])s1, (frac16 (*)[3][3])mt, (frac16)q[3], n); return 1; }
    default: return 0;   /* 0x50003/04/0A/0B/0D: o Opera tambem deixa para o SO */
    }
}
