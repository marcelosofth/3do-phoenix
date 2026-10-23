# Parte 10 — Fechamento do ARM60 (shifter, ALU, LDM/STM, troca de banco)

Lidas por objdump: FUN_00423010, FUN_00422860, FUN_004231d0, FUN_00423510, FUN_00423170, FUN_004234c0, FUN_00422800 (+ 421eb0…422680), FUN_009369b0.

## Shifter — FUN_00423010 (tabela 0xaf639c)
Tipos 0 LSL, 1 LSR, 2 ASR, 3 ROR, 4 RRX. O chamador converte: shift imediato 0 em LSR/ASR vira 32; ROR #0 vira RRX. Shift por registrador usa só o byte baixo.
Quantidade 0: valor igual, C inalterado. LSL/LSR: >32 → 0 e C=0; =32 → 0 e C=bit0/bit31. ASR ≥32 → sinal e C=bit31. ROR com (amt&31)==0: valor igual e C=bit31. Bate com o manual ARM.

## ALU — FUN_00422860 (tabela 0xaf631c, índice = (op>>20)&0x1f = opcode·2+S)
Recebe (this, op, Rn, operando2, &Rd); devolve 1 = "não mexa no PC".
- **TST/TEQ/CMP/CMN sem S são MRS/MSR** (ARMv3) — faltavam na versão da parte 9:
  - idx 16 MRS Rd,CPSR · idx 20 MRS Rd,SPSR
  - idx 18 MSR CPSR: se op bit 16 e modo≠USR → CPSR completo via FUN_009369b0; senão só flags (bits 31:28)
  - idx 22 MSR SPSR: com bit 16 e modo≠USR grava `v & 0xf00000df`; senão só flags
- Demais: AND EOR SUB RSB ADD ADC SBC RSC TST TEQ CMP CMN ORR MOV BIC MVN. Lógicas com S: N,Z do resultado, C do shifter, V preservado. Aritméticas: C/V padrão.
- Com Rd=PC e S: os flags são calculados e depois o SPSR é restaurado (em USR ficam os flags da operação).

## CPSR de volta — FUN_009369b0
`FUN_00422800(v|0x10)`; `CPSR = (v & 0xf00000df) | 0x10`; hook +0x28. É usada por MSR, LDM^ e DP com Rd=PC e S. Bit T (0x20) e bits 8–27 são descartados.

## LDM — FUN_004231d0 / STM — FUN_00423510
Endereço inicial conforme P/U (pós-dec: base−4n+4; pós-inc: base; pré-dec: base−4n; pré-inc: base+4). Leitura/escrita por ponteiro de host (+0xb0/+0xc0) com caminho lento por palavra.
- **LDM:** writeback **antes** das cargas (carga em Rn vence). Com S e sem PC na lista: carrega no banco USR, e o writeback também vai ao banco USR. Com S e PC: restaura CPSR do SPSR. Ciclos: 1+3+n (com PC: 5+3+n).
- **STM:** r0–r14 em ordem, PC (bit 15) à parte, gravado como pc+12. Se existe registrador na lista abaixo de Rn, o writeback vem **antes** (grava base nova); senão vem depois (grava base antiga). Com S lê o banco USR. Ciclos: 1+4+n.
- LDRT/STRT (P=0,W=1) usam FUN_00423170/004234c0 (banco USR).

## Troca de banco — FUN_00422800 (tabela 0xaf6304 → 6 rotinas por modo novo; cada uma com tabela por modo antigo)
Armazenamento (offsets na struct): USR r8–r12 em 0x110–0x120, USR r13/r14 em 0x124/0x128, SVC 0x12c/0x130, ABT 0x134/0x138, FIQ r8–r14 em 0x13c–0x154, IRQ 0x158/0x15c, UND 0x160/0x164, SPSR 0x168+4·i. Saindo de FIQ: guarda r8–r14 e restaura r8–r12 do USR. Entrando em FIQ: guarda r8–r12 do USR. Modo igual ao atual: nada. Modo inválido (tabela 0xaf66e0 = 255): nada. O FIQ não copia r8–r12 do USR: começa com o que tiver guardado (0 após reset).

## Código
`arm60.c` reescrito com tudo isso (bancos, user_get/user_set, ALU com PSR, shifter, LDM/STM, LDRT/STRT, MLA com Rd==Rm devolvendo Rn). `make` roda 22+ checagens do ARM60 e as do CLIO; passam.

## Ainda aberto
- Laço FUN_004a0920 (quando o IRQ/FIQ entra entre instruções, relógio da CPU) — não relido nesta rodada.
- Caminho lento de leitura/escrita (despachador de memória FUN_0049fda0/fea0) e o MMIO do MADAM/CLIO: ligação CPU↔CLIO ainda não feita.
- Os testes confirmam coerência com o que li, não equivalência com o `.exe`: falta o teste A/B por savestate no seu PC.
