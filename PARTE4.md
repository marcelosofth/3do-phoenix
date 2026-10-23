# Phoenix 3DO — RE, parte 4: XBUS/CD, MADAM Peek, SPORT, DSP control, estado de VDLP/CEL

Convenções: `this` = struct da máquina; `clio = this+0x1000dc8`; `madam = this+0x1d8`; dec/ tem as decompilações (nomes dec_<addr>.c).

## 1. XBUS (CLIO 0x500–0x5ff) — implementado como tabela de 16 dispositivos virtuais
- Tabela: `clio+0x3e0` = 16 ponteiros de objeto (um por ID de dispositivo); **seleção** = byte em `clio+0x460` (bits 3..0 = índice; bit 7 = flag que mascara a resposta de poll com `&0xf`).
- Poke (FUN_0048f9f0, bloco 0x5xx, só se `R[0x400] & 0x80`):
  - 0x500–0x53f → FUN_00498690: `clio+0x460 = val` (== `opera_xbus_set_sel`).
  - 0x580–0x5bf → FUN_00498500 → método virtual **+0x90** do dispositivo selecionado (== fifo_set_cmd).
  - 0x5c0–0x5ff → FUN_00498630 → método virtual **+0xb0** (== fifo_set_data).
  - 0x540–0x57f (poll set) tratado no mesmo bloco (não decompilado em detalhe).
- Peek (FUN_0048f170):
  - poll → FUN_00498570 → virtual **+0x98**; sem dispositivo: retorna **0x30** (ou 0 se bit7) — com bit7 mascara `&0xf`.
  - 0x580–0x5bf → FUN_00498600 → virtual **+0x88** (status FIFO); 0x5c0–0x5ff → FUN_00498540 → virtual **+0xa8** (dados FIFO); res → FUN_004985d0 → virtual **+0xb8**.
- Dispositivo 0 conhecido = **XBUS/CD-Drive** (strings "XBUS/CD-Drive" 0x4a1600, "SelectedDevice", "Identify", "CD-Drive", CommandFIFO/StatusFIFO/DataFIFO/PollReg/StatusReg/LastError/CurrSector em 0x4acb09–0x4af0d0, tabela de erros MEI_CDROM_*). Isto é o mesmo modelo do Opera (`opera_xbus_*`), mas o Phoenix usa vtable por ID e retorna 0x30 quando não há dispositivo.
- Strings adicionais: CD-ROM, FMV-ROM, FONT-ROM, NVRAM como "dispositivos/mídias" configuráveis; "CD-ROM Speed" opção de UI.

## 2. MADAM Peek (FUN_0048b2a0, 107 B) — reg = `addr & 0x7fc`, `madam` base = this+0x1d8
- 0x028 → palavra de status em `madam+0x1000124` (fora do array de regs).
- 0x100–0x1ff → delega para objeto `madam+0x38` (= this+0x210) via FUN_0047c7c0(offset−0x100) — **bloco de DMA/VDL**, não é array simples.
- 0x400–0x5ff: retorna `regs[off] & 0xfffffc` (endereços de DMA alinhados em 4 e limitados a 24 bits); 0x5a0–0x5bf lê o array de 8 palavras em `madam+0x1000228` (com a mesma máscara).
- demais: leitura crua `regs[off>>2]` (array em `madam+0x10003a0`).
- Poke (parte 2): 0x00 → vtable+0xc0 do objeto global; 0x04 → grava 0x29; 0x08 → MADAM_MCTL?/start CEL; 0x580 → callback; 0x7fc → matriz.

## 3. SPORT (0x03200000–0x03201fff, 8 KB) — objeto em `this+0x10974f0`
- Peek FUN_004714d0: **retorna o valor anterior de `+0x48` e grava `+0x48 = (addr & ~3) << 7`** (fonte da cópia linha-a-linha de VRAM = `opera_sport_set_source`).
- Poke FUN_004714e0(obj, addr&0xfffc, val): se `addr&0xffffe000 == 0x4000`… monta cabeçalho (1, `(addr&0x7fc)<<9`, valor guardado) e chama método virtual +0x78 (evento 0x10101) e depois **copia 0x200 palavras** (linha de VRAM) de `[+0x50]` para a linha destino; se `val == 0xffffffff` preenche a linha com o valor "color" de `+0x44` (flash write); senão aplica máscara. → Corresponde ao SPORT do 3DO (cópia de linha / flash write, usado para limpar/copiar framebuffers).
- Diferença notável: o Phoenix dispara um evento virtual (+0x78) para o renderizador (OpenGL) a cada operação SPORT — o buffer de vídeo não é só RAM.

