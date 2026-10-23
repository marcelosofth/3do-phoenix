#!/usr/bin/env python3
"""armdis_mini.py — desmontador ARMv3/v4 (little-endian, sem Thumb) sem dependencias externas.
Mesma interface do armdis.py original (que precisa de capstone/pip): le /tmp/ram.bin e desmonta
a partir de um endereco guest, que e o offset dentro do arquivo (o ram.bin do snap.c comeca em
guest 0x00000000).

Uso: python armdis_mini.py <endereco_hex> <n_instrucoes> [arquivo=/tmp/ram.bin]

Cobre: Data Processing (AND..MVN, com imediato/registrador/shift), MUL/MLA, LDR/STR (word e byte,
imediato e registrador), LDM/STM, B/BL, SWI, MRS/MSR, BX (caso apareca). O que nao reconhecer
imprime so o hex com "??".
"""
import sys, struct

COND = ["EQ","NE","CS","CC","MI","PL","VS","VC","HI","LS","GE","LT","GT","LE","AL","NV"]
DPOP = ["AND","EOR","SUB","RSB","ADD","ADC","SBC","RSC","TST","TEQ","CMP","CMN","ORR","MOV","BIC","MVN"]
REG  = ["r%d" % i for i in range(13)] + ["sp", "lr", "pc"]

def shifted_reg(instr):
    rm = instr & 0xF
    stype = (instr >> 5) & 3
    snames = ["LSL", "LSR", "ASR", "ROR"]
    if instr & 0x10:  # shift por registrador
        rs = (instr >> 8) & 0xF
        return "%s, %s %s" % (REG[rm], snames[stype], REG[rs])
    else:
        shamt = (instr >> 7) & 0x1F
        if shamt == 0 and stype == 3:
            return "%s, RRX" % REG[rm]
        if shamt == 0 and stype != 0:
            shamt = 32
        if shamt == 0:
            return REG[rm]
        return "%s, %s #%d" % (REG[rm], snames[stype], shamt)

def rotimm(instr):
    imm = instr & 0xFF
    rot = ((instr >> 8) & 0xF) * 2
    val = ((imm >> rot) | (imm << (32 - rot))) & 0xFFFFFFFF if rot else imm
    return val

