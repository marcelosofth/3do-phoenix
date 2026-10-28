# Phoenix 3DO — RE, parte 5: diffs verificados e especificação de patch para o Opera

Convenções: `R` = array de registradores do CLIO (`clio+0x8668c`), `off` = `addr & 0xfffc` relativo a 0x03400000.
Fonte: releitura de `dec_0048f9f0.c` (Poke), `dec_0048f170.c` (Peek), `dec_004714e0.c` (SPORT), `dec_00498*.c` (XBUS).

## 0. Status e bloqueio

- O sandbox desta sessão está **sem rede** (`Could not resolve host: github.com`), então **não foi possível clonar o
  `libretro/opera-libretro`, aplicar o patch nem compilar**. O projeto Ghidra também não existe mais (sessão efêmera).
- Sem o código do Opera, um `.diff` escrito de memória não aplicaria de forma confiável. Por isso esta parte entrega a
  **especificação exata, já verificada contra a decompilação**, no formato de trechos C. Quando o Opera estiver
  disponível (ver "O que preciso de você"), a conversão para diff real é mecânica.

## 1. Correções às partes 3 e 4 (achadas na releitura)

1. **XBUS com gate fechado (parte 3, item 4):** a lógica `R+0x500 = val; R[0x400] |= 0x40` **só roda quando
   `R[0x400] & 0x80 == 0`** (gate fechado), e para escritas em 0x500–0x53f. Em seguida, qualquer escrita 0x5xx com
   gate fechado cai na escrita crua `R[off] = val`. Não é um recurso do XBUS aberto, como a parte 3 sugeria.
2. **XBUS com gate aberto, mapeamento exato do Poke:** 0x500–0x53f → `sel = val & 0xff` (FUN_00498690);
   0x540–0x57f → poll set (FUN_00498660, vtable+? do dispositivo); 0x580–0x5bf → vtable+0x90 (cmd FIFO);
   0x5c0–0x5ff → vtable+0xb0 (data FIFO). Peek: 0x500–0x53f → vtable+0xb8; 0x540–0x57f → poll (+0x98);
   0x580–0x5bf → +0x88; 0x5c0–0x5ff → +0xa8. Sem dispositivo: Peek devolve 0x30 (ou 0 se bit 7 da seleção; ver parte 4).
3. **0x220 (timer delay):** além de `R[0x220] = val & 0x3ff`, o Poke chama um **método virtual +0x78 do próprio CLIO
   com argumento 1** (notificação para recalcular timers). Isso não estava na parte 3. No Opera equivale a recalcular
   o prescaler/reload ao mudar esse registrador.
4. **0x84:** grava `R[0x84] = val & 0xf` e nada mais. O Peek de 0x84 é leitura crua. Confirmado.
5. **SPORT (parte 4, item 3) — mapa corrigido** (offsets dentro de 0x03200000, `addr & 0xffffe000`):
   - região 0x0000: **copia uma linha** (0x200 palavras) da linha-fonte para a linha-destino;
     linha-destino = `(addr & 0x7fc) << 7` (em palavras); com `val == 0xffffffff` é `memcpy`,
     senão aplica máscara bit a bit: `dst = ((dst ^ src) & val) ^ src`;
   - região 0x2000: **só grava o registrador de cor** (`+0x44 = val`);
   - região 0x4000: **flash write** — preenche a linha com a cor (`+0x44`); com máscara,
     `dst = ((dst ^ cor) & val) ^ cor`;
   - Peek (FUN_004714d0): devolve o `+0x48` anterior e grava `+0x48 = (addr & ~3) << 7`.
   - Em todos os casos o Phoenix notifica o renderizador com o evento 0x10101 (vtable+0x78) e depois chama um callback
     de "linha suja" com `addr >> 2`.
   - O "0x77" mencionado na pendência 1 da parte 4 não aparece na decompilação; o evento é **0x10101**. Ignorar o 0x77.

## 2. Diffs CLIO verificados (Opera → comportamento Phoenix)

Tabela de status: OK = confirmado na decompilação; AJUSTE = confirmado com nuance.

