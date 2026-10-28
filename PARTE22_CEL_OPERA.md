# Parte 22 — Motor de CEL do Opera no phoenix_libretro

## Decisão
Validar o CEL jogo a jogo contra o phoenix.exe não converge. O alvo passa a ser o hardware 3DO (o que o Opera já aproxima, validado contra a biblioteca inteira).
**O motor de CEL do phoenix_libretro agora é o do Opera.** ARM, CLIO, MADAM (registradores, matrizes, DMA8), SPORT, VDLP, CD e DSP continuam os mesmos.

## O que mudou
| Arquivo | Mudança |
|---|---|
| `cel_opera.c` / `cel_opera.h` | **Novos.** Motor do Opera: leitor de CCB, PLUT, PDEC, PPROC/PPROJ (PPMP/PIXC), empacotado/literal/LRFORM e os rasterizadores Line/Scale/Arbitrary (perspectiva). |
| `celexec.c` / `celexec.h` | `celexec_run` chama `opera_cel_run` por padrão. Campo `legacy` (1 = motor antigo `cel.c`). Voltaram os contadores `cels_total`, `inv_*`, `ninv`, `inv[]` que o `libretro.c` já usava (o zip não compilava sem eles). |
| `libretro.c` | Opção `phoenix_cel_engine` = `opera` (padrão) \| `legacy` (reiniciar). Loga o motor escolhido no `retro_load_game`. |
| `machine.c` | `ph_state_load` preserva a escolha do motor. |
| `Makefile` | `cel_opera.c/.h` no CORE/HDRS; `-fwrapv`; `test_celexec legacy` entra no `make`. |
| `test_celexec.c` | Roda nos dois motores (`./test_celexec` = Opera, `./test_celexec legacy`). |

`cel.c` (motor antigo) **não foi apagado**: serve de A/B e os testes `test_cel`/`test_cel_unpacked` seguem exercitando-o.

## Como o `cel_opera.c` é feito
Gerado por `tools_cel_opera/gen_cel_opera.py` a partir do `opera_madam.c` do opera-libretro: o miolo é **copiado por intervalo de linhas, sem edição manual**. Só entram 4 retoques mecânicos (todos no gerador, comentados):
1. gancho de estatística antes de cada desenho (só contadores);
2. `CCB_LAST` zera `NEXTCCB` (igual ao `FUN_00488700` do .exe);
3. trava anti-laço: no máximo 65 536 CCBs por cadeia (cadeia que aponta para si mesma travaria o front-end);
4. `TexelDrawScale`: sai se o passo anda contra o destino (só acontece com dados lixo; com dados válidos o sentido sempre coincide).

Prólogo (`prologue.c`): `opera_mem_*` sobre a RAM do Phoenix **com checagem de limites** (fora dos 3 MB lê 0 / ignora escrita; o Opera original não confere), flags, leitor de bits.
Epílogo (`epilogue.c`): cola com o `madam_t` do Phoenix.

Regenerar: `python3 -I tools_cel_opera/gen_cel_opera.py <opera>/libopera/opera_madam.c <opera>/libopera/opera_bitop.c cel_opera.c tools_cel_opera/prologue.c tools_cel_opera/epilogue.c`

## Cola com o MADAM do Phoenix
Layout de RAM idêntico (palavra de 32 bits, byte = `addr^3`, meia-palavra = `addr^2`; 3 MB = DRAM 2 MB + VRAM 1 MB), então não há conversão de memória.
`cel_w[i]` = registrador `0x100+4*i`; `dma_arr[0..3]` = CURRENTCCB/NEXTCCB/PLUTDATA/PDATA.

| Phoenix | Opera |
|---|---|
| `cel_w[4]` (0x110) | `CECONTROL` |
| `cel_w[12..15]` | `REGCTL0..3` (strides, recorte, base de leitura, base de escrita) |
| `cel_w[8]` | `PIXC` do CCB (LDPPMP; "ppmp" no Phoenix) |
| `cel_w[16..23]` | x, y, vdx, vdy, hdx, hdy, hddx, hddy (formato do .exe) |
| `cel_w[0x20..0x3f]` | `PLUT[32]` |

Estado que o Opera guarda em variáveis (HDX.., PIXC, PRE1) vive em `celexec_t.op` → entra no savestate e não vaza entre máquinas. Se a CPU gravar direto num desses registradores, o registrador vence (igual ao .exe, que lê o estado dos registradores a cada cadeia).

## Validação feita
- `make` (12 suítes) e `make asan` limpos, nos dois motores onde aplicável.
- **BIOS fz1.bin, 300 M ciclos, 1 219 CELs: VRAM e DRAM bit a bit idênticas** entre Opera e legado (`tools_cel_opera/boot_cmp.c`) — prova que a cola (PLUT, registradores, persistência) está certa.
- Diferencial com CCBs sintéticos (`diff_engines.c`): **idênticos** em 1:1, escala 2x/0,5x/3x1,5, espelho H/V e rotação 90°. **Diferem** em rotação arbitrária e perspectiva (hddx/hddy): o Opera preenche cerca de 6% mais pixels de borda — exatamente a categoria do chão/estrada do Road Rash.
- Fuzz sob ASan/UBSan (`fuzz_cel.c`, ~12 000 cadeias aleatórias, 6 sementes): nenhum acesso fora da RAM, nenhum erro de UB.

## Limites honestos
1. **Não rodei jogo real** (sem CHD no ambiente): a melhora no Road Rash/FIFA precisa ser confirmada no seu RetroArch. Se piorar algo, `phoenix_cel_engine = legacy` volta ao motor antigo sem recompilar.
2. O Opera não classifica "CEL sem pixel": `CELINV` só mostra `cels=`; `inv_*` ficam em 0 (o `CELFMT`/`hist`/`grp` do motor antigo também não são preenchidos).
3. Custo em ciclos (stall do ARM) continua 0, como antes.
4. O fill do Opera é genérico (sem o atalho do blit 1:1 do legado); em Batocera de baixa potência vale medir o fps.
5. Licença do trecho do Opera: "modified GNU LGPL" do FreeDO — uso não comercial, com créditos (Troosh, Grishin, Wright, Sammons, Lazarev + mantenedores do opera-libretro). Os créditos estão no cabeçalho do `cel_opera.c`.

## Como testar no RetroArch
1. Compile (`make libretro`) e rode Road Rash / FIFA normalmente (motor Opera é o padrão).
2. Se algo piorar: Opções do núcleo → *Motor de CEL* = `legacy` → reiniciar o jogo. Compare.
3. Mande o log: a linha `motor de CEL: ...` aparece no início.
