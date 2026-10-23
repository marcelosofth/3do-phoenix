# Phoenix 3DO — RE, parte 1 (handoff para o próximo agente)

## Objetivo do usuário (Marcelo, pt-BR; "lengo": quer tudo pronto, ele fornece arquivos se faltar)
Usar PhoenixEmuProject.exe + REA (github.com/morluto/rea) para engenharia reversa e reproduzir o funcionamento
do núcleo 3DO (baseado em 3doplay/FreeDO, que é aberto). Meta de longo prazo: achar CLIO real (_clio_Poke/Peek),
MADAM, VDLP, DSP e comparar com o FreeDO. Entregar em partes. Escopo realista: documentar comportamento e reimplementar
sobre o FreeDO; NÃO é viável nem pretendido "copiar" o .exe inteiro (Qt UI, Crypto++, AngelScript etc.).

## Ambiente (o que funcionou / não funcionou)
- REA NÃO roda no sandbox: `npm install`/`npm ci` falham (registry bloqueado: ENOTFOUND/403), então não há dist/ do REA.
  Backend do REA = Ghidra, então usei Ghidra 12.1.3 headless direto (baixado de github.com releases:
  ghidra_12.1.3_PUBLIC_20260817.zip, MD5 48825d8798199d8416b1cbcd8efd7d85; JDK 21 já instalado).
- IMPORTANTE: a análise padrão estoura heap (OOM) no "x86 Constant Reference Analyzer". Solução: preScript
  scripts/DisableHeavy.java (desliga esse + Stack + Decompiler Parameter ID + Non-Returning Discovered) e
  `GHIDRA_HEADLESS_MAXMEM=6G`. Com isso a análise completa terminou (~6 min, 2 CPUs).
- Comando:
  GHIDRA_HEADLESS_MAXMEM=6G analyzeHeadless <proj> phoenix -import PhoenixEmuProject.exe -overwrite -scriptPath scripts -preScript DisableHeavy.java
  Depois: `-process PhoenixEmuProject.exe -noanalysis -readOnly -postScript <Script>.java <outdir> [args]`
- O projeto Ghidra fica só no sandbox (efêmero); refazer o import se a sessão for nova.
- Sem acesso à rede além de GitHub (git clone/download de releases funciona; API/gh CLI não).

## Binário
PE32+ x64 GUI, MinGW/GCC (SjLj exceptions, libstdc++-6), ~9 MB. .text 0x401000–0xaf05ff, .rdata 0xaf3000,
.bss 0xcf4000, seção "CryptoPP". 12.851 funções (sem símbolos; nomes FUN_). Qt (QGLWidget), OpenGL, OpenAL, WS2_32 (netplay).
Fontes originais citadas: ../PhoenixProject/{mainwindow,qfrontend,qfrontend_opengl,qfrontend_record,qf_config_input,
dumplibrary,dumpadapter,dumpadapter_3do_cd,savestation,phoenix_network}.cpp; libs: Crypto++, AngelScript, tinyxml.
Config: /phoenix.config.xml. Arquivos de BIOS/estado: madam.bin, clio.bin, nvram.bin, "memory_*".
dumpAdapter3DOcd::makeDSPDasm gera código "dsp_3do_instruments*.cpp/.h" (DSP por padrões/otimizadores — Phoenix tem
DSP otimizado por reconhecimento de instruções).

## Achados-chave
1. Máquina inteira = UMA struct enorme (param_1 = `this`). Layout visto em FUN_004a20a0 (save/load de estado,
   nomes via vtable em _DAT_00cf4050+0xf0 (savestation)):
   - MADAM regs : this+0x1000578, 0x800 bytes ("madam.bin")
   - CLIO regs  : this+0x1087454, 0x10000 bytes ("clio.bin")  (reg = base + offset do registrador)
   - RAM        : this+0x129755c, 0x300000 ("memory_<n>") ; NVRAM: this+0x159755c, 0x8000
   - ponteiro p/ sub-objeto MADAM em this+0x1097548 (usado como +0x10000ec == 0x10 etc.)
   - estado ARM60 no início da struct (this+0x34..0x1c8; CPSR em +0x1c8, modos 0x10 USER/0x11 FIQ).
