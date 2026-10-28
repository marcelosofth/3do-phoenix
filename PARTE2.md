# Phoenix 3DO — RE, parte 2: CLIO/MADAM Poke/Peek ENCONTRADOS

(Continuação de PhoenixRE_parte1; ambiente/comandos Ghidra estão em HANDOFF.md da parte 1.)

## Resultado principal
| Papel (nomes FreeDO)        | Função Phoenix | Tamanho | Observação |
|-----------------------------|---------------|---------|-----------|
| mem write32 (despachador)   | FUN_0049fda0  | 217 B   | `(addr>>20)&0xfff` |
| mem read32 (despachador)    | FUN_0049fea0  | 295 B   | idem; retorna 0xbadacce5 p/ região inválida |
| **_clio_Poke**              | **FUN_0048f9f0** | 2326 B | chamada com `this+0x1000dc8`, `addr&0xfffc` |
| **_clio_Peek**              | **FUN_0048f170** | 909 B  | idem; reg 0 retorna 0x02022000 (rev. CLIO) |
| _madam_Poke                 | FUN_0048adb0  | 1243 B  | chamada com `this+0x1d8`; reg = `addr & 0x7fc` |
| _madam_Peek                 | FUN_0048b2a0  | (ver dec) | chamada com `this+0x1d8` |
| Loop de CPU + IRQ           | FUN_004a0920  | 697 B   | testa CLIO IRQ0/IRQ1 e dispara FIQ/IRQ |
| Save/load de estado + .bin  | FUN_004a20a0  | 737 B   | madam.bin / clio.bin / memory_ / nvram.bin |
| CEL engine start            | FUN_0048b310  | 7416 B  | usa MADAM+0x8 ao iniciar |
| VDL/CLUTDMA                 | FUN_00478c50  | 6684 B  | chamado por FUN_00491a70 (ticks) |

## Mapa de memória (despachador, bits 31..20 do endereço)
0x000–0x002 RAM/VRAM (`this+0x129755c`, `this` = struct da máquina; há checagem de "breakpoint/dirty" em +0x159f568 na escrita)
0x030 / 0x060 = ROM BIOS (`this+0x1097558`; se `(CLIO+0x84... 0x10874d8)&4` mapeia diferente), 
0x031 = NVRAM (`this+0x159755c`, `(addr&0xc0000)==0x40000`), 
0x032 (0x03200000–0x03201FFF, 8 KB) = FUN_004714e0/4714d0 com objeto em `this+0x10974f0` (a identificar — candidato: bloco SPORT/sub-sistema),
0x033 = MADAM (+ escrita em 0x3300100 mexe em `this+0x2c` e chama FUN_004894c0 → provavelmente "reset/estado do ARM/DMA"),
0x034 = CLIO.
Estrutura da CLIO: `clio = this+0x1000dc8`; array de registradores em `clio+0x8668c` (= this+0x1087454 ✓ do save-state).
Estrutura da MADAM: `madam = this+0x1d8`; array de registradores em `madam+0x10003a0` (= this+0x1000578 ✓).

## CLIO (offsets de registrador → estado), da Poke FUN_0048f9f0 / Peek FUN_0048f170
Base do array R = clio+0x8668c.  Offsets abaixo = endereço&0xfffc relativo a 0x03400000.
- 0x000 rev (Peek devolve 0x02022000)
- 0x030 contador de linha/"vcount" (usa clio+0x96694; 0x15a8/0x613 NTSC, 0x15d7/0x620 PAL conforme clio+0x86678/0x10ccf)
- 0x040/0x044 IRQ0 set/clr (R+0x40 = clio+0x866cc); bit31 = "qualquer IRQ" recalculado com IRQ1 (R+0x60, clio+0x866ec)
- 0x048/0x04c IRQ0 mask set/clr (clio+0x866d4; Peek faz |0x80000000)
- 0x050/0x054 modo set/clr (clio+0x866dc)
- 0x060/0x064 IRQ1 set/clr (clio+0x866ec); 0x068/0x06c IRQ1 mask set/clr (clio+0x866f4)
- 0x084 → `*(clio+0x86ce2*8..)=val&0xf` (hardware config/"HDELAY"? a confirmar)
- 0x100–0x17F: 32 regs de 16 bits (`val&0xffff`) em R+off → tabela de **timers** (0x100..)
- 0x200/0x204 set/clr (clio+0x8688c) ; 0x208/0x20c set/clr (clio+0x86894) → timer enable/cascade
- 0x220 `val&0x3ff` (clio+0x868ac)  ; 0x300/0x304/0x308 (clio+0x86990, OR no 0x304) → DMA enable/status
- 0x400 `val & 0xffffbf7f` (clio+0x86a8c) → controle/DSP reset; 0x414 Peek → 0x4000
- 0x500–0x5FF: roteia para FUN_00498690/00498500/00498630 (Poke) e FUN_004985d0/00498600/00498540 (Peek) quando clio+0x86a8c bit 0x80 → bloco de **FIFOs / DMA de canais (DMA registers)**; 0x580–0x5bc grupos de 4.
- 0x17D0 .. 0x17FC (dentro de "uVar6==1" → bloco 0x03401000+? na verdade (addr&0xfffc)>>12==1): 0x17d0 Poke/Peek ↔ clio+0x12214/0x12216 (dado do **Expansion bus/XBUS/CD**, com lock FUN_0047b7c0/8e0), 0x17e4/0x17e8 → FUN_00427d90(clio+0x95*8, 1/0) (liga/desliga), 0x17f0 → **LCG aleatório** (x*0x41c64e6d+0x3039 &0x7fffffff, retorna >>16 — é o "random" do CLIO), 0x17f4/0x17f8/0x17fc leituras de status do **controle/joypad (xbus)**.
(Esses mapeamentos vêm da leitura rápida; confirmar contra `dec/dec_0048f9f0.c` e FreeDO clio.c.)

## MADAM
- reg = addr & 0x7fc; Poke: 0x00 → vtable(+0xc0), 0x04 → grava 0x29 em regs[1], 0x08 → regs[2] (CEL control), 0x580 → chama callback ponteiro-membro em madam+0x1000bb8/bc0/bc8 ("PBus/DMA"), 0x5a0–0x5bc → array de 8 palavras (madam+0x1000228), 0x7fc → **MATRIX ENGINE** (4 modos 0..3: multiplica matriz 4x4 em fixed-point 16.16 madam+0x10009a0..0x10009ec, resultado em globais _DAT_00cf4060..6c; modo 3 faz divisão 64/32 c/ madam+0x1000a20/24).
- Peek: FUN_0048b2a0 (ver dec).

## Pendências / parte 3 sugerida
1. Decompilar FUN_0048b2a0 (madam peek) e FUN_004714d0/4e0 (0x03200000).
2. Confirmar mapeamento fino dos regs CLIO (0x100–0x17f timers, 0x500+ FIFOs) comparando com FreeDO clio.c/madam.c linha a linha
   (o usuário tem 3doplay/freedo clonados; github permite git clone de FreeDO aqui).
3. VDLP: FUN_00478c50/00491a70/FUN_0047a577 (strings HEADVDL, CURRENTVDL, CLUTDMA) vs vdlp.c.
4. DSP (0x4647d2 região) e pproc_3do/pproc_3do_lite (FUN em 0x48a81e).
5. Escrever relatório acumulado phoenix_clio_poke_peek.md (usuário prefere .md, sem scripts Ghidra).
