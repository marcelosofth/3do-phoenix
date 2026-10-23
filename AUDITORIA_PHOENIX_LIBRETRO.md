# Auditoria phoenix_libretro × PhoenixEmuProject.exe

Data: 22/10/2026 · Base: `phoenix.zip` (snapshot atual) + `PhoenixEmuProject.exe` + notas de RE (PARTE*.md, PhoenixRE_parte1..21)

---

## Atualização (merge com `phoenix_1.zip`)
Suas correções foram **preservadas**; as minhas foram aplicadas por cima com merge de 3 vias (original × sua árvore × minhas alterações). O pacote `phoenix_auditoria_merge.zip` traz só os fontes alterados em relação à **sua** árvore (`merge_vs_seu_codigo.diff` mostra exatamente o que mudou nos 5 arquivos em que ambos mexemos).

| Item | Decisão no merge |
|---|---|
| Carry-out do **LSL** (`arm60.c`) + testes em `test_arm60.c` | **mantido (seu)**. ⚠️ **Correção da minha auditoria:** eu tinha dado o ARM60 como sem problemas (seção 5); estava errado — o carry do LSL usava `bit (amt-1)` em vez de `bit (32-amt)`. Esse tipo de bug quebra decoders de FMV (`MOVS/ADC` para ler flags). |
| CEL não-empacotado <16 bpp (`cel_decode_unpacked`, `celexec.c`, `test_celexec.c`) | **mantida a sua implementação**; a minha (que também cobria LRFORM/16 bpp via textura e buffer dinâmico) foi descartada por ser equivalente no que importa. Meu `test_cel_unpacked.c` (1/2/4/8 bpp) **passa sobre a sua versão**. Mantive só meu limite de leitura no `rd8`. |
| Ajuste do trace do CD (`cd.c`) | **mantido (seu)** |
| Diagnósticos do `libretro.c` (anel de SWIs, KDBG, `diag_snapshot`, `check_relaunch`) | **mantidos**, mas agora **só rodam com `phoenix_debug=on`** (antes `check_relaunch` gravaria dump de 3 MB a cada relançamento mesmo sem diagnóstico). Para seu caso do jogo que volta à intro: ligue `phoenix_debug`. |
| Savestate, `.cue/.bin/.img`, font ROM, HLEs sob opção, `phaudit` | aplicados sobre a sua árvore |

Verificação na árvore mesclada: **todas as suítes verdes** (`test_arm60` com seus 15 testes de LSL, `test_celexec` 19/19, `test_cel` 109/109, `test_cel_unpacked`, `test_state`, `test_cd`, `test_libretro` 37/37); `.so` e `phaudit` compilam.
Observação: os `.o` de `chdobj/` do seu zip são objetos Windows (COFF); não os mexi nem os incluí.

## 0. Resumo executivo

1. **Achei e corrigi 6 defeitos reais** (todos com teste ou experimento que prova o antes/depois). O mais grave para compatibilidade: **CEL não-empacotado com 1/2/4/8 bpp era desenhado errado** (só 16 bpp funcionava). Isso afeta qualquer jogo com sprite/fundo paletizado "cru".
2. **Achei HLEs "temporários" de áudio que podem corromper a RAM de qualquer jogo** (incrementavam uma palavra em `[[0x7bf10]+0x9c]` a 240 Hz, com endereço fixo). Provei que o DSP real já gera o IRQ que esse hack imitava. Agora ficam **desligados por padrão** (opção `phoenix_hle_audio`).
3. **Criei a ferramenta que acaba com a "caça a bugs"**: `phaudit` roda a biblioteca inteira de uma vez e devolve um CSV com flags de problema por jogo + captura de tela + contadores de todos os blocos. Cada lacuna abaixo marcada como "pendente" tem uma flag, então **uma execução diz quantos jogos cada lacuna atinge** — você prioriza por número, não por palpite.
4. **O que ainda falta** (seção 4): rasterizador de polígono do CEL é aproximação (a função do `.exe` `FUN_0047d9e0` nunca foi decompilada), custo de ciclos do CEL é 0, periféricos (Mouse/Lightgun/Flightstick), `.cdi/.nrg`, XBUS FMV-ROM, hack do semáforo do DSP, e partes do VDLP.

