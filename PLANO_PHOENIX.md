# Plano: compilar uma cópia do Phoenix (sem Opera)

Objetivo confirmado: ao final das fases, **compilar uma reimplementação do Phoenix** escrita a partir do que o
`PhoenixEmuProject.exe` faz, e não um patch do Opera/FreeDO. As referências a Opera/FreeDO nas partes 3, 5 e 6 ficam
**canceladas**; os achados de comportamento delas continuam válidos, só que agora vêm do .exe.

## Escopo honesto
- **Dá para copiar:** o núcleo de emulação (ARM60, MADAM, CLIO, VDLP, CEL, DSP, SPORT, XBUS/CD) e um front-end mínimo.
- **Não dá para copiar 1:1:** Qt, Crypto++, AngelScript, netplay e o renderizador OpenGL/pproc. Esses entram só como
  front-end simples (ou ficam fora) e o núcleo é testado por savestate (`madam.bin`/`clio.bin`) contra o .exe rodando no seu PC.
- Não consigo rodar o .exe nem compilar o front-end gráfico aqui (sem Windows, sem rede para bibliotecas): a compilação do
  núcleo em C com `gcc` funciona; o build final com janela/áudio você faz no seu PC (MinGW, como o original).

## Fases
| Fase | Conteúdo | Estado |
|---|---|---|
| 1 | CLIO: Poke/Peek, IRQ, timers, relógio (áudio/timers/linhas/VBL), DMA do XBUS | **feito em `phoenix_core/clio.c`**, compila e passa nos testes de consistência |
| 2 | MADAM: Poke/Peek, DMA, matrix engine; despachador de memória (FUN_0049fda0/fea0); SPORT | parcial (RE nas partes 2 e 4; falta código) |
| 3 | CPU ARM60: `FUN_00423870` (passo), `FUN_00422800` (bancos), ALU/shifter/LDM/STM | **feito em `phoenix_core/arm60.c`** (partes 9 e 10); falta só o laço de IRQ/FIQ `FUN_004a0920` e a ligação com o CLIO |
| 4 | VDLP (`FUN_00474410`/`FUN_004788b0`) e CEL engine (`FUN_0048b310`) | **VDLP feito em `vdlp.c` (Parte 18, a partir do formato público; falta A/B contra o .exe)**; CEL: só blit 1:1 (Parte 17) |
| 5 | DSP: passo por amostra `FUN_0046d570`, memórias, reset (`FUN_00427d90`) | precisa Ghidra/REA |
| 6 | Dispositivo de CD no XBUS ID 0 (`clio+0x50`), leitura da imagem do disco | precisa Ghidra/REA (vtable do CD) |
| 7 | Front-end mínimo + build + teste A/B por savestate contra o .exe | **front-end libretro feito (`libretro.c`, Parte 18)**; BIOS real mostra o logo 3DO; faltam CD, DSP, entrada e o A/B |

O que bloqueia as fases 3–6 é só o REA/Ghidra (rede). As fases 1 e 2 seguem sem ele.
