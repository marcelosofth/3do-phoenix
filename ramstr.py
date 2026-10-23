#!/usr/bin/env python3
# ramstr.py -- lista textos ASCII da RAM do 3DO (nomes de tasks/itens/modulos) e referencias a um valor.
# Uso: python ramstr.py [ram_antes.bin] [minimo=5] [--ref 284e0]
#   sem --ref : imprime todas as strings ASCII (endereco: texto). Nomes de tasks costumam aparecer perto de 0x1f0000-0x200000.
#   --ref V   : imprime os enderecos de RAM que contem a palavra V (ponteiros p/ a funcao, TCBs etc.)
import sys, struct
a = [x for x in sys.argv[1:]]
ref = None
if "--ref" in a:
    i = a.index("--ref"); ref = int(a[i + 1], 16); del a[i:i + 2]
path = a[0] if a else "ram_antes.bin"
mn = int(a[1]) if len(a) > 1 else 5
d = open(path, "rb").read()
n = len(d) // 4
if ref is not None:
    for i, w in enumerate(struct.unpack("<%dI" % n, d[:n * 4])):
        if w == ref: print("%08x: %08x" % (i * 4, w))
    sys.exit()
be = b"".join(d[i:i + 4][::-1] for i in range(0, n * 4, 4))   # bytes na ordem do 3DO
run = b""; st = 0
for i, c in enumerate(be + b"\0"):
    if 32 <= c < 127:
        if not run: st = i
        run += bytes([c])
    else:
        if len(run) >= mn: print("%08x: %s" % (st, run.decode()))
        run = b""