## 4. DSP — controle (FUN_00427d90 e FUN_0045f8a0)
- DSP é o objeto em `clio+0x4a8` (`param_1 + 0x95` em unidades de 8 bytes).
- `FUN_00427d90(dsp, flag)` (chamada por 0x17e4 com 1 e 0x17e8 com 0): se a thread do DSP está ativa (`dsp+0x28`), seta `dsp+0x34=1` e **espera** (FUN_0047ac10/ac00) ela parar; depois reinicializa: PC/estado (`dsp+0x11da0 = 0x237` se flag==0, senão `= *(dsp+0x11d72) - 1`), `+0x11dbc |= 4`, zera contadores `+0x11d96/98/a4/a2/c0`. Ou seja: 0x17e8 = **reset normal (PC=0x237…)**, 0x17e4 = **reset/restart com contador carregado** (variante que o Opera não implementa).
- `FUN_0045f8a0(dsp, idx, val16)`: `if (idx<0x100) imem_shadow[idx&0x7f] = val` em `dsp+0x11894` (I-mem de 128 entradas… cache/shadow usado pelo otimizador de instruções; cada escrita 0x3000/0x3400 usa isto).

## 5. VDLP, CEL e ticks — o que realmente são FUN_00478c50 / FUN_00491a70 / FUN_004899d0 / FUN_0048d0c0
**Correção da parte 2:** essas funções são **construtores que registram campos nomeados no sistema de savestate/debug**, não o laço de execução. Elas definem a struct de cada bloco:
- **VDLP (`vdlp_3do`, 0x47a577; ctor FUN_00478c50, 6684 B)** — campos: CLUTDMA (bitfield "Modulo 3, EnDMA, VMode, Abs, PrevTick, CurrOver, PrevOver, Words 6, Lines 9"), CLUTB/CLUTG/CLUTR (256×8 bits), BACKGROUND, HEADVDL, MODULO, CURRENTVDL, CURRENTBMP, PREVIOUSBMP, **OUTCONTROLL** (bitfield: Back, FAMY, NTSC/PAL, S640, NoCLUT, CVBS/SlSt, FTr, BDet, SwapHVW, SW/SH/SB (2b), VI, HI, RND, REP, SwapHV, VSP/HSP/LSB (2b), NVI, NHI, CO, SLDis), LineDelay, xBack.
- **CEL (`CELEngine`; ctor FUN_0048d0c0 7072 B + FUN_004899d0 3954 B)** — State (Idle=0/Busy=16/Pause=48), DMA-WORDS, **CCBFLAGS** (SKIP, LAST, NPABS, SPABS, PPABS, LDSIZE, LDPRS, LDPIXC, LDPLUT, CCBPRE, YOXY, ACSC, ALSC, ACW, ACCW, TWD, LCE, ACE, MARIA, PXOR, USEAV, PACKED, POVER, PLUTPOS…), PRE0 (SKIPX, VCNT, UNCODED, REP8, BPP), PRE1 (WOFF, WOFFX, NOSWAP, UNCLSB, LRFORM, TLHPCNT), pproc_3do / pproc_3do_lite (xConstants/xMods = pós-processador de pixel, versão completa e "lite").
- **Temporização (FUN_00491a70, 9812 B)** — campos **VDLPTickAccum, AudioTickAccum, TimersTickAccum, DSPCalibration, TickCounter**: o Phoenix acumula "ticks" por subsistema (vídeo, áudio, timers, calibração do DSP) e os executa por quantum — é o relógio de sincronização entre ARM, VDLP, timers, DSP e CEL.
- **Kernel/HLE de tarefas (FUN_004a7f60 e arredores, strings KernelBase, IdleTask, UndefTask, ARMQuota, CEL2ARMClocks, Memory, FullSpace)**: o Phoenix **inspeciona o kernel do 3DO** (tarefas, memória) para rastrear carga/ociosidade e decidir quota de ARM (opções "ARM Scale", "Low Accuracy", "CEL Rate", "Post CCB", "Multy Task DSP/CEL").
- Opções de configuração vistas: Render, Video Mode, Texture cash size, FBO Count (renderização via OpenGL/FBO), dispositivos de entrada (Mouse, Lightgun, Flightstick Pro, Orb, Coin/Start/Service/Reload/Holster — modo arcade).

## 6. O que isto muda para o plano
- Diferenças CLIO da parte 3 continuam válidas; XBUS por vtable explica o "0x30 sem dispositivo".
- **Para o core novo**: o que mais separa o Phoenix do Opera em desempenho/compatibilidade = (a) relógio por ticks com acumuladores, (b) CEL engine/DSP em threads, (c) renderização GL com pós-processador (pproc). Nenhum deles é necessário para um core funcional; (a) é o candidato a testar primeiro (afeta timing/VBL).
- A lógica fina do CEL e do VDLP está nos métodos virtuais dessas classes (não nos construtores); localizar via vtables (ver pendências).

## Pendências / parte 5
1. Aplicar patch no Opera (diffs 1–10 da parte 3 + SPORT 0x77/retorno do valor anterior + XBUS sem dispositivo=0x30) e compilar.
2. Para o VDLP/CEL/ticks reais: localizar as vtables (ponteiros `PTR_FUN_00c240f0`, `DAT_00cc1a70`…) e decompilar os métodos; opcional (partes 7+).
3. Hashes MD5 em .rdata (a48e6746…, cf11bbb5…, 5df7f212…, dacffcf5…) = provável identificação de BIOS/fontes; verificar contra BIOS conhecidas se quiser.
