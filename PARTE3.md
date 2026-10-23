# Phoenix 3DO — RE, parte 3: CLIO Phoenix × Opera (libretro/opera-libretro, descendente do FreeDO)

Referência usada: `libopera/opera_clio.c` (`opera_clio_poke` / `opera_clio_peek`) clonado de github.com/libretro/opera-libretro.
(FreeDO/3doplay original não foi clonado nesta parte; Opera é o descendente mais completo e mantido. Se quiser, a parte 4 repete contra o freedo.)
Phoenix: `_clio_Poke` = FUN_0048f9f0, `_clio_Peek` = FUN_0048f170 (receber `this+0x1000dc8`, `addr & 0xfffc`; array de regs R = clio+0x8668c).

## Igual (comportamento equivalente)
- IRQ0 set/clr 0x40/0x44, mask set/clr 0x48/0x4c, IRQ1 set/clr 0x60/0x64, mask 0x68/0x6c (mesma semântica "bit31 = secundário").
- Timers 0x100–0x17c: escrita mascarada `& 0xffff`; 0x200/0x204 e 0x208/0x20c set/clr com espelho em 0x204/0x20c na leitura.
- 0x220 → `val & 0x3ff` (delay do timer).
- 0x300 (DMA stop): limpa bits em reg 0x304 e zera, para cada um dos 13 FIFOs de entrada (0x400+i*0x10) e 4 de saída (0x500+i*0x10), os 4 registradores — idêntico ao Opera.
- 0x308 (DMA clear) e leitura 0x308 → reg 0x304.
- 0x3c (RandSample) ignora escrita; 0x17f0 lê LCG `x*0x41c64e6d+0x3039 & 0x7fffffff`, retorna `>>16` (Opera usa prng32; Phoenix mantém seed em clio+0x12244).
- DSP: N-mem 0x1800–0x1fff (2 palavras/ARM word), 0x2000–0x2fff (16 bits), I-mem 0x3000–0x33ff (2 palavras), 0x3400–0x37ff (1 palavra); leitura 0x3800/0x3c00 equivalente.
- XBUS: 0x500..0x53f/0x540/0x580/0x5c0 divididos em 4 blocos de 0x40 (res/poll/fifo-status/fifo-data); Phoenix só aceita se R+0x400 bit 0x80 estiver setado.
- 0x17d0 semáforo DSP/ARM (Phoenix: `clio+0x12216 = val; clio+0x12214 = 8`, com mutex quando DSP está em thread).

