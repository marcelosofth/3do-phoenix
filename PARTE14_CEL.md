# Parte 14 — Motor de CEL: leitor de CCBs, PLUT, decoder empacotado, conversão índice→cor (código novo: cel.c/h)

Método: Ghidra 12.1.3 headless sobre o `PhoenixEmuProject.exe`, **import sem análise automática** (a máquina desta sessão tem 3 GB de RAM,
a análise completa estourava); cada função foi criada por endereço e decompilada com `scripts/DecompNew.java`. Por isso as listas "callees"
dos arquivos `dec/` ficam vazias (os nomes `FUN_` dentro do código continuam corretos). Decompilações em `PhoenixRE_parte14.zip` (`dec/`).
Convenção: `CEL` = `param_1` de `FUN_00488700` = `madam+0x38` (Parte 13); offsets abaixo são relativos a ele.

## O que a Parte 13 deixou em aberto e o que ficou claro
`FUN_00488700(CEL, async)` é o **laço da cadeia de CCBs**. Para cada CCB: lê os campos (seção 1), monta registradores derivados, escolhe
o decoder pela flag PACKED, soma o custo em ciclos e (se `async==0`) desenha. Retorna `(ciclos/4) / CEL+0x1000074` (stall do ARM).

**Correção ao que se esperava:** `FUN_00487100` **não** é um decoder de pixels. É o caminho do CEL **não empacotado**: calcula geometria
(seção 3), procura numa lista de texturas já decodificadas (struct de 0x28 bytes por entrada, ponteiro em `CEL+0x78`..`0x20000f*8`) uma que cubra
o mesmo intervalo de RAM com os mesmos parâmetros (`FUN_0047cd70` monta a chave, `FUN_0047c330` compara) e, se não achar, cria a textura via
vtable (`+0x48`, callback `FUN_00489930`). O **decoder empacotado de verdade é `FUN_004859b0`** (seção 4), com o mesmo esquema de cache.
Ou seja: o Phoenix decodifica a imagem-fonte **uma vez** para uma textura de 32 bits e invalida por escrita na RAM (watchpoints do Parte 12).

## 1. Leitor de CCB (laço em `FUN_00488700`, rótulo `LAB_004887da`) — portado em `cel_ccb_read`
`cur = CEL+0x10001f4 & 0xfffffc`; para `cur >= 0x300000` termina a cadeia. Leituras fora de 3 MB devolvem 0.
| CCB | campo | destino (CEL+) | regra |
|---|---|---|---|
| +0x00 | flags | 0x1000210 | |
| +0x04 | next | 0x10001f4 | `NPABS` (0x20000000): absoluto; senão `(v+cur+8)&0xffffff`; `>=0x300000` → 0; `LAST` (0x40000000) → 0 |
| +0x08 | dados | 0x10001fc | `SPABS` (0x10000000): absoluto; senão `(v+cur+0xc)&0xffffff`; fora da RAM → 0 |
| +0x0c | PLUT | 0x10001f8 | só se `LDPLUT` (0x800000); `PPABS` (0x08000000) absoluto; senão `(v+cur+0x10)&0xffffff` |
| +0x10/14 | xpos, ypos | 0x1000130 / 0x1000134 | só se `YOXY` (0x200000) **e** não `SKIP` (bit 31); o cursor sempre pula para +0x18 |
| +0x18… | hdx,hdy,vdx,vdy | 0x1000140/144/138/13c | `LDSIZE` (0x4000000), nessa ordem |
| | hddx,hddy | 0x1000148/14c | `LDPRS` (0x2000000) |
| | ppmp | 0x1000110 | `LDPPMP` (0x1000000) |
| | PRE0 [, PRE1] | 0x1000214 / 0x1000218 | no CCB se `CCBPRE` (0x400000), senão no início dos dados (avança `src`); **PRE1 só existe se não `PACKED`** (0x200); em CEL empacotado fica o valor antigo |
Desenha só se `!SKIP && (flags & 0x60000)` (ACW|ACCW). Os nomes das flags seguem o cabeçalho 3DO público; o que está provado é a máscara usada em cada teste.
Depois da leitura o .exe copia tudo para registradores derivados (`+0x100027c` flags, `+0x1000280/284` PRE0/PRE1, `+0x100028c…2ac` deltas, e
**duplas** em `+0x2b8…2f0`: `hdx,hdy,vdx,vdy,hddx,hddy` × 2⁻²⁰ (0xb03118) e `ppmp`… × 2⁻¹⁶ (0xb030f8) para o rasterizador — Parte 15).

## 2. PLUT (`LDPLUT`, em `cel_ccb_read`)
Lê halfwords em `plutp` (RAM guarda palavras big-endian, meia-palavra no endereço `^2`). Quantidade: PRE0&7 `<3` → **8**, `==3` → 16, demais → 32 entradas
(o .exe carrega 8 mesmo para 1–2 bpp: sobra inofensiva). Guarda em `CEL+0x170` (32 palavras) e uma cópia truncada a 16 bits em `CEL+0x1000300` (usada pelo decoder).

