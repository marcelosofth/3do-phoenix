#!/usr/bin/env python3
# armdis.py -- desmontador ARM (ARMv3 / ARM60 do 3DO) em Python puro. NAO precisa de capstone nem de pip.
#
# Uso:
#   python armdis.py <endereco hex> <n>                 desmonta n palavras de /tmp/ram.bin (gravado pelo snap)
#   python armdis.py <endereco hex> <n> --bios fz1.bin  desmonta da BIOS (big-endian; endereco 0x03000000+ ou offset)
#   python armdis.py <endereco hex> <n> --ram caminho   outro dump de RAM
#
# O dump de RAM do snap e `fwrite(m.ram,4,0x40000,...)`: a palavra do endereco A esta no offset A
# (valor 32 bits do ARM em little-endian do host), por isso nada de inverter bytes.
# Em `LDR Rx,[pc,#n]` o valor do literal e mostrado como comentario (; =0x...).
import sys, struct, os, glob

CC = ["eq","ne","cs","cc","mi","pl","vs","vc","hi","ls","ge","lt","gt","le","","nv"]
DP = ["and","eor","sub","rsb","add","adc","sbc","rsc","tst","teq","cmp","cmn","orr","mov","bic","mvn"]
SH = ["lsl","lsr","asr","ror"]
RN = ["r0","r1","r2","r3","r4","r5","r6","r7","r8","r9","r10","r11","r12","sp","lr","pc"]

def ror32(v, n):
    n &= 31
    return ((v >> n) | (v << (32 - n))) & 0xFFFFFFFF if n else v

def reglist(m):
    out = []; i = 0
    while i < 16:
        if m >> i & 1:
            j = i
            while j + 1 < 16 and m >> (j + 1) & 1: j += 1
            out.append(RN[i] if i == j else (RN[i] + "-" + RN[j] if j > i + 1 else RN[i] + "," + RN[j]))
            i = j + 1
        else:
            i += 1
    return "{" + ",".join(out) + "}"

def shifted_reg(w):
    rm = RN[w & 15]; t = (w >> 5) & 3
    if w & 0x10:
        return "%s, %s %s" % (rm, SH[t], RN[(w >> 8) & 15])
    amt = (w >> 7) & 31
    if amt == 0:
        if t == 0: return rm
        if t == 3: return rm + ", rrx"
        amt = 32
    return "%s, %s #%d" % (rm, SH[t], amt)