## DIFERENÇAS (candidatas a explicar compat./bugs; priorizar de cima para baixo)
1. **Revisão do CLIO (reg 0x00 Peek):** Phoenix retorna **0x02022000**; Opera retorna 0x02020000. Muda o que BIOS/kernel detectam (revisão do chip).
2. **DMA start (0x304 poke):** Phoenix só faz `R[0x304] |= val`; **não executa o DMA no poke** (Opera chama `clio_handle_dma(val)` + hack TIMER_VAL). Phoenix move os FIFOs por scheduler/ticks (ver strings DMA-WORDS FUN_004899d0, CLUTDMA FUN_00478c50) e antes do 0x300 sincroniza com a thread (vtable+0x40, arg 3, se flag em clio+0x86680).
3. **XBUS direction (0x400 poke):** Phoenix: `R[0x400] = val & 0xffffbf7f`; **se `(val & 0x800)==0` força `|= 0x80`**; se bit 0x800 setado só grava (sem 0x80) — Opera: se 0x800 setado ignora a escrita, senão grava val. O bit 0x80 habilita os registradores XBUS 0x5xx (gate).
4. **0x500–0x5ff:** Phoenix tem lógica extra: para 0x500–0x53f (>>4 em 0x50..0x53) guarda val em R+0x...b8c (clio+0x86b8c) e seta bit 0x40 em R[0x400]; 0x580–0x5bf → FUN_00498500 / 0x5c0–0x5ff → FUN_00498630 (Poke); Peek 0x580–0x5bf → FUN_00498600, 0x5c0–0x5ff → FUN_00498540, outros → FUN_004985d0 (Opera: res/poll/status/data).
5. **0x84 (ADB/ROM):** Phoenix grava só `val & 0xf` (em clio+0x10874d8) e a visibilidade da ROM/mapeamento é decidida **na leitura** (despachador 0x0049fea0 testa bit 0x4 dessa palavra para 0x30xxxxx). Opera faz adbio_set por bit + `opera_clio_set_rom()` na escrita.
6. **HCNT/VCNT (0x30/0x34/0x08/0x0c):** Phoenix **calcula 0x30 a partir de um contador de ticks** (clio+0x96694; NTSC 0x15a8 ticks/linha → 0x613 hcnt; PAL 0x15d7 → 0x620) tanto no Poke quanto no Peek (modo NTSC/PAL em clio+0x10ccf/0x86678). 0x34 (VCNT), 0x08/0x0c (VINT) **não têm case**: escrita crua `R[off]=val` sem máscara (Opera mascara VINT_MASK/HCNT_MASK/VCNT_MASK|FIELD).
7. **CSTAT 0x28 e WATCHDOG 0x2c:** **sem tratamento no Poke** (escrita crua). Opera trata reset soft/DIPIR/clear-on-zero (0x28) e restart do watchdog (0x2c). Peek 0x2c: Opera retorna 0 — Phoenix lê cru. Se algum jogo/BIOS depender de CSTAT/DIPIR, isto é um ponto de divergência.
8. **Modo 0x50/0x54:** Phoenix implementa set/clr em clio+0x866dc (`|=`/`&=~`); Opera tem `#if 0`.
9. **0x4c (mask clr):** Phoenix: `R[0x48] = ~val & R[0x48] | 0x80000000` (mantém bit31 ligado); Opera limpa explicitamente CLIO_INT0_SECONDARY. Na leitura 0x48/0x4c os dois acabam com bit31 = 1.
10. **Peek 0x40/0x44:** Phoenix devolve `R[0x40]` direto (bit31 é recalculado no Poke de 0x40/0x44 com base em IRQ1&mask: `R[0x60]!=0`); Opera recalcula no Peek com `R[0x60] & R[0x68]`. Phoenix usa `R[0x60]` **sem** AND com a máscara 0x68 — comportamento diferente.
11. **DSP: invalidação de código compilado.** Poke 0x2000–0x2fff compara a palavra nova com um *shadow* (`clio+0x10d3c`) e com uma máscara por palavra (`clio+(idx+0x45)*8+0x96`); se mudou e a thread de DSP está ativa, ele **para a thread** (FUN_0047ac10 espera, FUN_0047ac00 yield) antes de gravar — é o mecanismo do recompilador/otimizador de DSP (os arquivos gerados `dsp_3do_instruments*.cpp`). Opera não tem.
12. **DSP controle:** Phoenix trata **0x17e4 → FUN_00427d90(dsp,1)** e **0x17e8 → FUN_00427d90(dsp,0)** (Opera só 0x17e8 = reset); 0x17fc grava `val&1` em clio+0x12264 (Opera: `running = val>0`); Peek 0x17f4/0x17f8 devolvem ushorts de clio+0x1223c/0x1226c e 0x17fc retorna `(clio+0x12260==0) | clio+0x12264` (Opera: leitura crua).
13. **Multithread:** strings "Multy Task DSP"/"Multy Task CEL" + locks (FUN_0047b7c0/8e0 em _DAT_00cf4050) em torno de 0x17d0 e do 0x300 → DSP e CEL engine rodam em threads separadas, sincronizadas no acesso aos registradores.

## Para reproduzir no Opera/FreeDO (sugestão de ordem)
(1) trocar rev 0x02020000 → 0x02022000 e testar boot; (3)(5) verificar gate 0x80 do XBUS e ROM-select na leitura; (6) HCNT por ticks; (7) CSTAT/WDOG — provavelmente o Phoenix confia na BIOS; (10) semântica do bit31 de IRQ0.
Cada item acima aponta para a função/linha no `dec/dec_0048f9f0.c` e `dec/dec_0048f170.c` (parte 2).

## Pendências para parte 4
- Peek completo para 0x400–0x414 e 0x500+ (parte final de dec_0048f170.c não lida: cortar `0x4` e `0x5`).
- FUN_00498500/630/690 (poke FIFO/XBUS) e FUN_004985d0/600/540 (peek): ver se é o CD-ROM XBUS (strings CommandFIFO/StatusFIFO/DataFIFO ~0x4acb09).
- FUN_00427d90 (DSP 1/0) e FUN_0045f8a0 (DSP mem write) para detalhar o DSP.
- MADAM peek FUN_0048b2a0 e CEL (FUN_0048b310 já decompilada).
- VDLP: FUN_00478c50, FUN_00491a70.
