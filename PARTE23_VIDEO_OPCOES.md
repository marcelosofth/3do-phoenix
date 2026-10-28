# Parte 23 — Opções de vídeo (Configurações > Vídeo do Phoenix) no núcleo libretro

Arquivos: `libretro.c`, `test_libretro.c` (o resto do núcleo não mudou).

## Mapeamento
| Phoenix (.exe) | Opção libretro | Valores | Como foi feito |
|---|---|---|---|
| Keep screen aspect ratio | `phoenix_keep_aspect` | on / off | on: a imagem (com bordas) é anunciada em 4:3 (`aspect_ratio` da geometria). off: pixels quadrados (largura/altura do canvas). |
| Expand background | `phoenix_expand_bg` | off / on | Canvas alargado a 16:9 (426×240 NTSC, 512+ PAL), imagem centrada, laterais com a cor de fundo da VDL (`vdlp.bg`). |
| Show borders | `phoenix_borders` | off / on | Margem de 5% (16×12 NTSC, 19×14 PAL) com a cor de fundo da VDL. |
| Image filter: Disconnect / Linear / Shader: std | `phoenix_image_filter` | off / linear / shader_std | off = pixel repetido; linear = bilinear; shader_std = `shaders/std.frag` do Phoenix portado para C (mesmas contas do GLSL). |
| FBO Scale | `phoenix_fbo_scale` | 1x…4x | Escala interna da saída; é o que dá sentido aos filtros (a 1x o filtro não tem o que interpolar). |

Todas valem **na hora**: `retro_run` consulta `GET_VARIABLE_UPDATE` e manda `SET_GEOMETRY`. O teto da geometria é fixo (2272×1280 = canvas 16:9 máximo × 4).
Padrões = comportamento anterior (on / off / off / off / 1x): a saída por defeito é idêntica à da Parte 18.

## Cadeia por quadro
`g_src` (VDLP nativo, 0x00RRGGBB) → canvas (margens) → escala + filtro → formato do frontend (XRGB8888 ou RGB565) em `g_fb`.
No shader std, o que só depende da célula de origem (diferenças entre vizinhos e fator da diagonal) é calculado uma vez por célula.

## Não implementado (e por quê)
- **Vsync**: no libretro é do frontend (RetroArch > Vídeo > Sincronização vertical). O núcleo não controla.
- **Render (Software/Original)**, **Texture Filtering**, **Texture Scaling** (scaler AngelScript `scalers/std.as`): dependem de um render OpenGL que o núcleo não tem; na própria tela do Phoenix aparecem desativados com Render = Software.
- **Video Stream (Internal)**: sem referência no código/documentação do projeto.
- **"Do not keep track of the modification pixel processor"** (desativada na captura) e **"Drawing all CEL at the end of CCB-list"**: o comportamento exato no .exe não foi decompilado; implementar por palpite não é seguro. Precisam de uma rodada de REA na tela de opções (strings da UI → função que lê a flag).

## Testes
`test_libretro`: 37 → 55 verificações (escala 2x, linear 75/25, shader em área lisa/aresta/diagonal 4x, bordas, fundo expandido, aspecto, troca ao vivo com `SET_GEOMETRY`, PAL+RGB565+tudo ligado dentro do teto). ASan/UBSan limpos.
Compilar: `make clean && make libretro` (o `phoenix_libretro.dll` do zip é o antigo; precisa ser recompilado).