def dis(w, pc, read32=None):
    """devolve (texto, comentario)"""
    c = CC[w >> 28]
    cmt = ""
    cls = (w >> 25) & 7

    if (w >> 28) == 15:
        return "undef/nv  (cond=1111)", ""

    # ---- classe 000 : MUL/MLA, SWP, DP com registrador ----
    if cls == 0 and (w & 0x0fc000f0) == 0x00000090:
        s = "s" if w & 0x100000 else ""
        rd, rn, rs, rm = (w >> 16) & 15, (w >> 12) & 15, (w >> 8) & 15, w & 15
        if w & 0x200000:
            return "mla%s%s %s, %s, %s, %s" % (c, s, RN[rd], RN[rm], RN[rs], RN[rn]), ""
        return "mul%s%s %s, %s, %s" % (c, s, RN[rd], RN[rm], RN[rs]), ""
    if cls == 0 and (w & 0x0fb00ff0) == 0x01000090:
        b = "b" if w & 0x400000 else ""
        return "swp%s%s %s, %s, [%s]" % (c, b, RN[(w >> 12) & 15], RN[w & 15], RN[(w >> 16) & 15]), ""
    if cls == 0 and (w & 0x90) == 0x90:
        return "undef  (%08x)" % w, ""

    # ---- DP / MRS / MSR ----
    if cls in (0, 1):
        op = (w >> 21) & 15; s = bool(w & 0x100000)
        rn, rd = (w >> 16) & 15, (w >> 12) & 15
        if 8 <= op <= 11 and not s:       # PSR transfer
            psr = "spsr" if w & 0x400000 else "cpsr"
            if op in (8, 10) and cls == 0 and (w & 0x0fbf0fff) == 0x010f0000:
                return "mrs%s %s, %s" % (c, RN[rd], psr), ""
            if (w & 0x0fb0fff0) == 0x0120f000 and cls == 0:
                f = (w >> 16) & 15
                fld = psr + ("_all" if f == 9 or f == 15 else "_flg" if f == 8 else "_ctl" if f == 1 else "_%x" % f)
                return "msr%s %s, %s" % (c, fld, RN[w & 15]), ""
            if cls == 1 and (w & 0x0fb0f000) == 0x0320f000:
                f = (w >> 16) & 15
                v = ror32(w & 255, ((w >> 8) & 15) * 2)
                fld = psr + ("_all" if f == 9 or f == 15 else "_flg" if f == 8 else "_ctl" if f == 1 else "_%x" % f)
                return "msr%s %s, #0x%x" % (c, fld, v), ""
            return "undef  (%08x)" % w, ""
        if cls == 1:
            v = ror32(w & 255, ((w >> 8) & 15) * 2)
            o2 = "#0x%x" % v if v > 9 else "#%d" % v
        else:
            o2 = shifted_reg(w)
        name = DP[op]
        if op in (8, 9, 10, 11):          # tst/teq/cmp/cmn : S implicito; Rd=15 -> sufixo p
            p = "p" if rd == 15 else ""
            return "%s%s%s %s, %s" % (name, c, p, RN[rn], o2), ""
        ss = "s" if s else ""
        if rd == 15 and s: ss = "s"       # movs pc,lr  (restaura CPSR)
        if op in (13, 15):                # mov / mvn
            return "%s%s%s %s, %s" % (name, c, ss, RN[rd], o2), ""
        # comentario util para add/sub rX,pc,#n
        if cls == 1 and rn == 15 and op in (2, 4):
            tgt = (pc + 8 + (v if op == 4 else -v)) & 0xFFFFFFFF
            cmt = "=0x%08x" % tgt
        return "%s%s%s %s, %s, %s" % (name, c, ss, RN[rd], RN[rn], o2), cmt

    # ---- LDR / STR ----
    if cls in (2, 3):
        if cls == 3 and (w & 0x10):
            return "undef  (%08x)" % w, ""
        L = w & 0x100000; B = "b" if w & 0x400000 else ""
        P = w & 0x1000000; U = w & 0x800000; Wb = w & 0x200000
        rn, rd = (w >> 16) & 15, (w >> 12) & 15
        t = "t" if (not P and Wb) else ""
        sg = "" if U else "-"
        if cls == 2:
            off = w & 0xfff
            offs = "#%s%d" % (sg, off) if off < 10 else "#%s0x%x" % (sg, off)
            if off == 0 and U: offs = None
        else:
            offs = sg + shifted_reg(w)
        if P:
            addr = "[%s]" % RN[rn] if offs is None else "[%s, %s]" % (RN[rn], offs)
            if Wb: addr += "!"
        else:
            addr = "[%s]" % RN[rn] if offs is None else "[%s], %s" % (RN[rn], offs)
        if cls == 2 and rn == 15 and P and not Wb and L:
            a = (pc + 8 + (off if U else -off)) & 0xFFFFFFFF
            cmt = "[0x%08x]" % a
            if read32:
                try:
                    cmt += " = 0x%08x" % read32(a & ~3) if not B else ""
                except Exception:
                    pass
        return "%s%s%s%s %s, %s" % ("ldr" if L else "str", c, B, t, RN[rd], addr), cmt

    # ---- LDM / STM ----
    if cls == 4:
        L = w & 0x100000
        mode = ("ib" if w & 0x800000 else "db") if w & 0x1000000 else ("ia" if w & 0x800000 else "da")
        rn = (w >> 16) & 15
        rl = reglist(w & 0xffff)
        # alias de pilha
        txt = "%s%s%s %s%s, %s%s" % ("ldm" if L else "stm", c, mode, RN[rn], "!" if w & 0x200000 else "", rl,
                                     "^" if w & 0x400000 else "")
        return txt, ""

    # ---- B / BL ----
    if cls == 5:
        off = w & 0xffffff
        if off & 0x800000: off -= 0x1000000
        tgt = (pc + 8 + (off << 2)) & 0xFFFFFFFF
        return "%s%s 0x%08x" % ("bl" if w & 0x1000000 else "b", c, tgt), ""

    # ---- coprocessador / SWI ----
    if cls == 6:
        L = w & 0x100000
        off = (w & 255) * 4
        P = w & 0x1000000; U = w & 0x800000; Wb = w & 0x200000
        base = "[%s" % RN[(w >> 16) & 15]
        sg = "" if U else "-"
        if P:
            addr = base + (", #%s%d" % (sg, off) if off else "") + "]" + ("!" if Wb else "")
        elif Wb:
            addr = base + "], #%s%d" % (sg, off)
        else:
            addr = base + "], {%d}" % (w & 255)
        return "%s%s%s p%d, c%d, %s" % ("ldc" if L else "stc", c, "l" if w & 0x400000 else "",
                                       (w >> 8) & 15, (w >> 12) & 15, addr), ""
    if cls == 7:
        if w & 0x1000000:
            return "swi%s 0x%06x" % (c, w & 0xffffff), ""
        if w & 0x10:
            return "%s%s p%d, %d, %s, c%d, c%d, %d" % ("mrc" if w & 0x100000 else "mcr", c, (w >> 8) & 15,
                                                       (w >> 21) & 7, RN[(w >> 12) & 15], (w >> 16) & 15, w & 15,
                                                       (w >> 5) & 7), ""
        return "cdp%s p%d, %d, c%d, c%d, c%d, %d" % (c, (w >> 8) & 15, (w >> 20) & 15, (w >> 12) & 15,
                                                     (w >> 16) & 15, w & 15, (w >> 5) & 7), ""
    return "?", ""

