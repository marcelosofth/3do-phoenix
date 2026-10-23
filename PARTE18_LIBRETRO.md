# Parte 18 — Front-end libretro + VDLP (vídeo)

Entrega: `libretro.h`, `libretro.c`, `vdlp.h`, `vdlp.c`, `test_vdlp.c`, `test_libretro.c`, `Makefile` (atualizado) e `boot_3do.png`.

## O que foi feito
- **`libretro.h`**: cópia do cabeçalho oficial (libretro-common, licença MIT), sem alterações.
- **`libretro.c`**: `retro_init`, `retro_deinit`, `retro_load_game` (aceita também *sem jogo*), `retro_unload_game`, `retro_run`, `retro_reset`, `retro_get_system_info`, `retro_get_system_av_info`, callbacks de vídeo/áudio/entrada, NVRAM como `SAVE_RAM` (32 KB, preservada no reset), região, e opções (`phoenix_region` NTSC/PAL, `phoenix_pixel_format` xrgb8888/rgb565, `phoenix_bios`). Savestate e cheats: declarados como não suportados.
- **`vdlp.c`**: lê a VDL na RAM, trata CLUT (5 bits por canal, com `rgb_enable`), fundo, bypass da CLUT, `COLORSONLY`, entrada seguinte relativa/absoluta, persistência em linhas, módulo de linha (320/384/512/640/1024) e o avanço do framebuffer (pares esquerda/direita de 16 bits). Converte para **XRGB8888** ou **RGB565** num buffer 320×240 (NTSC) / 320×288 (PAL).
- **Ligações** (nenhum arquivo do núcleo foi alterado): `madam.dma580_write` (poke MADAM `0x580` = cabeça da VDL; era o gancho que a Parte 13 deixou sem dono), `clio.vdlp_line_pending/vdlp_line`, `clio.audio_out`, e `madam.regs[2]` (MCTL: CLUTXEN `0x2000`, VSCTXEN `0x4000`).
- **Laço de quadro**: `ph_run` em fatias de 1024 ciclos até o VDLP fechar o quadro visível (linha 21+240 NTSC / 21+288 PAL), com teto de 2,2 quadros de ciclos. FPS: NTSC 12,5 MHz/(263×792) ≈ 60,01; PAL ≈ 50,0; áudio 44 100 Hz (ambos derivados das constantes do `clio.c`).

## Resultado com a BIOS real (`make bios-test N=600`)
`fz10`, `fz1`, `goldstar` e `sanyotry` fazem boot, programam MADAM/VDL (`MCTL=0x16000`, cabeça da VDL em `0x2b0000`/`0x2f1000`) e o VDLP desenha o **logo de boot do 3DO** (`boot_3do.png`).

## Achado (bug de inicialização, corrigido no front-end)
A BIOS lê `CSTAT` (CLIO `0x28`) no início e testa `(CSTAT & 0x43) == 1` (bit PON = power-on) para o boot a frio. Com `CSTAT = 0` ela caía num laço de erro em `0x030000a8` e nunca tocava o MADAM. `machine_boot()` agora põe `CLIO.R[0x28] = 1`. O valor real do reset no Phoenix (`FUN_00490890`/ctor do CLIO) **não foi confirmado no .exe**; o bit PON vem da documentação pública/Opera. Se quiser, o lugar certo é `clio_reset()`.

## Testes
`make` roda 7 suítes; novas: `test_vdlp` (29 verificações: CLUT, bypass, rgb_enable, COLORSONLY, próxima entrada relativa, persistência, DMA desligado, RGB565, módulo 640, PAL) e `test_libretro` (31: info/av_info, pokes no MADAM chegando ao VDLP pelo barramento, pixels, ~735 amostras/quadro, ~208 k ciclos/quadro, NVRAM, reset, PAL/RGB565, fallback de formato, sem BIOS, disco inexistente). `make asan` limpo (ASan/UBSan). `make libretro` gera `phoenix_libretro.so` (`.dll` no MinGW) com os 25 símbolos `retro_*`.

## Limites honestos
1. **O VDLP não veio da decompilação do .exe** (`FUN_00478c50`/`00474410`/`004788b0` ainda não foram decompilados). Os campos da VDL foram conferidos contra a documentação pública e o opera-libretro; o código é novo. Duas coisas são suposição minha: a CLUT inicial (rampa linear) e as linhas de início/fim (5, 21, +240/+288, como o opera-libretro). O Phoenix pode diferir; só um A/B contra o .exe confirma.
2. **Sem disco ainda**: o CD no XBUS (Fase 6) não existe. `retro_load_game` valida e guarda o caminho da imagem (`.iso/.bin/.cue`), mas a BIOS não verá o disco. Por isso a tela fica no logo.
3. **Áudio**: o DSP (Fase 5) não existe, então só sai silêncio (o formato L=16 bits altos / R=baixos é suposição).
4. **Entrada**: não há joybus no CLIO; `retro_run` só faz o *poll*.
5. **CEL geral**: só o blit 1:1 existe (Parte 17) e ainda não está ligado ao `madam.cel_exec`; jogos que desenham com escala/rotação ficarão sem sprites.
6. **Alta resolução (640/1024)**: amostrada para 320 colunas, sem os 4 campos intercalados.
7. ~~Vetor de reset~~: **fechado nesta rodada** (ver `machine.c`/REA) — `FUN_004a7f60` grava `PC = 0x03000000` de verdade no reset; a função já veio decompilada em `PhoenixRE_parte4.zip`/`dec/dec_004a7f60.c`, só não tinha sido cruzada com o `machine.c` ainda. O ganho agora fica em `ph_init()` (núcleo), não mais em `libretro.c` (front-end).

## Como usar
`make libretro` → `phoenix_libretro.so`. No RetroArch, coloque `fz10.bin` em `<system>/`, `<system>/BIOS/`, `<system>/3do/` ou `<system>/3do_phoenix/BIOS/` e abra o núcleo (sem jogo) para ver o boot.