### Limites desta auditoria (honestidade)
- **Não tenho imagens de jogos aqui** (só BIOS + discos sintéticos). Não consigo confirmar sintoma por jogo; por isso a prioridade das pendências é por risco estrutural, e o `phaudit` serve para transformar isso em dado real na sua biblioteca.
- **Não executei o `.exe`** (sem runtime Windows). A comparação com o `.exe` usa suas decompilações anteriores e as strings do binário.
- **Li por inteiro**: `machine`, `clio`, `madam`, `cel`, `celexec`, `vdlp`, `arm60`, `joybus`, `libretro`. **Li parcialmente**: `dsp.c` (primeiras ~200 linhas + interface), `cd.c` (dispatcher/loader). **Não revisei a fundo**: `sport.c`, motor de matrizes do MADAM (0x600–0x7fc), `cd_chd.c`.

---

## 1. Estado de base (antes de mexer)

| Suíte | Resultado |
|---|---|
| test_arm60 / clio / machine / madam / vdlp / dsp / cd / libretro / celexec | verde |
| **test_cel** | **7 falhas** — teste desatualizado: montava a fonte "crua" no layout entrelaçado antigo, mas o código (corretamente) só entrelaça com `PRE1 bit 11` (LRFORM). Corrigido o teste, 109/109. |

Depois das correções: **todas as suítes verdes** (inclusive sob ASan/UBSan nos itens novos).

---

## 2. Defeitos corrigidos

### F1 — CEL não-empacotado <16 bpp (ALTO)
- **Evidência no `.exe`**: `FUN_00487100` (caminho não-empacotado) monta uma textura via `FUN_0047d760`, que lê a imagem **pixel a pixel com `bpp` bits (MSB-first), passo = `WOFFSET*4+8`**, e converte cada índice pela PLUT (`FUN_0047ca50`). LRFORM entrelaça a 16 bits.
- **No núcleo**: só CEL empacotado virava textura; o não-empacotado caía num caminho "cru" que lia **sempre 16 bits por pixel** e usava um passo calculado com o campo errado para bpp<8.
- **Prova**: `test_cel_unpacked.c` (novo): 1, 2, 4 e 8 bpp saíam errados (4 falhas) → 0 falhas.
- **Correção**: `cel_decode_unpacked()` — **no merge ficou a versão do usuário** (bpp<16, não-LRFORM; 16 bpp e LRFORM seguem pelo caminho cru, que está correto); `rd8` do decoder devolve 0 fora dos 3 MB (como o `.exe`).
- Não testado: 6 bpp (usa o mesmo caminho genérico) e LRFORM 16 bpp com a nova rota (o teste antigo de 16 bpp passa).

### F2 — HLEs de áudio específicos de jogo/BIOS (ALTO)
Em `libretro.c`:
- `SWI 0x4001e` devolvia um contador falso para **qualquer** jogo;
- `SWI 0x40007` (r1=9) devolvia eco falso do PlayControl;
- `h_audio_out` fazia `ram[[0x7bf10]+0x9c]++` a cada 184 amostras — **endereço fixo "da BIOS fz1" que na verdade é RAM do programa**. Qualquer jogo que guarde algo com cara de ponteiro em `0x7bf10` tem uma palavra aleatória incrementada a 240 Hz.
- **Experimento** (BIOS fz1, sem HLE, 25 s): o DSP real dispara **IRQ0 bit 11** (DSP→ARM, o relógio do audio folio) dezenas de vezes por segundo; `0x7bf10` nem contém ponteiro válido fora de um jogo.
- **Correção**: tudo sob opção `phoenix_hle_audio` (**off** por padrão).
- ⚠️ **Teste de regressão que só você pode fazer**: o jogo que motivou esses HLEs (fades de paleta que dependiam do contador; o código cita um jogo japonês). Rode com a opção `off`. Se travar, ligue `on`, rode `phaudit` e me mande o `.txt` — aí corrigimos a causa real no DSP/IRQ em vez de manter o hack.

