# Parte 19 - Ligacao do CEL ao MADAM (rodada 1)

Novo: `celexec.h/.c` (laco da cadeia de CCBs de FUN_00488700, modo async==0) ligado a `madam.cel_exec` em `machine.c`.

## Mapa de registradores (achado desta rodada)
CEL = madam+0x38. `dma_arr[0..3]` (MADAM 0x5a0..0x5ac) = CEL+0x10001f0/f4/f8/fc = **pc, NEXTCCB, PLUTDATA, PDATA**.
O inicio da cadeia e `dma_arr[1]` (poke 0x033005a4); o .exe grava de volta pc/next/plut/src ao terminar.
`cel_w[i]` = CEL+0x10000f0+4i: [4]=pixc, [8]=ppmp, [13]=recorte, [14]=base de leitura, [15]=base de escrita,
[16..23]=xpos,ypos,vdx,vdy,hdx,hdy,hddx,hddy, [0x20..0x3f]=PLUT. PRE0/PRE1 (CEL+0x1000214/218) ficam so no executor.

## Fluxo
poke 0x100 -> `cel_state=0x10`, `cel_exec(async=1)` (devolve 0 = sem espera) -> `ph_write32` ve stall 0 e chama `cel_run(0)` ->
`madam_cel_set(0)` -> `cel_exec(async=0)` percorre a cadeia: `cel_ccb_read` -> (empacotado: `cel_decode_packed` + compactacao) ->
`cel_pix_setup` + `cel_raster_setup` -> `cel_blit11`. Depois de cada CCB: `xpos += vdx*H`, `ypos += vdy*H`
(H = altura do ultimo CEL **desenhado**, CEL+0x1000274), como no fim do laco do .exe.

## Fora desta rodada (declarado)
- Custo em ciclos (FUN_00482950/004821b0): hoje stall = 0, o CEL desenha instantaneamente. Muda a temporizacao, nao a imagem.
- Caminhos 2/3 do rasterizador (FUN_0047ef90, 0047f9e0, 0047e3c0, 00481320): CELs com escala/rotacao/quad contam em `cel.unsupported` e nao desenham.
- Modo assincrono (FUN_00704200) e cache de texturas por intervalo de RAM (so otimizacao; aqui o empacotado e decodificado a cada CCB).
- Cena de teste: `test_celexec` (12 verificacoes): 16 bpp linear, cadeia de 2 CCBs com avanco vertical, empacotado (literal + repeticao). Valores vem da leitura do .exe, nao de execucao dele.

## Integracao sobre o projeto atual (correcao das rodadas 18/19)
As rodadas 18 e 19 tinham sido feitas sobre uma base antiga (vdlp/machine/Makefile menores, sem libretro). Aqui tudo foi refeito
partindo do 3do_phoenix.zip atual: **o vdlp.c/vdlp.h, libretro.c, test_libretro.c e PARTE18_VDLP_RODADA1/2.md do projeto foram mantidos**
(API `vdlp_init/vdlp_free/vdlp_frame_end/ph_set_pal`, ganchos blit/present, linhas sujas). So entrou o que era novo:
- `celexec.c/.h` + `test_celexec.c` (novos); `machine.h/.c`: membro `celexec_t cel`, `c_cel_exec` ligado a `madam.cel_exec`, `ph_free` chama `celexec_free`.
- `Makefile`: `cel.c celexec.c` entram em CORE (o libretro .so passa a incluir o CEL, com `-lm`), alvo `test_celexec` no `all` e no `asan`.
- `test_vdlp.c` corrigido: o `vdlp.c` ja contava as linhas da VDL a partir da linha 0 do CLIO (achado da Parte 18), mas o teste
  ainda assumia a contagem a partir da 1a linha visivel (11 falhas antigas). Agora a linha visivel y e a linha LEAD+y (0x10 NTSC / 0x11 PAL).
Verificado: `make` (clio, arm60, machine, madam, cel 109, vdlp, celexec 12, libretro 32) e `make asan` sem falhas; `make libretro` gera phoenix_libretro.so.
