#!/usr/bin/env python3
# quem.py -- roda o quemchama e ja desmonta sozinho os pontos de origem. Sem preencher endereco nenhum.
# Uso:  python quem.py            (usa os caminhos padrao abaixo)
#       python quem.py [bios] [imagem] [alvo_hex] [ciclo_min] [ciclo_max]
# Gera tambem quem_saida.txt com tudo (mande esse arquivo).
import sys, os, re, struct, subprocess
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import armdis

a = sys.argv[1:]
bios = a[0] if len(a) > 0 else "F:/Emulators/RetroArch/bios/fz1.bin"
img  = a[1] if len(a) > 1 else "G:/Internet_Temp/Batocera_add/Projetos/AI Shougi (Japan).chd"
alvo = a[2] if len(a) > 2 else "284e0"
cmin = a[3] if len(a) > 3 else "2000000"
cmax = a[4] if len(a) > 4 else "110000000"
exe = next((p for p in (os.path.join(here, "quemchama.exe"), os.path.join(here, "quemchama")) if os.path.isfile(p)), None)
if not exe:
    sys.exit("quemchama(.exe) nao encontrado na pasta. Compile primeiro (comando gcc do cabecalho do quemchama.c).")
ramf = os.path.join(here, "ram_antes.bin")
if os.path.exists(ramf): os.remove(ramf)
r = subprocess.run([exe, bios, img, alvo, cmin, cmax, ramf], capture_output=True, text=True)
out = [r.stdout, r.stderr]
if r.returncode != 0 or not os.path.exists(ramf):
    txt = "".join(out) + "\nquemchama nao chegou ao alvo (rc=%d). Tente ciclo_min menor, ex.: python quem.py \"%s\" \"%s\" %s 1000000\n" % (r.returncode, bios, img, alvo)
    print(txt); open(os.path.join(here, "quem_saida.txt"), "w").write(txt); sys.exit(1)
ram = open(ramf, "rb").read()
def rd(x): return struct.unpack_from("<I", ram, x)[0]
def show(start, n, mark=None, title=""):
    out.append("\n--- %s ---\n" % title)
    for i in range(n):
        ad = start + 4 * i
        if ad + 4 > len(ram): break
        t, cm = armdis.dis(rd(ad), ad, rd)
        out.append("%s%08x: %08x  %-34s%s\n" % ("=> " if ad == mark else "   ", ad, rd(ad), t, ("; " + cm) if cm else ""))
jumps = [(int(m.group(1), 16), int(m.group(2), 16)) for m in re.finditer(r"cyc=\d+\s+([0-9a-f]{8}) -> ([0-9a-f]{8})", r.stdout)]
seen = set()
for frm, to in reversed(jumps[-8:]):
    if frm in seen or frm == 0xffffffff: continue
    seen.add(frm)
    s = max(0, frm - 0x1c)
    show(s, 10, frm, "origem do salto %08x -> %08x (=> marca a instrucao que saltou)" % (frm, to))
show(0x28148, 12, None, "28148 (tratador chamado por 'bllt' quando sp<r10)")
show(0x284f8, 24, None, "funcao vizinha 284f8")

# ---- strings da RAM + buffers dos setores lidos pelo CD (DMA) ----
if len(ram) < 0x300000:
    out.append("\n!!! ram_antes.bin tem so %d bytes (precisa de 3145728). Recompile o quemchama com o quemchama.c novo.\n" % len(ram))
n = len(ram) // 4
be = b"".join(ram[i:i + 4][::-1] for i in range(0, n * 4, 4))
strs = []; run = b""; st = 0
for i, c in enumerate(be + b"\0"):
    if 32 <= c < 127:
        if not run: st = i
        run += bytes([c])
    else:
        if len(run) >= 5: strs.append((st, run.decode()))
        run = b""
open(os.path.join(here, "strings.txt"), "w").write("".join("%08x: %s\n" % x for x in strs))
kw = re.compile(r"launch|boot|\.app|system|folder|cd-rom|startup|shougi|dipir|idle|task|mount|opera|disc", re.I)
out.append("\n--- strings interessantes da RAM (todas em strings.txt) ---\n")
for a_, t_ in strs:
    if kw.search(t_): out.append("%08x: %s\n" % (a_, t_[:100]))
def hexdump(addr, ln):
    out.append("\n--- RAM em %08x (bytes na ordem do 3DO) ---\n" % addr)
    for o in range(0, ln, 16):
        if addr + o + 16 > len(be): break
        b = be[addr + o:addr + o + 16]
        out.append("%08x: %s  %s\n" % (addr + o, " ".join("%02x" % x for x in b), "".join(chr(x) if 32 <= x < 127 else "." for x in b)))
for a_ in (0x1f1b70, 0x1f0620, 0x1f0540, 0x1f1140):
    hexdump(a_, 96)
txt = "".join(out)
print(txt)
open(os.path.join(here, "quem_saida.txt"), "w").write(txt)
print("\n(gravado em quem_saida.txt)")
