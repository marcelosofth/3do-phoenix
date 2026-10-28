# Phoenix 3DO — RE, parte 6: plano de port para FreeDO/Opera

Base: partes 1–5 (`HANDOFF.md`, `PARTE2–5.md`). Este plano não depende do patch da parte 5 ter sido aplicado, mas
a **Fase 1 é exatamente esse patch**. Nada aqui foi compilado ou testado (sandbox sem rede); são decisões de engenharia
tiradas do que a decompilação mostrou.

## 1. Escopo realista

| Componente do Phoenix | Portar? | Motivo |
|---|---|---|
| Diferenças de registradores CLIO (parte 3/5) | **Sim** | Pequeno, localizado, verificado |
| SPORT, XBUS sem dispositivo | **Sim** | Pequeno, verificado |
| Relógio por ticks (VDLPTickAccum, AudioTickAccum, TimersTickAccum) | **Sim, experimental** | Afeta timing/VBL; só entendido pelos nomes dos campos |
| MADAM Poke/Peek (parte 2/4) | **Sim, após comparação linha a linha** | Ainda não comparado com o Opera |
| CEL engine / VDLP lógicos | **Só após achar as vtables** | A parte 4 mostrou que o que decompilamos eram construtores |
| DSP recompilador/otimizador | **Não** | Específico do Phoenix, invalida código por palavra, depende de thread |
| Threads DSP/CEL ("Multy Task") | **Não** no core libretro | Libretro prefere determinismo e savestate simples |
| Renderização GL, pproc | **Não** | Fora do núcleo; o Opera já tem seu renderizador |
| HLE do kernel (ARMQuota, IdleTask…) | **Não por enquanto** | Heurística de desempenho, não de fidelidade |
| Qt UI, netplay, Crypto++, AngelScript | **Não** | Irrelevante ao núcleo |

Regra: **só portar o que muda comportamento observável do hardware**, não o que é otimização do Phoenix.

## 2. Fases

### Fase 1 — Registradores (baixo risco)
Itens 1, 3, 9, 10 da parte 5 (revisão 0x02022000, gate XBUS 0x80, bit 31 do IRQ0). Depois 5, 6, 7, 8 e SPORT/XBUS.
**Fica por último, isolado:** item 2 (DMA sem execução no poke de 0x304), porque muda *quando* o DMA acontece e,
sem o relógio da Fase 2, pode travar jogos que esperam o DMA concluir logo após o poke.
Critério de saída: o Opera continua inicializando a BIOS e chegando ao mesmo ponto que antes.

### Fase 2 — Relógio por ticks (risco médio, ganho incerto)
Hoje só sabemos que `FUN_00491a70` tem acumuladores por subsistema e `TickCounter`. Antes de reimplementar:
1. Reabrir o Ghidra (precisa de rede para baixar de novo) e decompilar o **método virtual** que usa esses acumuladores,
   achado via a vtable (`PTR_FUN_00c240f0`, `DAT_00cc1a70`), e não o construtor.
2. Descobrir a razão ARM:vídeo:timer:áudio por quantum. Os valores NTSC/PAL do CLIO 0x30 (0x15a8/0x613 e 0x15d7/0x620)
   já são uma pista parcial da razão ticks/linha.
3. Só então ajustar o scheduler do Opera. Se a razão for a mesma, **não há o que portar**.
Critério de saída: VBL e timers do Opera coincidem com o Phoenix medidos pelo mesmo número de instruções ARM.

### Fase 3 — MADAM
Comparar `FUN_0048adb0` (Poke) e `FUN_0048b2a0` (Peek) com `opera_madam.c`. Já conhecido: Peek mascara 0x400–0x5ff com
`0xfffffc`; 0x100–0x1ff delega a objeto próprio; 0x7fc é a matrix engine (modos 0–3, ponto fixo 16.16, divisão 64/32).
A matrix engine do Phoenix deve ser comparada contra o Opera com vetores de teste numéricos, porque arredondamento é
onde implementações divergem.

### Fase 4 — CEL e VDLP (só se as anteriores não bastarem)
Exigem as vtables. Prioridade: `CCBFLAGS/PRE0/PRE1` e `OUTCONTROLL` são só descrições de campos; o que importa é como
o motor interpreta cada bit, e isso está nos métodos virtuais ainda não decompilados.

## 3. Validação: usar o Phoenix como oráculo

O Phoenix grava `madam.bin` (0x800 bytes) e `clio.bin` (0x10000 bytes) no savestate, ou seja, o **array de registradores
cru**. Isso permite um teste diferencial sem alterar o Phoenix:
1. Rodar o mesmo disco/BIOS no Phoenix e no Opera por N frames e gerar savestate no Phoenix.
2. Comparar `clio.bin` com o dump equivalente do Opera registrador a registrador.
3. **Ignorar de antemão** os campos que divergem por desenho: bit 31 de IRQ0 (item 10), 0x30 (contador por ticks),
   0x404/0x400 bit 0x80, 0x204/0x20c (espelhos). Todo o resto que divergir é candidato a bug.
Limite honesto: os dois emuladores não rodam em lockstep, então só dá para comparar em pontos de repouso (por exemplo
logo após o boot ou na mesma tela de menu), não em instruções arbitrárias.

Instrumentação complementar: no Opera, logar `(pc, off, val)` de todo Poke/Peek CLIO/MADAM até um ponto fixo e comparar
com o mesmo log feito no Phoenix via breakpoint, se o Phoenix oferecer depurador (as strings sugerem "breakpoint/dirty"
em `+0x159f568`; não verifiquei se há UI para isso).

## 4. Riscos conhecidos

- **Item 2 (DMA sem execução imediata)** pode ser regressão no Opera. Mantê-lo atrás de uma flag de compilação.
- **Revisão 0x02022000:** a BIOS pode escolher caminhos de código diferentes. Testar com mais de uma BIOS.
- **Licenças:** Opera/FreeDO têm licenças próprias e o Phoenix é um binário fechado. Este trabalho reimplementa
  **comportamento observado**; evitar copiar código decompilado literalmente para o repositório final.
- **Cobertura de jogos:** não testei nenhum jogo, então não afirmo ganho de compatibilidade.

## 5. Entregáveis propostos para as próximas partes

- P7: patch real no Opera (depende de rede ou dos fontes, como na parte 5) + build.
- P8: vtables e relógio por ticks (Fase 2), com Ghidra reaberto.
- P9: comparação MADAM linha a linha + vetores de teste da matrix engine.
- Consolidar tudo em `phoenix_clio_poke_peek.md` (seu relatório acumulado), no formato .md que você usa no VS Code.

## 6. O que preciso de você

1. Rede liberada para github.com **ou** os fontes do Opera (`opera_clio.c`, `opera_madam.c`, `opera_sport.c`,
   `opera_xbus.c` + `.h`), como pedido na parte 5.
2. Uma BIOS e um disco de teste **seus** (não envio nem peço arquivos protegidos; use só os que você possui) caso queira o
   teste diferencial da seção 3.
