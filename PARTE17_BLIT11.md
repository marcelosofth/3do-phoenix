# Parte 17 — Caminho 1 de `FUN_00480290` (blit 1:1) de ponta a ponta (cel.c, parte 17)

Liga o que as Partes 14–16 deixaram solto: `cel_ccb_read` → `cel_index_to_color` → `cel_pixel` → framebuffer. Fonte: `dec_00480290.c` (linhas 1–375), `dec_00488700.c` (campos derivados). O Ghidra tipou os cantos como `float`; são **inteiros** (cópias `(int)(float)` sem efeito) e foram portados como `int`.

## 1. Quando vale (`cel_blit11` devolve 0 se não valer)
`start_col (PRE0>>24 & 0xf) >= largura` → a função inteira não faz nada (devolve 1). `hddx/hddy != 0` ou escala ≠ 1 (`cel_is_unit_blit`) → **não é caminho 1** (devolve 0; seguem os caminhos 2/3). Descarte por enrolamento (Parte 15, §3) → devolve 1 sem desenhar.

## 2. Algoritmo
1. `n = largura − start_col`. Parte inteira: `hdx_i/hdy_i = (int16)((hd>>4)>>16)`, `vdx_i/vdy_i = vd>>16`, `xp/yp = (int16)(pos>>16)`.
2. Quatro cantos `(x, y, tx, ty)`: **A** `(xp, yp, sc, 0)`; **B** `A + (hdx_i·n, hdy_i·n)`, textura `(W−1, 0)`; **C** `A + (vdx_i·H, vdy_i·H)`, textura `(sc, H−1)`; **D** `C + (hdx_i·n, hdy_i·n)`, textura `(W−1, H−1)` (`H` = altura, `+0x274`).
3. Ordenação (troca posição **e** textura): `y(D) < y(A)` → troca A↔D; `y(C) < y(B)` → troca B↔C; `x(B) < x(A)` → troca A↔B **e** D↔C. Depois disso A→B é sempre x crescente, mesmo com espelho/rotação de 90°.
4. Recorte em `[0..clip]` **inclusivo** (`+0x350/354`): `xs/xe` a partir de `x(A)/x(B)` (negativo → 0; acima do clip → `clip+1`); `xs == xe` → sai. Linhas: `ys = y(A)` (negativo → 0 e pula `−y(A)` linhas; acima do clip → `clip+1`); `ye = y(C)` limitado a `clip+1`.
5. Passos do texel: `sx,sy = sinal(B−A)` (por pixel em x), `cx,cy = sinal(C−A)` (por linha). Início: `tx = tx(A) + cx·pulo + (xs−x(A))·sx`, idem `ty`.
6. Por texel: **fonte crua** (sem entrada de textura): `end = (ty>>1)·(((PRE1>>16)&0x3ff)·4+8) + src + tx·4 + (ty&1)·2`, 16 bits em `end^2` (0 fora de 3 MB) → `cel_index_to_color`; **textura decodificada**: `data[(tex_y+ty)·largura + tex_x + tx]`. Bit 31 = transparente → pula. Textura **já convertida** (`+0x64 ≠ 0`): grava os 16 baixos direto (sem ler o destino). Senão: lê o destino (`rd_base/rd_step`), `cel_pixel`, grava (`wr_base/wr_step`; fora de 3 MB é ignorado).
Endereço de pixel: `passo·(y>>1) + (y&1)·2 + base + x·4` (Parte 15, §1).

## 3. Código e testes
`cel.h/.c`: `cel_tex_t`, `cel_raster_t`, `cel_raster_setup`, `cel_blit11`. `test_cel.c`: **109 verificações** (29 novas, mapeamento derivado à mão): normal, espelho H e V, rotação de 90°, recorte inclusivo, fora da tela (x/y negativos), texel transparente, escala ≠ 1 e `hdd ≠ 0` (devolvem 0), descarte, `start_col ≥ largura`, pipeline lendo o destino, textura convertida e não convertida, origem `tex_x/tex_y` e leitura fora do vetor. `make` e `make asan` limpos; diferencial da Parte 16 continua 0/60 M.

Limites honestos: valores esperados vêm da decompilação, não de uma execução do `.exe`. Duas coisas são **minhas**, não do `.exe`: (a) índice fora da textura decodificada vira "transparente" (o `.exe` leria memória arbitrária); (b) a textura decodificada tem que vir **compacta** (passo = largura, como `FUN_004859b0` copia para a entrada de cache); `cel_decode_packed` devolve passo `0x800`. `cel_raster_setup` só cobre CEL não empacotado.

## Aberto
1. Caminhos 2 e 3 (`FUN_0047ef90`, `FUN_0047f9e0`, `FUN_0047e3c0`, `FUN_00481320`): são o desenho geral (escala/rotação livre); sem eles só sprites 1:1 funcionam.
2. Ligar `cel_ccb_read` + `cel_blit11` ao `cel_exec` do `madam.c` (laço de `FUN_00488700`, custo em ciclos `FUN_00482950`).
3. O que o plano ainda lista como não feito: VDLP, DSP, XBUS/CD, laço IRQ/FIQ `FUN_004a0920`, front-end libretro (ver resposta).