2. FUN_004a0920 (697 B) = laço principal de execução da CPU: roda FUN_00423870 (step ARM), checa IRQ do CLIO
   lendo CLIO+0x40/0x48 (IRQ0 pend/enable: this+0x1087494/0x108749c) e CLIO+0x60/0x68 (IRQ1: +0x10874b4/0x10874bc),
   dispara FIQ/IRQ (FUN_00422800(this+0x48, 0x11)). FUN_0049fea0, 004a0030, 004a0090, 004a0130, 004a19a0 testam CLIO+0x84.
3. Hits de acesso a MADAM concentram-se em FUN_0048adb0 (1243 B, 66 hits) e FUN_0048b310 (7416 B, 26 hits) → candidatas
   fortes a MADAM Poke/Peek e CEL engine. FUN_0048d0c0 (CELEngine, string "CELEngine"), FUN_004899d0 (DMA-WORDS).
4. NÃO achei função "_clio_Poke" por deslocamento absoluto: só 14 hits de CLIO. O CLIO Poke/Peek provavelmente usa
   ponteiro base+registrador em runtime, ou é dirigido por tabela (a Phoenix usa descritores de bitfield em string,
   ex. "32:N 1@f,Z 1@f,..." e savestation com nomes). PRÓXIMO PASSO principal: achar quem escreve em CLIO via
   (this+0x1087454)+reg computado — procurar `LEA reg,[this+0x1087454]`/ADD com 0x1087454 (hit em 004a2210 é só save-state)
   e funções com switch grande sobre (addr & 0x3ff) / jump tables em torno de 0x0340xxxx (endereços CLIO 0x03400000–0x034FFFFF).
5. Subsistemas por strings (xrefs): vdlp_3do 0x47a577 (FUN_00478c50 = VDL/CLUTDMA/VDLPTick; chamada por FUN_00491a70),
   VDLPTickAccum/TimersTickAccum/DSPCalibration → FUN_00491a70 (9812 B, laço de ticks/temporização),
   CELEngine/CEL2ARMClocks/MADAM string 0x4a814e → FUN_004a7f60 (5808 B, init/loop da máquina; chamado de 0x404a50),
   XBUS/CD-Drive 0x4a1600, CommandFIFO/StatusFIFO/DataFIFO ~0x4acb09–0x4af0d0 (CD/XBUS), pproc_3do 0x48a81e, DSP 0x4647d2.
6. Lista de entradas em 0x00af....: tabelas de strings de erros CD (MEI_CDROM_*), erros DSPP, GRAFERR_*.

## Arquivos neste pacote
functions.tsv (addr,size,name,callers,callees), strings.tsv (addr,string,xrefs), imports.tsv, blocks.tsv,
disp_hits.tsv (acessos a offsets da struct: CLIO/MADAM/RAM/NVRAM por função), dec/*.c (decompilação de 12 funções-chave),
scripts/ (ExportInfo, DecompAt <outdir> <addr...>, ScanDisp, DisableHeavy).

## Próximas partes sugeridas
P2: decompilar FUN_0048adb0/0048b310 (MADAM) e mapear registradores; P3: caçar CLIO Poke/Peek (item 4); P4: VDLP
(FUN_00478c50/00491a70) vs FreeDO vdlp.c/clio.c; P5: DSP (FUN_004647d2 região) e pproc; P6: plano de port para FreeDO.
Persistir resultados em phoenix_clio_poke_peek.md (relatório acumulado que o usuário já mantém; ele prefere .md, sem scripts Ghidra
do lado dele — ele só tem VS Code).