| # | Registrador | Comportamento Phoenix (confirmado) | Status |
|---|-------------|------------------------------------|--------|
| 1 | 0x00 Peek | retorna `0x02022000` | OK |
| 2 | 0x304 Poke | `R[0x304] \|= val`, **sem executar DMA**; 0x308 Poke: `R[0x304] &= ~val`; Peek 0x304/0x308 → `R[0x304]` | OK |
| 3 | 0x400 Poke | `R[0x400] = val & 0xffffbf7f; if (!(val & 0x800)) R[0x400] \|= 0x80;` Peek 0x400/0x404 → `R[0x400]`; Peek 0x414 → `0x4000` | OK |
| 4 | 0x500–0x5ff | ver seção 1, itens 1 e 2 | AJUSTE |
| 5 | 0x84 Poke | `R[0x84] = val & 0xf` (ROM-select é decidido no despachador de leitura, bit 0x4) | OK |
| 6 | 0x30 | Poke: `ticks = (((ticks / L) * H + min(val, H)) * L) / H`; Peek: `((ticks % L) * H) / L`, com NTSC L=0x15a8, H=0x613, PAL L=0x15d7, H=0x620. 0x34, 0x08, 0x0c: escrita crua sem máscara | OK |
| 7 | 0x28, 0x2c | escrita crua; 0x2c Peek lê cru; 0x3c Poke é ignorado | OK |
| 8 | 0x50/0x54 | set/clr em `R[0x50]`; Peek devolve `R[0x50]` | OK |
| 9 | 0x4c Poke | `R[0x48] = (~val & R[0x48]) \| 0x80000000`; 0x48 Poke: `R[0x48] \|= val`; Peek 0x48/0x4c → `R[0x48] \| 0x80000000` | OK |
| 10 | 0x40/0x44 | Poke 0x40: `R[0x40] \|= val; if (R[0x60] != 0) R[0x40] \|= 0x80000000`. Poke 0x44: `R[0x40] &= ~val; if (R[0x60] == 0) R[0x40] &= 0x7fffffff`. Poke 0x60: `R[0x60] \|= val; if (R[0x60]) R[0x40] \|= 0x80000000`. Poke 0x64: `R[0x60] &= ~val; if (!R[0x60]) R[0x40] &= 0x7fffffff`. Peek 0x40/0x44 → `R[0x40]` cru. **Nenhum AND com a máscara 0x68** | OK |
| 11 | DSP N-mem 0x1800–0x1fff e 0x2000–0x2fff | só grava no DSP se a palavra mudou fora da máscara de "palavras não otimizadas"; se mudou e a thread do DSP está ativa, para a thread antes | OK (específico do recompilador do Phoenix; **não portar**) |
| 12 | 0x17e4/0x17e8/0x17fc | 0x17e4 → reset com contador carregado (`FUN_00427d90(dsp,1)`), 0x17e8 → reset normal (`dsp,0`), 0x17fc Poke → `R[0x17fc] = val & 1` (em `clio+0x12264`); Peek 0x17fc → `(clio+0x12260 == 0) \| clio+0x12264`; Peek 0x17f4/0x17f8 → ushorts de `clio+0x1223c/0x1226c` | OK |

## 3. Trechos de patch sugeridos (Opera, `opera_clio.c`)

Nomes de variáveis são os do Opera como eu os conheço; ao receber o fonte eu confirmo cada um antes de gerar o diff.

```c
/* 1 — revisão do CLIO */
case 0x000: return 0x02022000;

/* 2 — DMA start sem execução imediata (comportamento Phoenix) */
case 0x304: CLIO.regs[0x304] |= val; break;   /* NÃO chamar clio_handle_dma() aqui */
case 0x308: CLIO.regs[0x304] &= ~val; break;

/* 3 — XBUS direction */
case 0x400:
    CLIO.regs[0x400] = val & 0xffffbf7f;
    if (!(val & 0x800)) CLIO.regs[0x400] |= 0x80;
    break;

/* 10 — IRQ0 bit31 sem máscara */
case 0x040: CLIO.regs[0x40] |= val;  if (CLIO.regs[0x60]) CLIO.regs[0x40] |= 0x80000000; break;
case 0x044: CLIO.regs[0x40] &= ~val; if (!CLIO.regs[0x60]) CLIO.regs[0x40] &= 0x7fffffff; break;
/* Peek 0x40/0x44: devolver CLIO.regs[0x40] cru (remover o recálculo com 0x68) */

/* 9 — mask clear mantém bit 31 ligado */
case 0x04c: CLIO.regs[0x48] = (~val & CLIO.regs[0x48]) | 0x80000000; break;

/* 7 — CSTAT/WDOG crus (opcional; só se quiser reproduzir o Phoenix literalmente) */
case 0x028: case 0x02c: CLIO.regs[off] = val; break;

/* 6 — 0x34, 0x08, 0x0c: escrita crua, sem VINT_MASK/VCNT_MASK */
```

SPORT (`opera_sport.c`): trocar a lógica das três regiões pelo mapa da seção 1, item 5 (cópia de linha, cor, flash write
com máscara `((dst ^ ref) & val) ^ ref`) e preservar "Peek devolve o `+0x48` anterior".

XBUS sem dispositivo: Peek de poll retorna `0x30` (ou `0` se o bit 7 da seleção estiver ligado); gate `R[0x400] & 0x80`
fechado → escrita crua em `R[off]`, e para 0x500–0x53f também `R[0x500] = val; R[0x400] |= 0x40`.

**Ordem de teste recomendada** (menor risco → maior): 1 (rev 0x02022000) → 3 (gate XBUS) → 10 (bit 31 do IRQ0) →
9 → 2 (DMA sem execução imediata; este é o mais invasivo e pode quebrar o timing do FIFO, então teste isolado) →
SPORT → XBUS sem dispositivo.

## 4. Pendências

- **Aplicar e compilar o patch** (bloqueado por rede; ver abaixo).
- Vtables do VDLP/CEL/ticks (parte 4, pendência 2) — precisa reabrir o Ghidra; também depende de rede para baixar o Ghidra.
- MD5s em `.rdata` (a48e6746…, cf11bbb5…, 5df7f212…, dacffcf5…): não verifiquei contra BIOS conhecidas; não tenho
  uma lista de hashes confiável de memória, então não vou afirmar identificação.

## 5. O que preciso de você para fechar a parte 5

Qualquer uma destas destrava o diff real e o build:
1. **Habilitar a rede do sandbox** (Settings → permitir github.com) e eu clono `libretro/opera-libretro`, aplico e compilo; ou
2. **Subir `libopera/opera_clio.c`, `opera_sport.c` e `opera_xbus.c`** (e os `.h` correspondentes) do seu clone local. Com isso
   eu gero o `.diff`, e você aplica no VS Code com `git apply`.
