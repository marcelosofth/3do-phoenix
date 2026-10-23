# Parte 15 — Rasterizador do CEL (`FUN_00480290`): mapa dos caminhos + testes de descarte/endereçamento (cel.c, parte 15)

Método igual à Parte 14 (Ghidra sem análise automática; decompilações em `PhoenixRE_parte15.zip`). Novas: `FUN_00481320`, `FUN_0047f9e0`, `FUN_0047ed60`, `FUN_0047ccf0`, `FUN_008de650`.
**Escopo honesto:** li a cabeça inteira e o esqueleto dos três caminhos; **portei só o que está verificado** (seção 4). Não portei o desenho de pixels nem os caminhos 2 e 3.

## 1. Registradores derivados (montados em `FUN_00488700`; CEL+)
`0x274` altura (linhas) · `0x278` largura · `0x27c` flags · `0x280/284` PRE0/PRE1 · `0x288/28c` hddx/hddy · `0x290/294` hdx/hdy · `0x298/29c` hddx/hddy `>>4` · `0x2a0/2a4` hdx/hdy `>>4`
· `0x2a8/2ac` vdx/vdy · `0x2b0/2b4` xpos/ypos (16.16) · **doubles**: `0x2b8/2c0` hddx/hddy×2⁻²⁰ · `0x2c8/2d0` hdx/hdy×2⁻²⁰ · `0x2d8/2e0` vdx/vdy×2⁻¹⁶ · `0x2e8/2f0` xpos/ypos×2⁻¹⁶.
Framebuffer: `0x258` ponteiro da RAM · `0x348` base de **escrita** (= registrador `0x1000012c`), `0x35c` passo de escrita (= `cel_w220`) · `0x34c` base de **leitura do destino** (= reg `0x128`), `0x358` passo de leitura (= `cel_w21c`) · `0x350/354` limites **inclusivos** de recorte (x,y; de reg `0x124`: `&0x3ff` e `>>16&0x3ff`).
Textura decodificada: `0x268` ponteiro da entrada de cache (+0x5c largura, +0x60 altura, +0x64 flag "pós-conversão PPMP", +0x78 dados) · `0x260/264` origem x/y dentro da textura · `0x270` ponteiro-fonte na RAM.
Endereçamento de pixel (RAM e framebuffer): `passo*(y>>1) + (y&1)*2 + base + x*4`, valor de 16 bits em `(endereço ^ 2)`; leitura/escrita fora de 3 MB = 0 / ignorada.

## 2. `FUN_00480290(CEL)` — três caminhos
Se `start_col = PRE0>>24 & 0xf` ≥ largura, não faz nada (a conta `largura - start_col` vira o número de colunas).
**Caminho 1 – blit 1:1 alinhado** (só sem ponteiro de textura de 32 bits **ou** com ele): vale quando `|hdx|==0x100000, hdy==0` (ou eixos trocados) **e** `|vdx|==0x10000, vdy==0` (ou trocados), i.e. escala 1, rotação múltipla de 90°/espelho. Antes faz o **descarte por enrolamento** (seção 3). Ordena os 4 cantos, recorta em `[0..clip]` e varre texel→pixel; para cada texel: `cor = FUN_0047ca50(...)` (ou lê da textura decodificada); se bit 31 = 0, lê o destino, passa por `FUN_0047d0d0(cor, destino)` e grava.
**Caminho 2 – plot por texel** (`+0x10000c8==1` ou flags bit 12 `0x1000`, e `FUN_0047ed60(CEL,largura)` falso): para cada texel, projeta o paralelogramo e chama `FUN_0047ef90` (textura em 32 bits convertida ou fonte crua) ou `FUN_0047f9e0` (textura já pós-PPMP); aborta a linha (`break`) se o auxiliar devolve ≠0.
**Caminho 3 – malha de triângulos em float** (caso geral, "HLE"): calcula os 4 cantos em double, o comprimento da diagonal (`sqrt` duplo), escolhe uma subdivisão em quadrados (vetores `afStack_468[224]`) e chama `FUN_0047e3c0(CEL, v0, v1, v2)` por triângulo (recorte com tolerância `0xb030d8`, constantes `0xb030c8/0xb030e0`). `FUN_00481320` (278 linhas) é o outro desenhador de triângulo/escalas — **não lido em detalhe**.
`FUN_0047ed60(CEL, n)`: testa se os 4 cantos do quadrilátero (a n-ésima coluna) têm o **mesmo enrolamento** que as flags; devolve `flags&0x60000 == enrolamento` (usado para escolher entre caminho 2 e fallback).

## 3. Descarte por enrolamento (portado)
`cross = ((vdx+hdx)*(hdy−vdy) + vdy*vdx) − hdy*hdx` (em doubles dos `0x2c8..0x2e0`) `= vdx*hdy − hdx*vdy`. `enrolamento = cross<0 ? 0x20000 : 0x40000`.
O CEL é **descartado** se `flags&0x60000 != 0x60000` **e** `flags&0x60000 == enrolamento`. (Em `FUN_00488700` o desenho só ocorre quando `flags&0x60000 != 0`; com os dois bits em 1 nunca descarta.) Atenção: os nomes ACW/ACCW seguem o cabeçalho 3DO; o que está provado é a máscara e a comparação.

## 4. Código novo (cel.c/h) e testes
`cel_cross`, `cel_winding`, `cel_culled`, `cel_is_unit_blit`, `cel_fb_addr`, `cel_fb_read16/write16`. `test_cel.c`: 35 verificações (12 novas: sinais de `cross`, descarte com cada combinação de bits, blit 1:1 com rotação de 90°, endereços par/ímpar, limite de 3 MB). `make` e `make asan` limpos. Valores esperados vêm da decompilação, **não de execução do .exe**.

## Aberto (próxima rodada)
1. `FUN_0047d0d0` — fim do pipeline de pixel (PPMP/PIXC) para fechar o blit 1:1 de ponta a ponta.
2. `FUN_0047ef90`, `FUN_0047f9e0`, `FUN_0047e3c0`, `FUN_00481320` (desenhadores); `FUN_004860d0`/`FUN_00704200`/`FUN_00703e20` (desenho a partir de textura e fila assíncrona, lado OpenGL).
3. Validar os campos `0x10000c8` e `flags&0x1000` (o que ativam).
