# Parte 9 — CPU ARM60 (FUN_00423870), lida com objdump (sem Ghidra)

Método: `objdump -d -M intel` em 0x422800–0x4242b0 + leitura das tabelas em .rdata (0xaf63b0, 0xaf6304, 0xaf66a0, 0xaf66e0).

## Layout da struct da CPU (this = rcx)
| Offset | Uso (observado) |
|---|---|
| +0x10 + (ptmf) | objeto de barramento; cada acesso é um ponteiro-para-método (ptr em +X, ajuste em +X+8) |
| +0x28/0x30/0x38 | hook chamado após troca de modo |
| +0x40/0x48/0x50 | hook de SWI (HLE); se nulo usa o byte em +0x58 |
| +0x60/0x68 write32 · +0x70/0x78 write8 · +0x80/0x88 read32 · +0x90/0x98 fetch · +0xa0/0xa8 read8 |
| +0xd0 + 4·i | R0..R15 (R14 = +0x108, **R15 = +0x10c**) |
| +0x168 + 4·i | SPSR dos bancos 1..5 (FIQ +0x16c, IRQ +0x170, SVC +0x174, ABT +0x178, UND +0x17c) |
| +0x180 | CPSR (flags no nibble alto) |
| +0x184 | contador de ciclos da instrução, **negativo** (começa em −1; retorno = −valor) |
| +0x188 | carry-out do shifter (bit 0) |

Divergência com o HANDOFF: ele dizia CPSR em +0x1c8; o passo usa +0x180 para flags/modo. Não resolvi qual é cópia de qual.

## Passo (resumo)
1. `opcode = fetch(PC)`; `PC += 4` (R15 guarda o endereço da instrução; leituras de R15 somam mais 4, ou 8 com shift por registrador).
2. Condição: `(tab[op>>28] >> (cpsr>>28)) & 1`, tab em 0xaf66a0 = `f0f0 0f0f cccc 3333 ff00 00ff aaaa 5555 0c0c f3f3 aa55 55aa 0a05 f5fa ffff 0000`. Falhou ⇒ 1 ciclo.
3. Despacho por `idx = ((op>>23)&0x1e) | bit4` (tabela 0xaf63b0, 32 entradas):
   - 0,2 → DP reg c/ shift imediato · 1 → MUL/MLA (máscara 0x0fc000f0==0x90) senão DP shift-reg · 3 → SWP (0x0fb00ff0==0x01000090) senão DP shift-reg
   - 4–7 → DP imediato (rotação ×2) · 8–11,12,14 → LDR/STR (12/14 = offset por registrador) · 16–19 → LDM/STM
   - 20,21 → B · 22,23 → BL (LR = pc+4; destino = pc+8+sext(imm24)·4)
   - 30,31 → SWI · 13, 15, 24–29 → instrução indefinida (coprocessador incluso)
4. Tabela de modo (0xaf66e0, índice = CPSR&0x1f): 0x10→0, 0x11→1 FIQ, 0x12→2 IRQ, 0x13→3 SVC, 0x17→4 ABT, 0x1b→5 UND; outros = inválido (255).
5. SWI: chama hook (+0x40); se não tratou: SPSR_svc=CPSR, CPSR|=0x80, `FUN_00422800(0x13)`, modo=SVC, hook +0x28, LR=PC, PC=8. Indefinida: igual com modo 0x1b, SPSR em +0x17c, PC=4.
6. Flags lógicas: se `(0xaa0a000a >> ((op>>20)&0x1f)) & 1`, C ← carry do shifter (+0x188).
7. DP com Rd=PC e S=1: CPSR ← SPSR do banco atual (via 0x9369b0).

## Ciclos observados (constantes no código)
B/BL = 5 · SWI/indefinida = 5 · DP shift-reg = 2 · DP com Rd=PC = 5 · LDR = 5 (Rd=PC: 9) · STR/STRB = 6 · SWP = 8 · MUL = p/2+2, p = bit mais alto de Rs (Rs=0 → 2); MUL com Rd==Rm zera Rd; S só mexe em N,Z.

## Não lido ainda (e por isso estimado ou simplificado em `arm60.c`)
`FUN_00423010` (shifter), `FUN_00422860` (ALU; escrevi a ALU pelo manual ARM), `FUN_004231d0`/`00423510` (LDM/STM; ciclos estimados), `FUN_00423170`/`004234c0` (write-back/PC em LDR/STR), corpo de `FUN_00422800` (troca de banco; reescrita pelo manual), MLA (não vi ciclo extra), FIQ/IRQ no laço `FUN_004a0920`.

## Teste
`make test_arm60`: laço de soma, STR/LDR, MUL, SWI (SVC, LR, vetor 8, I=1), condição, B, IRQ. Passa. Isso só confirma que o código segue o que li aqui; **não prova equivalência com o .exe** — isso exige savestate A/B no seu PC.