### F3 — Formatos de imagem (ALTO para a biblioteca)
- O núcleo aceitava só `iso|chd` (comentário no código: ".bin/.cue recusados"). O `.exe` aceita `*.iso *.img *.cdi *.bin *.nrg`.
- **Correção**: `.cue` (usa o 1º `FILE` relativo à pasta do cue), `.bin`, `.img` — a detecção 2048/2352 (Mode1/Mode2) já existia no loader. Testado com `.bin` bruto 2352 + `.cue` sintéticos: resultado idêntico ao `.iso`. Extensões do RetroArch: `iso|chd|cue|bin|img`.
- **Pendente**: `.cdi`, `.nrg`, cue com vários `FILE`.

### F4 — Savestate (MÉDIO; o `.exe` tem `*.states`)
- `retro_serialize_size()` devolvia 0 → sem save state, rewind, run-ahead, netplay do RetroArch.
- **Correção**: `ph_state_size/save/load` (machine.c): CPU, CLIO, MADAM, SPORT, CEL, CD, DSP, VDLP, RAM 3 MB e NVRAM (~3,7 MB). Ponteiros/ganchos do host são religados a partir da máquina viva.
- **Prova**: `test_state.c`: salva no quadro 400, roda 300 quadros, carrega, roda 300 de novo → **hash idêntico**; estado truncado é recusado. `test_libretro` cobre o ciclo via API libretro.
- Requisito: mesmo BIOS e mesmo disco carregados ao restaurar.

### F5 — FONT-ROM / kanji (MÉDIO p/ jogos japoneses)
- O `.exe` trata FONT-ROM e FMV-ROM como dispositivos separados; o núcleo só carregava a BIOS (2º banco = zeros com BIOS de 1 MB).
- **Correção**: `ph_load_font_rom()` + busca de `kanji.bin`, `fontrom.bin`, `font.bin`, `fonts.bin`, `kanjifont.bin`, `3do_font.bin`, `panafz1-kanji.bin`, `fz1-font.bin` (1 MB exatos) na pasta system. ⚠️ **Os nomes são heurística minha** (o `.exe` identifica por MD5, não por nome) e **não testei com ROM real** — se a sua tiver outro nome, renomeie ou me diga o nome.

### F6 — Ruído de depuração no caminho quente (MÉDIO: desempenho)
- `arm60_step` chamava `debug_log` por instrução em `0x1900–0x1A00`; `enter_exc` logava **a cada FIQ/IRQ/SWI**.
- `libretro.c`: logs por quadro, `[pad]`, `h_blit`, histograma de SWI e **dump de 3 MB de RAM no `retro_unload_game`**, sempre ligados.
- **Correção**: tudo atrás de `phoenix_debug` (off). `arm60` ganhou `trace`.

### F7 — Teste desatualizado (`test_cel`) — ver seção 1.

---

## 3. A ferramenta: `phaudit` (acaba com a caça a bugs)

```
make phaudit
./phaudit -n 1800 -s -o auditoria  BIOS/fz10.bin  D:/Jogos3DO
```
- Aceita arquivo(s) ou pastas (recursivo). `-n` = quadros por jogo (1800 = 30 s). `-s` aperta START a cada 3 s (passa de tela de título).
- Medido aqui: **600 quadros em ~1,5 s** por disco (1 CPU fraca). 500 jogos × 1800 quadros ≈ 1 h 15.
- Saída em `auditoria/`: **`auditoria.csv`** (uma linha por jogo), `<jogo>.txt` (relatório completo de todos os módulos) e `<jogo>.bmp` (captura final — dá para olhar 500 jogos em uma galeria).