## 3. Geometria (`FUN_00487100`; `cel_geometry`)
`bpp = {1,1,2,4,6,8,16,16}[PRE0&7]` (tabela 0xb030a0); altura = `((PRE0&0xffc0)>>6)+1`, **dobrada se `PRE1&0x800`**; largura = `(PRE1&0x7ff)+1`;
`WOFFSET = bpp<8 ? PRE1>>24 : (PRE1&0x3ff0000)>>16`; passo da linha = `WOFFSET*4+8` bytes.

## 4. Decoder empacotado (`FUN_004859b0`, leitor de bits `FUN_0059c720`) — portado em `cel_decode_packed`
Cada linha começa com um campo de `bpp<8 ? 8 : 16` bits (palavras extras); a linha tem `8+off*4` bytes. Pacotes: tipo (2 bits) + `contagem-1` (6 bits):
**0** fim de linha · **1** literal (`n` pixels de `bpp` bits) · **2** transparente (`n` pixels = `0x80000000`) · **3** repetição (1 pixel, `n` vezes).
Quirk preservado: o teste de fim de linha acontece **depois** de ler o tipo (e então consome 6 bits). Máx. 0x800 pixels por linha; o resto da linha vira transparente;
a largura da textura é a maior linha. Cada pixel passa por `FUN_0047ca50` (seção 5). O leitor de bits é MSB-first, 1..32 bits (0→1, >32→32), lendo bytes por vtable (`byte ^ 3` na RAM).

## 5. Índice → cor (`FUN_0047ca50`; `cel_index_to_color`)
Saída de 32 bits: bit 31 = transparente; baixos 15 bits = RGB555; bits 16+ carregam bits extras (caso 8 bpp).
| PRE0&7 | resultado |
|---|---|
| 0,1 | `PLUT[(v&1) + (flags&0xf)*2]` |
| 2 | `PLUT[(v&3) + (flags&0xe)*2]` |
| 3 | `PLUT[(v&0xf) + (flags&8)*2]` |
| 4 (6 bpp) | `((v>>5&1)<<15) + (PLUT[v&0x1f]&0x7fff)` |
| 5 (8 bpp) | não-linear (`PRE0&0x10==0`): `(t*8\|t<<6\|t)<<16 \| PLUT[v&0x1f]` com `t=(v>>5)&7`; linear: rearranjo de bits (transcrito, ver `cel.c`) `\| (flags&0x80)<<8` |
| 6,7 (16 bpp) | não-linear: `v&0x8000 \| (v&0x3fe0)<<11 \| PLUT[v&0x1f]&0x7fff`; linear: `v` |
Por fim, se `!(flags & BGND 0x20)` e `(r & 0x7fff)==0` → `r \|= 0x80000000`. As fórmulas do caso 8 bpp linear foram **transcritas** da decompilação, sem interpretação.

## 6. O que li, mas ainda não portei (e o que NÃO li)
- `FUN_0047d0d0(CEL, pixel, pixel2)` — **pipeline de pixel PPMP/PIXC** (aplica `+0x1000340/342/344`, cores por componente, shifts de `PPMP`, XOR/soma, metade). Li ~70% (cabeça e meio); falta o final e a ligação com `+0x10002f8/fc`. Fica para a Parte 15.
- `FUN_0047cd70` monta a chave de cache (bpp, PLUT, PPMP) — leitura parcial; `FUN_0047ccf0` **não** decompilado.
- `FUN_00482950` + `FUN_004821b0` — **custo em ciclos**: lê pre/ppmp para saber se o CEL usa blend/leitura de destino; mede a aresta da CEL com `FUN_004821b0` (duas vezes), soma `2×` (ou `4×` se há blend) e acrescenta uma penalidade de 2×`(|a|+|b|−custo)` quando o modo está ligado e não há `LDPLUT`/flags `0x180000`. Fórmula **lida mas não validada**, não portada.
- `FUN_00480290` (rasterizador) e `FUN_004860d0`/`FUN_00704200`/`FUN_00703e20` (desenho a partir da textura / fila assíncrona): **não lidos** (só aparecem nos pontos de chamada). `FUN_00943830/…e90/…8300` são helpers de lista/contador (copy-on-write de Qt) — ignorados.

## Código e testes
`cel.h/.c` + `test_cel.c` (23 verificações, `make` e `make asan` limpos): cadeia de dois CCBs (relativo/absoluto, LAST, SKIP), PLUT de 16 entradas, geometria, conversão de cor e um CEL empacotado montado à mão (literal, repetição, transparente, EOL, 2 linhas).
**Os valores esperados vêm da leitura da decompilação, não de uma execução do .exe** (o mesmo limite da Parte 13). Ainda não está ligado a `madam_t.cel_exec`.

## Aberto
1. Parte 15: rasterizador `FUN_00480290`, `FUN_00704200`/`FUN_004860d0` e o final do pipeline `FUN_0047d0d0`.
2. Validar o custo em ciclos contra uma execução real; ligar `cel_ccb_read` ao gancho `cel_exec`.
3. Os campos `+0x10002f8` e `+0x10002fc` (modo de fonte do pixel / bit de `PRE0`/`PPMP`) ainda não têm nome.
