# Parte 16 — Pipeline de pixel PPMP/PIXC (`FUN_0047d0d0`) completo (cel.c, parte 16)

Fecha a pendência nº 1 da Parte 15: o fim do blit 1:1 (`cor → FUN_0047d0d0(cor, destino) → grava`). Decompilação: `PhoenixRE_parte14.zip/dec/dec_0047d0d0.c` (a função inteira, 1,9 KB de C; não foi preciso reimportar o Ghidra).
**Verificação (nova nesta parte):** compilei a própria decompilação do Ghidra como C e comparei com `cel_pixel` em **60 milhões de entradas aleatórias** (todos os campos aleatórios): **0 divergências** (`tools/diff_0047d0d0/`). Isso prova que a reestruturação (sem `goto`) é fiel à decompilação; **não** prova a decompilação contra uma execução do `.exe` (mesmo limite das partes 13–15).

## 1. Entradas
`cel_pixel(p, src, dst)`: `src` = cor do texel (saída de `cel_index_to_color`/textura), `dst` = pixel atual do framebuffer. Devolve o pixel de 16 bits.
Campos lidos (struct `cel_pix_t`; o que `FUN_00488700` copia/deriva, linhas ~378–415 de `dec_00488700.c`):

| campo | offset | origem |
|---|---|---|
| `flags` | `+0x100027c` | flags do CCB |
| `ppmp0`, `ppmp1` | `+0x1000340`, `+0x1000342` | metades baixa/alta da cópia do PPMP (`+0x1000110`) |
| `pixc` | `+0x1000344` | cópia do registrador `+0x1000100` |
| `mode_f8` | `+0x10002f8` | CEL **empacotado**: `pixc>>20 & 3`; senão `PRE1>>12 & 3` |
| `mode_fc` | `+0x10002fc` | bit 0 = `flags & 0x40`; bit 1 = `pixc & 0x8000000` (só se empacotado **ou** `PRE1 & 0x4000 == 0`) |
| `xpos`, `ypos` | `+0x10002b0`, `+0x10002b4` | XPOS/YPOS 16.16 do CCB (constantes durante o CEL; `FUN_00487100` só salva/restaura) |

Resolve o pendente "campos `+0x10002f8/fc` sem nome" da Parte 14: são seletores de **de onde vem o bit 0 / bit 15 do pixel**, não modos de desenho. `cel_pix_setup(p, ccb, pixc)` faz essa derivação.

## 2. Passos (nomes conforme o cabeçalho 3DO; o que está provado são as máscaras)
1. **Metade do PPMP.** `flags&0x100` (POVER) força a metade (`flags&0x80`: 1 = alta); senão vale o bit 15 de `src`. `half` = a metade escolhida. `ms = half&0x8000` (multiplica o **destino** em vez da origem); `ss = half>>6 & 3` (2º operando: 0 nada, 1 constante `half>>1 & 0x1f`, 2 destino, 3 origem).
2. **Bit 15 e bit 0 do resultado (`b15`).** `pixc>>30 < 2`: bit 15 = `pixc` bit 30. Senão (e se `pixc&0x1000000` com `ms` ou `ss==2`) vem do bit 15 do destino; senão conforme `mode_fc`: 1 = bit 15 de `src`; 2 = bit 15 de **xpos**; 0 = bit 15 de **ypos**; 3 = bit 0 de `src` → bit 15. Depois `pixc>>28 & 3` (`mode`): 1 força bit 0 = 1; 3 soma um bit conforme `mode_fc` (1: bit 0 de `src`; 2: bit 15 de **ypos**; 0: bit 15 de **xpos**; 3: bit 15 de `src`; ou bit 0 do destino se `pixc&0x1000000` e `ms`/`ss==2`). **Atenção:** a troca xpos↔ypos entre os dois ramos está assim na decompilação e foi preservada.
3. **Bit 0 do destino** (`pixc>>22 & 3`: 0 zera, 1 mantém, 2 = bit 4, 3 = bit 5) e **da origem** (`mode_f8`: 0 zera, 1 mantém, 2 = bit 4, 3 = bit 5) → `d`, `a`.
4. **Multiplicador por canal** sobre `col = ms ? d : a` (canais: bits 0–4, 5–9, 10–14): `mf = half>>13 & 3`; 0: constante `(half>>10 & 7)+1`; 1: `(a>>16/19/22 & 7)+1`; 2 e 3: `(a>>2/7/12 & 7)+1`. Divisor `>> dv` com `dv = ((half>>8)−1 & 3)+1` (valor 0 vira 4); em `mf==2` o divisor é **por canal**, vindo dos bits 0/5/10 de `a`.
5. **Combinação com o 2º operando** (`ss`): sem USEAV (`flags&0x400`) soma ou XOR (`flags&0x800`), depois `half&1` → `>>1`. Com USEAV: operando `>>` (`half>>4 & 3`; 3 = por canal dos bits 0–1/5–6/10–11 de `a`), `half&4` → extensão de sinal de 5 bits, soma/subtrai (`half&2`)/XOR, `half&1` → `>>1`; `half&8` → **máscara de 5 bits sem saturar**; senão, se `half&6`, piso em 0. `ss==0`: só `half&1` (e o desvio de máscara de 5 bits se USEAV e `half&8`).
6. **Saturação** em `0x1f` e empacotamento `c1<<5 | c2<<10 | c0`; `| b15`; se `mode==0` zera o bit 0; se `!(flags&0x10)` (NOBLK) e `(r & 0x7fff)==0` → `r |= 0x400`.

Quirks preservados (conferidos no diferencial): a constante `half>>1 & 0x1f` **sobrepõe** os bits 5:1 que em USEAV valem deslocamento (5:4), sinal (2), subtração (1) e máscara (3); só `ss==1` usa a constante, mas com USEAV esses bits também comandam o resto.

## 3. Código e testes
`cel.h/.c`: `cel_pix_t`, `cel_pix_setup`, `cel_pixel`. `test_cel.c`: **80 verificações** (45 novas, valores calculados à mão: identidade, meia intensidade, soma com saturação, XOR, USEAV com soma/subtração/sinal/máscara, `mf=0..3`, `ms`, bit 0/15 em cada modo, `cel_pix_setup`). `make` e `make asan` limpos.
`tools/diff_0047d0d0/`: `orig.c` (decompilação pura), `main.c` (harness), `build.sh` — roda os 60 M de casos.

## Aberto (próxima rodada, Parte 17)
1. Ligar o blit 1:1 de ponta a ponta: laço de `FUN_00480290` caminho 1 (corte, ordenação dos cantos) usando `cel_pixel` + `cel_fb_*`; teste com um CEL real pequeno.
2. Desenhadores `FUN_0047ef90`, `FUN_0047f9e0` (caminho 2), `FUN_0047e3c0`, `FUN_00481320` (caminho 3).
3. Origem do registrador `+0x1000100` (o `pixc`) em `madam.c`: confirmar o nome/endereço MADAM.