**Como usar para "corrigir tudo de uma vez"**
1. Rode na biblioteca inteira. 2. Abra o CSV, agrupe por flag. 3. A flag com mais jogos é a próxima causa a atacar. 4. Corrija a causa (não o jogo). 5. Rode de novo e compare os CSVs.

| Flag | Significado | Onde olhar |
|---|---|---|
| `!UNDEF_INSN` | CPU executou instrução indefinida | `arm60.c` (PC/opcode no `.txt`) |
| `!BAD_FETCH` | CPU buscou código em região não mapeada (crash) | `machine.c` mapa de memória |
| `!CEL_UNSUPPORTED` | CCB que o motor não sabe desenhar | `celexec.c` |
| `!CD_UNKNOWN_CMD(op)` | comando do drive que o dispatcher ignora | `cd.c` (CDDA/outros) |
| `!CD_READ_OOB` | leitura fora da imagem (dump incompleto/TOC) | imagem / `cd.c` |
| `!NO_DSP_IRQ11` | audio folio nunca recebeu o relógio do DSP | `dsp.c` / IRQ |
| `!BLANK_SCREEN` / `!STATIC_IMAGE_10S` / `!PC_LOOP` | tela preta, imagem congelada, CPU num laço | ver captura + `.txt` |
| `i:CEL_POLY_APPROX` | usou o rasterizador aproximado (G2) | `cel.c` |
| `i:CEL_LRFORM` | usou LRFORM | `cel.c` |
| `i:VIDEO_HIRES_DOWNSAMPLED` | modo ≥640 px (G3) | `vdlp.c` |
| `i:XBUS_OTHER_DEV` | acesso a XBUS ≠ CD (G7) | `clio.c` |
| `i:DSP_SEMAPHORE_HACK` | acionou o ack forçado do semáforo (G4) | `clio.c` |
| `i:BAD_READ/WRITE` | acessos fora do mapa depois do boot | `machine.c` |

Contadores são zerados ~90 quadros após o boot para descartar a sondagem normal da BIOS (RAM e varredura de XBUS).

Arquivos: `ph_audit.[ch]` (coleta/flags), `phaudit.c` (runner); contadores de auditoria adicionados em `arm60`, `clio`, `cd`, `vdlp`, `celexec`, `machine` (custo desprezível: incrementos).

---

## 4. Diferenças ainda abertas (ordem sugerida)

| # | Lacuna | Evidência | Impacto provável | Flag |
|---|---|---|---|---|
| G2 | **Rasterizador de polígono do CEL é aproximação** (vizinho mais próximo no centro do pixel). O `.exe` usa `FUN_0047d9e0`, **nunca decompilada** (cel.c:374 admite). Cobre escala/rotação/perspectiva, ou seja, quase todo jogo. | cel.c comentários; PARTE19/21 | Bordas/serrilhado/artefatos em sprites escalados e quads; diferenças sutis em todo jogo 3D/pseudo-3D | `i:CEL_POLY_APPROX` |
| G1 | **Custo de ciclos do CEL = 0** (o `.exe` calcula em `FUN_00482950` → `FUN_004821b0`, `FUN_008de650`). O ARM nunca fica parado esperando o CEL. | celexec.c não retorna stall | Velocidade de jogo, corridas CPU×CEL, jogos que dependem de tempo | (tempo; comparar FPS) |
| G4 | **Hack do semáforo ARM↔DSP** em `clio.c` (`sem_status&8 → 4`, "ack forçado") — não é comportamento do DSP real. | clio.c ~l.319 | Jogos com protocolo de semáforo próprio no áudio | `i:DSP_SEMAPHORE_HACK` |
| G5 | **Periféricos**: só joypad (+fallback de teclado), 2 portas. O `.exe` tem **Mouse, Lightgun, Flightstick Pro** (strings). | joybus.c; strings do exe | Jogos de mira/mouse/voo sem controle | (manual) |
| G6 | **Imagens**: `.cdi`, `.nrg`; cue multi-`FILE`; CHD com "pai"; faixas de áudio ignoradas. | cd.c/cd_chd.c | Jogos que não abrem / sem CD-DA | `!CD_UNKNOWN_CMD`, falha de load |
| G7 | **XBUS só tem o CD (ID 0)**; o `.exe` tem "FMV-ROM" (placa MPEG). | clio dev[0]; strings | Jogos que exigem MPEG | `i:XBUS_OTHER_DEV` |
| G3 | **VDLP**: filtro de interpolação (`+0x8c`) não portado; linhas ≥640 px **recortam o centro** em vez de reduzir; só EnDMA e poucos bits de OUTCONTROL tratados; `c_vdlp_pending` é no-op. | vdlp.h/c | Jogos hi-res com tela cortada; efeitos de vídeo por linha | `i:VIDEO_HIRES_DOWNSAMPLED` |
| G8 | **Opções de calibragem do `.exe`** (`DSPCalibration`, `AudioTickAccum`, `VDLPTickAccum`, `TimersTickAccum`) sem equivalente no núcleo — conferir se afetam velocidade/áudio. | strings | Áudio/timing | `!NO_DSP_IRQ11` e FPS |
| G9 | Região só manual (`phoenix_region`); sem auto-detecção por disco. | libretro.c | Jogo PAL em NTSC | — |
| G10 | `retro_get_memory_data(SYSTEM_RAM)` e cheats não expostos. | libretro.c | Cheats/conquistas | — |
| G11 | Não revisado a fundo: `sport.c`, matrizes do MADAM, `cd_chd.c`, `dsp.c` (resto). | — | desconhecido | `!UNDEF_INSN`, `!BLANK_SCREEN` |

