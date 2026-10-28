# Parte 13 — MADAM Peek/Poke, comandos do CEL, SPORT (código novo: madam.c/h, sport.c/h)

Método: Ghidra 12.1.3 headless (mesmos scripts da parte 1) sobre o `PhoenixEmuProject.exe`; REA 6.7.0 compilado e com o
Ghidra reconhecido no `rea doctor` (ver "Ferramentas" no fim). Decompilações em `PhoenixRE_parte13.zip` (`dec/`).
Convenção desta parte: **offsets de MADAM são relativos a `madam` = máquina+0x1d8**; o Peek/Poke recebem o endereço e usam `addr & 0x7fc`.

## Mapa de registradores (FUN_0048b2a0 Peek, FUN_0048adb0 Poke)
| Faixa (`off`) | Poke | Peek |
|---|---|---|
| 0x000 | chama gancho global (vtable+0xc0) com `(char)valor`; **não grava** | cru |
| 0x004 | grava **0x29** em regs[1] (ignora o valor) | cru |
| 0x008 | regs[2] = valor | cru |
| 0x028 | (cru) | **estado do CEL** (`madam+0x1000124` = CEL+0x10000ec: 0/0x10/0x30) |
| outros 0x00..0xfc | cru | cru |
| 0x100..0x1ff | `FUN_00489650(CEL, off-0x100, v)` (abaixo); **retorno vira o stall em 0x03300100** | `cel_w[(off-0x100)>>2]` (`FUN_0047c7c0`) |
| 0x400..0x53f | `v &= 0xfffffc`; se `(off&0xc)<8` zera a palavra em `off+8`; grava | `& 0xfffffc` |
| 0x540..0x57f, 0x584..0x59f, 0x5c0..0x5ff | `v &= 0xfffffc`; grava | `& 0xfffffc` |
| 0x580 | `v &= 0xfffffc`; chama ponteiro-membro `madam+0x1000bb8/bc0/bc8` (nulo = ignora); **não grava** | `& 0xfffffc` (regs) |
| 0x5a0..0x5bc | `v &= 0xfffffc`; grava em array separado `madam+0x1000228` | idem, mesmo array |
| 0x600..0x7f8 | cru | cru |
| 0x7fc | **motor de matrizes** (abaixo); regs[0x1ff] = 0 | cru (sempre 0) |
Correção ao rascunho da Parte 4: a faixa que zera o registrador seguinte é **0x400..0x53f** (o .exe testa `off-0x400 < 0x140`), 0x540 já fica fora.
Registradores: `regs` = `madam+0x10003a0` (= `this+0x1000578`, o `madam.bin` do savestate).

## Bloco 0x100..0x1ff (FUN_00489650) = comandos do CEL
Palavras em CEL+0x10000f0 (`cel_w[64]`). `off-0x100 >= 0xc0` não faz nada.
- **0x100** (idx 0): se estado==0 → estado=0x10, `scope_begin`, `FUN_00488700(CEL, async)` com `async = enabled ? (flag40^1) : 0`, `scope_end`; **retorna o resultado do motor** (stall do ARM). Se já ocupado devolve 0.
- **0x104** (idx 1): `FUN_00704850` (espera a fila assíncrona); se `enabled && estado!=0 && flag40==0` roda `FUN_00488700(CEL,0)`; estado=0.
- **0x108**: 0x30→0x10 (continua). **0x10c**: 0x10→0x30 (pausa).
- **0x130** (idx 0xc): guarda e calcula `cel_w21c`/`cel_w220` (dois tamanhos a partir dos bits 0..5 e 8..13) — fórmula exata em `madam.c`.
- idx 4..0x1f (exceto 0xc): `cel_w[idx] = v`. idx 0x20..0x2f (0x180..0x1bc): `cel_w[2*idx-0x20] = v>>16`, `cel_w[2*idx-0x1f] = v&0xffff`.
- `FUN_004894c0(CEL, n)` (laço da CPU e 0x03300100 com stall 0): se n==0 → sincroniza; se `flag40==0 && enabled && estado!=0 && n==0` roda `FUN_00488700(CEL,0)`; depois `estado=n`.
- **Correção à Parte 11:** `*(máquina+0x1097548)` **não** é "objeto MADAM": é o componente CEL achado por registro + `dynamic_cast` (init em 0x4a894c). O uso idêntico de `+0x10000ec` pelo Peek 0x28 mostra que é o mesmo objeto que `madam+0x38` (= máquina+0x210) — **inferência de alta confiança, não provada**.

