#!/usr/bin/env python3
# Gera ph_fontdata.h (FONT-ROMs kanji incorporadas ao nucleo) a partir dos .bin.
# Uso: python3 tools/gen_fontdata.py [pasta_dos_bin]   (padrao: pasta atual); rode na raiz do projeto.
import sys, zlib, os
src = sys.argv[1] if len(sys.argv) > 1 else "."
files = [("panafz1_kanji", "panafz1-kanji.bin"), ("panafz10ja_anvil_kanji", "panafz10ja-anvil-kanji.bin")]
out = ["/* ph_fontdata.h - FONT-ROMs (kanji) incorporadas ao nucleo. GERADO por tools/gen_fontdata.py a partir dos .bin; nao editar a mao.\n * Incluido so por ph_options.h (dados static const: ficam no proprio .dll/.so, nenhum arquivo externo e necessario). */\n#ifndef PH_FONTDATA_H\n#define PH_FONTDATA_H\n#include <stdint.h>\n"]
for sym, fn in files:
    d = open(os.path.join(src, fn), "rb").read()
    out.append("/* %s: %d bytes, CRC32 %08x */\nstatic const uint8_t ph_fontdata_%s[%d] = {\n" % (fn, len(d), zlib.crc32(d), sym, len(d)))
    for i in range(0, len(d), 48):
        out.append(",".join(str(b) for b in d[i:i+48]) + ",\n")
    out.append("};\n")
out.append("#endif\n")
open("ph_fontdata.h", "w").write("".join(out))