def find_ram(path):
    """No Windows, o /tmp do snap.exe (MinGW/MSYS) nem sempre e o /tmp do Python: procura em varios lugares."""
    if path != "/tmp/ram.bin":
        return path
    here = os.path.dirname(os.path.abspath(__file__))
    cand = ["ram.bin", os.path.join(here, "ram.bin"), "/tmp/ram.bin"]
    for v in ("TEMP", "TMP"):
        if os.environ.get(v): cand.append(os.path.join(os.environ[v], "ram.bin"))
    cand += ["C:/tmp/ram.bin", "G:/tmp/ram.bin"]
    for pat in ("C:/msys64*/tmp/ram.bin", "C:/msys64*/*/tmp/ram.bin", "C:/Users/*/AppData/Local/Temp/ram.bin",
                "C:/cygwin*/tmp/ram.bin", "C:/mingw*/tmp/ram.bin", "C:/mingw*/msys/*/tmp/ram.bin"):
        cand += glob.glob(pat)
    ok = [c for c in cand if os.path.isfile(c)]
    if not ok:
        sys.exit("ram.bin nao encontrado. Rode o snap com 4o argumento: snap.exe bios img ciclos ram.bin  (ou use --ram caminho)")
    return max(ok, key=os.path.getmtime)

def main(argv):
    args = [a for a in argv[1:]]
    path = "/tmp/ram.bin"; base = 0; bios = False
    if "--ram" in args:
        i = args.index("--ram"); path = args[i + 1]; del args[i:i + 2]
    if "--bios" in args:
        i = args.index("--bios"); path = args[i + 1]; del args[i:i + 2]; bios = True
    if len(args) < 2:
        print(__doc__); return 1
    a = int(args[0], 16); n = int(args[1], 0)
    path = find_ram(path)
    data = open(path, "rb").read()
    print("; lendo", path, file=sys.stderr)
    if bios and a >= 0x03000000:
        base = 0x03000000
    def read32(x):
        o = x - base
        if o < 0 or o + 4 > len(data): raise IndexError
        return struct.unpack_from(">I" if bios else "<I", data, o)[0]
    for i in range(n):
        ad = a + 4 * i
        try:
            w = read32(ad)
        except IndexError:
            print("%08x: (fora do arquivo, tamanho=0x%x)" % (ad, len(data))); break
        t, cm = dis(w, ad, read32)
        print("%08x: %08x  %-34s%s" % (ad, w, t, ("; " + cm) if cm else ""))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