## Motor de matrizes (poke 0x7fc, modo = valor)
Matriz 4×4 em 0x600..0x63c (linha i = 0x600+16i), vetor V0..V3 em 0x640..0x64c, produtos em 64 bits, resultado `>>16`. Saída em 4 **globais do .exe** (0xcf406c=out0, 68=out1, 64=out2, 60=out3); **antes de cada operação** as saídas *antigas* são copiadas para 0x660..0x66c (portanto o que o Peek lê em 0x660 é o resultado da operação anterior).
- modo 0: só o latch. modo 1: 4 termos nas 4 linhas. modo 2: 3 termos (V0..V2) nas linhas 0..2, out3 inalterado.
- modo 3: out2 = z (3 termos); `q = ((0x680<<32)|0x684) / z` (se z≠0); out1 = `((int)(y>>16) * q) >> 32`, out0 idem para x.
- modo >3: só zera 0x7fc. (`INT64_MIN / -1` derruba o .exe; o port dá a volta.)

## SPORT (0x03200000, FUN_004714d0/e0) — VRAM = RAM + 2 MB (conferido no ctor 0x4716a0: `[obj+0x50] = máquina+0x149755c`)
- Peek: devolve o `src` **anterior** e grava `src = (off & ~3) << 7` (índice em palavras).
- Poke 0x0000..0x1fff: copia 0x200 palavras de `src` para `(off&0x7fc)<<7` (máscara `((d^s)&v)^s`; `v=0xffffffff` = cópia total).
- Poke 0x2000..0x3fff: guarda a cor. Poke 0x4000..0x5fff: flash write de 0x200 palavras em `(off&0x7fc)*0x80` com a cor (mesma máscara).
- O .exe ainda dispara evento de renderização (vtable+0x78, id 0x10101) e um callback; aqui são os ganchos `event`/`written`.
- Desvio único: `src` fora da VRAM (o .exe lê lixo do host) lê 0.

## Código e testes
- Novos: `madam.c/h`, `sport.c/h`. `machine.c/h`: `madam`/`sport` internos; os ganchos `madam_peek/poke/dev32_*/cel_*` antigos continuam existindo e **substituem** o bloco interno quando não são NULL (os testes antigos seguem iguais; só renomeei duas funções estáticas de `test_machine.c` que colidiam com `madam_peek/poke`).
- `test_madam.c` (novo): máscaras e zeragem, array 0x5a0, gancho 0x580, matrizes (4×4, 3×3, perspectiva, z=0, modo>3 contra cálculo independente), comandos/estados do CEL, SPORT. `test_machine.c` seção 6: MADAM/SPORT/CEL pelo barramento.
- `make` e `make asan` passam (clio, arm60, machine, madam).
- Os valores esperados vêm da leitura da decompilação, **não** de uma execução do .exe.

## Aberto
1. **Motor de CEL `FUN_00488700`** (3,5 KB; chama 00482950, 00480290, 00487100, 004860d0, 004859b0, 00704200) — entra pelo gancho `cel_exec`. É a próxima parte (Fase 4).
2. `slice`/`mult` reais e reset da CPU/BIOS (FUN_004a7f60), VDLP (Fase 4), DSP (5), CD (6).
3. Significado de `cel_flag40` (CEL+0x40) e `cel_enabled` (CEL+0x10000e0): lidos de config; ainda sem nome.
4. O gancho do 0x580 (ponteiro-membro) — quem o registra (provável DMA/PBus).

## Ferramentas (honesto)
- Rede liberada nesta sessão: `npm ci`/build do REA 6.7.0 e download do Ghidra 12.1.3 (MD5 conferido) funcionaram.
- `rea doctor` com `GHIDRA_INSTALL_DIR` absoluto: Ghidra, headless, decompilador nativo e JDK 21 = healthy.
- **Todas as decompilações desta parte vieram do Ghidra headless** (pipeline já validado nas partes 1–4). Iniciei `rea decompile 0x48b2a0 --provider ghidra`, mas o REA refaz a análise inteira do .exe e não terminou a tempo; nas próximas partes uso o REA primeiro e caio no Ghidra direto se ele não responder.
- Cuidado: `analyzeHeadless` rejeita caminho de projeto começando com `.` (use caminho absoluto) e precisa de `DisableHeavy.java` + `GHIDRA_HEADLESS_MAXMEM=6G`.