def decode(addr, instr):
    cond = COND[(instr >> 28) & 0xF]
    c = "" if cond == "AL" else cond

    # BX
    if (instr & 0x0FFFFFF0) == 0x012FFF10:
        return "BX%s %s" % (c, REG[instr & 0xF])

    # Branch / Branch-Link
    if (instr >> 25) & 0x7 == 0x5:
        link = (instr >> 24) & 1
        off = instr & 0xFFFFFF
        if off & 0x800000:
            off -= 0x1000000
        target = (addr + 8 + (off << 2)) & 0xFFFFFFFF
        return "B%s%s 0x%x" % ("L" if link else "", c, target)

    # SWI
    if (instr >> 24) & 0xF == 0xF:
        return "SWI%s 0x%06x" % (c, instr & 0xFFFFFF)

    # MRS / MSR (bits 27:26=00, bit24=1, bit23=0, bits21:20=00/10)
    if (instr & 0x0FBF0FFF) == 0x010F0000:
        rd = (instr >> 12) & 0xF
        psr = "SPSR" if (instr >> 22) & 1 else "CPSR"
        return "MRS%s %s, %s" % (c, REG[rd], psr)
    if (instr & 0x0DB0F000) == 0x0120F000:
        psr = "SPSR" if (instr >> 22) & 1 else "CPSR"
        if instr & 0x02000000:
            return "MSR%s %s_f, #0x%x" % (c, psr, rotimm(instr))
        return "MSR%s %s, %s" % (c, psr, REG[instr & 0xF])

    # Multiply / Multiply-Accumulate
    if (instr & 0x0FC000F0) == 0x00000090:
        a = (instr >> 21) & 1
        s = (instr >> 20) & 1
        rd = (instr >> 16) & 0xF; rn = (instr >> 12) & 0xF
        rs = (instr >> 8) & 0xF; rm = instr & 0xF
        mn = "MLA" if a else "MUL"
        tail = (", %s" % REG[rn]) if a else ""
        return "%s%s%s %s, %s, %s%s" % (mn, c, "S" if s else "", REG[rd], REG[rm], REG[rs], tail)

    top3 = (instr >> 25) & 0x7
    # Data processing (imediato ou registrador): bits 27:26 == 00
    if (instr >> 26) & 0x3 == 0:
        op = (instr >> 21) & 0xF
        s = (instr >> 20) & 1
        rn = (instr >> 16) & 0xF
        rd = (instr >> 12) & 0xF
        imm = (instr >> 25) & 1
        mn = DPOP[op]
        sbit = "S" if (s and op not in (8, 9, 10, 11)) else ""
        if imm:
            val = rotimm(instr)
            rhs = "#0x%x" % val
        else:
            rhs = shifted_reg(instr)
        if op in (13, 15):  # MOV / MVN nao usam Rn
            return "%s%s%s %s, %s" % (mn, c, sbit, REG[rd], rhs)
        if op in (8, 9, 10, 11):  # TST/TEQ/CMP/CMN nao escrevem Rd
            return "%s%s %s, %s" % (mn, c, REG[rn], rhs)
        return "%s%s%s %s, %s, %s" % (mn, c, sbit, REG[rd], REG[rn], rhs)

    # LDR/STR (word/byte), bits 27:26 == 01
    if (instr >> 26) & 0x3 == 1:
        imm = not ((instr >> 25) & 1)  # bit25=0 => offset imediato; =1 => offset por registrador
        p = (instr >> 24) & 1
        u = (instr >> 23) & 1
        b = (instr >> 22) & 1
        w = (instr >> 21) & 1
        ld = (instr >> 20) & 1
        rn = (instr >> 16) & 0xF
        rd = (instr >> 12) & 0xF
        mn = ("LDR" if ld else "STR") + ("B" if b else "")
        sign = "" if u else "-"
        if imm:
            off = instr & 0xFFF
            offs = "#%s0x%x" % (sign, off) if off else None
        else:
            offs = sign + shifted_reg(instr & 0xFFF | 0)
        base = REG[rn]
        if off_is_zero_pre := (p and imm and (instr & 0xFFF) == 0):
            addrmode = "[%s]" % base
        elif p:
            addrmode = "[%s, %s]%s" % (base, offs, "!" if w else "")
        else:
            addrmode = "[%s], %s" % (base, offs)
        return "%s%s %s, %s" % (mn, c, REG[rd], addrmode)

    # LDM/STM, bits 27:25 == 100
    if top3 == 4:
        p = (instr >> 24) & 1; u = (instr >> 23) & 1
        s = (instr >> 22) & 1; w = (instr >> 21) & 1
        ld = (instr >> 20) & 1
        rn = (instr >> 16) & 0xF
        reglist = instr & 0xFFFF
        names = [REG[i] for i in range(16) if reglist & (1 << i)]
        mode = {(1,1):"IB",(1,0):"DB",(0,1):"IA",(0,0):"DA"}[(p,u)]
        mn = "LDM" if ld else "STM"
        return "%s%s%s %s%s, {%s}%s" % (mn, c, mode, REG[rn], "!" if w else "", ",".join(names), "^" if s else "")

    return None

def main():
    if len(sys.argv) < 3:
        print("uso: %s <endereco_hex> <n_instrucoes> [arquivo=/tmp/ram.bin]" % sys.argv[0])
        return 1
    addr0 = int(sys.argv[1], 16)
    n = int(sys.argv[2])
    path = sys.argv[3] if len(sys.argv) > 3 else "/tmp/ram.bin"
    with open(path, "rb") as f:
        f.seek(addr0)
        data = f.read(n * 4)
    for i in range(0, len(data) - 3, 4):
        addr = addr0 + i
        instr = struct.unpack_from("<I", data, i)[0]
        txt = decode(addr, instr)
        print("%08x: %08x  %s" % (addr, instr, txt if txt else "?? (nao reconhecida)"))
    return 0

if __name__ == "__main__":
    sys.exit(main())
