#!/usr/bin/env python3
# Gera ph_biosdata.h (BIOS incorporadas ao nucleo) a partir dos .bin. Uso (na raiz do projeto): python3 tools/gen_biosdata.py [pasta_dos_bin]
# Cada linha da tabela: (arquivo de origem, nome do arquivo que o nucleo mostra/usa, simbolo C). O CRC32 de cada uma e conferido em ph_options.h (PH_EMBBIOS).
import sys, zlib, os
src = sys.argv[1] if len(sys.argv) > 1 else "."
ROMS = [
    ("panafz1.bin",                 "panafz1",            "panafz1.bin"),
    ("panafz10.bin",                "panafz10",           "panafz10.bin"),
    ("panafz10-patched.bin",        "panafz10_norsa",     "panafz10-norsa.bin"),
    ("panafz10e-anvil.bin",         "panafz10e_anvil",    "panafz10e-anvil.bin"),
    ("panafz10e-anvil-patched.bin", "panafz10e_anvil_norsa", "panafz10e-anvil-norsa.bin"),
    ("goldstar.bin",                "goldstar",           "goldstar.bin"),
    ("sanyotry.bin",                "sanyotry",           "sanyotry.bin"),
    ("3do_arcade_saot.bin",         "arcade_saot",        "3do_arcade_saot.bin"),
]
SAFE = set(b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ")
out = ["/* ph_biosdata.h - BIOS incorporadas ao nucleo. GERADO por tools/gen_biosdata.py; nao editar a mao.\n * Incluido so por ph_options.h. Dados static const em literais de string (octal de 3 digitos): ficam no proprio .dll/.so.\n * Cada vetor tem 1 byte a mais (o NUL do literal); o tamanho real e sizeof - 1. */\n#ifndef PH_BIOSDATA_H\n#define PH_BIOSDATA_H\n#include <stdint.h>\n"]
for fn, sym, shown in ROMS:
    d = open(os.path.join(src, fn), "rb").read()
    out.append("/* %s (mostrada como %s): %d bytes, CRC32 %08x */\nstatic const uint8_t ph_biosdata_%s[%d] =\n" % (fn, shown, len(d), zlib.crc32(d), sym, len(d) + 1))
    for i in range(0, len(d), 64):
        out.append('"' + "".join(chr(b) if b in SAFE else "\\%03o" % b for b in d[i:i+64]) + '"\n')
    out.append(";\n")
out.append("#endif\n")
open("ph_biosdata.h", "w").write("".join(out))