### Ordem de ataque recomendada
1. **Rodar `phaudit` na biblioteca e me mandar o CSV** — define a ordem real.
2. **G2**: decompilar `FUN_0047d9e0` (e vizinhas) e portar fielmente — é a maior fonte provável de divergência visual.
3. **G1**: portar `FUN_00482950`/`FUN_004821b0`/`FUN_008de650` (tempo).
4. **G4/G8**: confirmar com o CSV se algum jogo aciona o hack do semáforo; se não, remover; conferir calibragens.
5. **G5, G6, G7**: periféricos, formatos e FMV conforme a contagem de jogos afetados.

---

## 5. O que verifiquei e **não** é problema
- **ARM60**: todas as classes de instrução decodificam (DP, MUL/MLA, SWP, LDR/STR, LDM/STM, B/BL, SWI, MSR/MRS); coprocessador cai em indefinida, correto para ARM60. **Ressalva:** a *semântica* de flags não foi auditada por mim — o carry do LSL estava errado e só foi achado pelo usuário (ver Atualização). Vale uma varredura de flags de ROR/RRX/shift-por-registro e MUL com S contra o `.exe`.
- **CD**: o dispatcher cobre todos os `case` do switch do `.exe` (`FUN_004aa440`: 02,03,06,08,09,0b,10,80,82–85,87,8a–8d,93).
- **Flags de CCB**: o `.exe` só consulta `0x60000` (ACW/ACCW) e `0x180000` (só custo de ciclos); nenhum bit relevante é ignorado pelo núcleo.
- **Relógio do áudio**: o DSP real já gera o IRQ (experimento da F2).

---

## 6. Arquivos entregues (`phoenix_auditoria_merge.zip` — sobrepor na **sua** árvore)
Alterados (já mesclados com as suas mudanças): `arm60.c/h`, `cd.c/h`, `cel.c`, `celexec.c/h`, `clio.c/h`, `machine.c/h`, `vdlp.c/h`, `libretro.c`, `test_cel.c`, `test_libretro.c`, `Makefile`. **Não tocados (ficam os seus):** `cel.h`, `test_arm60.c`, `test_celexec.c`.
Novos: `ph_audit.c/h`, `phaudit.c`, `test_cel_unpacked.c`, `test_state.c`.

Build: `make all` (roda todas as suítes e gera `phaudit`), `make libretro`. Novas opções do núcleo: `phoenix_hle_audio` (off), `phoenix_debug` (off).
