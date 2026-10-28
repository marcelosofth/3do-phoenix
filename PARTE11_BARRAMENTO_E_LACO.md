# Parte 11 — Laço da CPU (FUN_004a0920), despachador de memória e ligação ARM60 ↔ CLIO

Fontes: decompilação `dec_004a0920.c`, `dec_0049fea0.c` e objdump de 0x49fda0, 0x4a0030–0x4a0730.
Resolve a dúvida do HANDOFF: a CPU está em `máquina+0x48`, então o "CPSR em +0x1c8" da máquina é o `+0x180` da CPU (e PC `+0x154` = `+0x10c`).

## Objetos dentro da struct da máquina
| Offset | O que é |
|---|---|
| +0x28 / +0x2c | ciclos totais / ciclos de parada do ARM pelo CEL (stall) |
| +0x40 | multiplicador de relógio: ticks do CLIO = `(mult·ciclos)>>4` |
| +0x48 | CPU ARM60 (parte 9/10) |
| +0x1000dc8 | objeto CLIO (Peek/Poke/tick) |
| +0x1097548 | objeto MADAM; `*(+0x10000ec)==0x10` = CEL ocupado |
| +0x1097558 | BIOS (2 MB, duas bancas de 1 MB) |
| +0x129755c / +0x159755c | RAM 3 MB / NVRAM 32 KB (palavras com o valor 3DO big-endian; byte = endereço^3) |
| +0x1297558 | tamanho da fatia (ciclos entre ticks do CLIO) |
| +0x159f560 | endereço de parada (compara com o PC) |

## Mapa de endereços (região = `addr>>20 & 0xfff`)
| Região | Leitura (FUN_0049fea0) | Escrita (FUN_0049fda0, tabela 0xb03940) |
|---|---|---|
| 0, 1, 2 | RAM (região 2 chama FUN_0058e9f0 se +0x3c==0) | RAM (+ aviso à tabela de watchpoints) |
| 0x30 | BIOS; se CLIO+0x84 bit 2: banco +0x100000 | ignorada |
| 0x60 | BIOS banco 0 (alias) | ignorada |
| 0x31 | NVRAM (byte) só se `(addr&0xc0000)==0x40000`, senão 0xbadacce5 | idem |
| 0x32 | 8 KB em `FUN_004714d0` (+0x10974f0) | `FUN_004714e0` |
| 0x33 | MADAM `FUN_0048b2a0` | MADAM `FUN_0048adb0`; **endereço 0x03300100**: retorno vira stall (+0x2c), se 0 chama `FUN_004894c0(madam,0)` |
| 0x34 | CLIO `FUN_0048f170` | CLIO `FUN_0048f9f0` |
| outras | 0xbadacce5 | ignorada |
Busca de instrução (FUN_004a0030): só regiões 0–2, 0x30, 0x60 (resto 0xbadacce5). Leitura de byte (FUN_004a0130): RAM/BIOS/NVRAM com `addr^3`, resto 0. Escrita de byte (FUN_004a0530): só RAM e NVRAM. FUN_004a0090/004a0340 devolvem ponteiro de host para blocos (aceleração de LDM/STM); o efeito é o mesmo do acesso palavra a palavra.

## Laço — FUN_004a0920 (quantum 0x40000 ciclos)
1. Chama o gancho de início (+0xd0 da vtable de frontend).
2. Repete fatias de `slice` ciclos. Dentro: se `PC == endereço de parada` consome a fatia inteira; se o CEL está ocupado e o ARM **não** está em FIQ, o ARM fica parado (consome `min(slice, stall)`; quando o stall acaba chama `FUN_004894c0(madam,0)`); em FIQ roda e dá `FUN_004894c0(madam,0x30)` por instrução.
3. Executa uma instrução (`FUN_00423870`) e soma os ciclos.
4. **Interrupção do CLIO = FIQ.** Se `(IRQ0 & enable0 & 0x7fffffff) || (IRQ1 & enable1)` e CPSR.F=0: SPSR_fiq=CPSR, CPSR|=0xc0, modo FIQ, gancho de modo, LR=PC+4, PC=0x1c. O IRQ do ARM nunca é usado.
5. Fim da fatia: `total += ciclos; clio_tick((mult·ciclos)>>4)`, reavalia a interrupção; se o orçamento acabou, chama os ganchos de fim e retorna.

## Código (`machine.c`/`machine.h`) e teste
`ph_init`, `ph_read32/ph_fetch/ph_read8/ph_write32/ph_write8`, `ph_load_bios`, `ph_run`. A CPU e o CLIO já conversam: o teste `test_machine` roda um programa que habilita o timer do CLIO, libera o FIQ e confere que o vetor 0x1c é atendido (85 FIQs em ~16 mil ciclos), além do mapa de memória, banco de BIOS, NVRAM, 0xbadacce5 e o stall do CEL. MADAM, dispositivo 0x03200000 e CEL entram por ganchos (NULL = neutro).

## Aberto
- Valor real de `slice` e de `mult` (inicializados em FUN_004a7f60); uso 128 e 16.
- Vetor/estado de reset da CPU e como a BIOS chega à RAM (FUN_004a7f60 / reset); o teste põe PC=0x03000000 à mão.
- MADAM (Peek/Poke, `FUN_0048adb0/0048b2a0`), CEL (`FUN_004894c0`), `FUN_004714d0/e0` e o gancho `FUN_0058e9f0` ainda não reimplementados.
- Tabela de watchpoints (+0x159f568/570) ignorada: é depuração.
- Coerência com o que li, não prova de equivalência com o `.exe`.

## Verificação (11b)
- `make` roda `test_clio`, `test_arm60` e `test_machine`; `make asan` repete o `test_machine` com ASan/UBSan. Todos passam.
- `test_machine` ganhou a seção 4: um programa ARM em RAM exercita `STRB/LDRB` e `STM/LDM` contra o mapa (RAM aceita; ROM ignora escrita de byte e de bloco; NVRAM aceita byte; `LDRB` na região do CLIO devolve 0) e a seção 3b (`vram_touch`).
- Conferido linha a linha contra `dec_0049fea0.c` e `dec_0049fda0.c` (PhoenixRE_parte4.zip): janela de NVRAM, banco de BIOS, alias 0x60, faixa de 8 KB do dispositivo 0x03200000 na leitura, `0x03300100` e o stall.
- **Corrigido:** `FUN_0058e9f0` só é chamada na *leitura* de 32 bits da região 2 e só se `*(máquina+0x3c)==0` (novo campo `vram_flag`); a escrita de 32 bits **não** chama (só consulta a tabela de watchpoints).
- **Não verificado (faltam as decompilações no pacote):** `FUN_004a0130` (read8), `FUN_004a0530` (write8), `FUN_004a0030` (fetch), `FUN_004a0090/004a0340` (blocos). Em especial: (a) se read8/write8 chamam `FUN_0058e9f0` na região 2 e com que flag; (b) o índice de NVRAM em read8/write8 — o código usa `(a>>2)&0x7fff` como no read32, mas esta nota (acima) diz `addr^3`; (c) se o fetch repete o read32 byte a byte. Para fechar: decompilar essas 5 funções no Ghidra (`DecompAt`) e comparar.
- O endianness do host é assumido little-endian (`byte = idx^3` sobre `uint32_t`).
