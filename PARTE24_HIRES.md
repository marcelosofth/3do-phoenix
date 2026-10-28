# PARTE 24 - HiRes CEL Rendering (`phoenix_high_resolution`)

Implementado seguindo o `opera-libretro` (HIRESMODE). Vale **na hora** (sem reiniciar o nucleo).

## Modelo
- Framebuffer 2x = VRAM (plano 0) + 3 planos extras de 1 MB (`ph_machine_t.hr[3]`, mesmo leiaute de palavras da RAM).
  Pixel 2x (x,y) -> plano `q = ((y&1)<<1)|(x&1)`, posicao `(x>>1, y>>1)` (igual ao `readPIX/writePIX` do Opera).
- Opera guarda os planos em `VRAM + q*VRAM_SIZE` (alem da RAM). O Phoenix usa buffers separados; isso evita sobrescrever
  DRAM quando o destino do CEL nao e VRAM.
- **Escritas 1x valem para os 4 planos** (nearest): CPU (`ph_write32/8`, regiao da VRAM), DMA (`clio.ram_written`),
  SPORT (flash/copia, `sport.c`; a copia preserva o conteudo 2x plano a plano) e CELs de linha/escala (`opera_mem_write16`).
- **CEL arbitrario (3D)** (`TexelDrawArbitrary`): coordenadas em 2x (`>> (16-HIRESMODE)`), grava so a subamostra de cada pixel
  (`writePIX`). `HIRESMODE` (variavel em `cel_opera.c`) so liga quando `REGCTL3` aponta para a VRAM; destino em DRAM = 1x.
- **VDLP** (`vdlp.c`): com `hires`, captura tambem os planos 1..3 e gera 2 linhas de `2*largura` por linha nativa
  (linha par = planos 0,1; impar = planos 2,3). Blit em linhas 2x (`rgb_hr`).
- **Video** (`libretro.c`): `g_src2` (2x). Layout com `k` (fator da origem) e `s` (fator de saida): FBO 1x/2x = 2x exato
  (640x480 NTSC), 3x e 4x reamostram a origem 2x. Filtros/bordas/fundo expandido funcionam igual.
- Savestate nao guarda os planos extras: ao carregar, recomecam da VRAM por replicacao.

## Correcao junto
`vdlp_line` zerava `dirty` a cada linha, entao trocar Bypass CLUT / HiRes ao vivo nao reconvertia linhas sem mudanca.
Agora `vdlp_t.force_dirty` vale ate o fim do quadro.

## Testes
`test_hires.c` (CPU/DMA/SPORT/CEL 1x e 3D/DRAM/desligar/savestate/regiao/VDLP) e bloco "HiRes" em `test_libretro.c`.
`make` e `make asan` passam (o unico aviso do ASan, stack-use-after-return em `defof`, ja existia no teste).
