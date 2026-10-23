# Phoenix 3DO — RE, parte 7: o relógio real (FUN_00490890) — timers, linhas de vídeo, VBL, áudio

Escopo: **só o `PhoenixEmuProject.exe`**, sem Opera/FreeDO (conforme sua instrução). Sem comparação com outros emuladores.
Convenções: `clio = this+0x1000dc8`; `R = clio+0x8668c` (registrador 0xNNN do CLIO = `R+0xNNN`).

## 0. Método e limite do ambiente

- O sandbox continua **sem rede**: não há Ghidra nem REA nesta sessão (ambos precisam ser baixados). Usei:
  `objdump -d` sobre o PE (1,79 M linhas de `.text`), um leitor mínimo de PE em Python para ler vtables em `.rdata`, e as
  decompilações do Ghidra que você já tinha em `PhoenixRE_parte4.zip` (`dec/`).
- Confirmei que o `.exe` enviado agora é idêntico ao do zip (mesmo MD5 `c99682f8…`).
- Consequência: tudo abaixo vem de código já decompilado + desmontagem; onde é hipótese eu marco **[hipótese]**.
  Para decompilar funções novas (ex.: o dispositivo de CD, a DSP) preciso do Ghidra/REA de volta.

## 1. Correção importante às partes 1 e 4

`FUN_00491a70` (9812 B) **não é um módulo de temporização**: é o **construtor do CLIO** (registra campos nomeados;
grava a vtable `PTR_FUN_00c240f0` e zera os acumuladores em `clio+0x9668c/90/94`, vistos em 0x493227–0x49323b).
Os "acumuladores de ticks" (VDLPTickAccum, AudioTickAccum, TimersTickAccum, TickCounter) são **campos do CLIO**.
O relógio de fato é o método **`FUN_00490890(clio, ciclos)`** (2051 B), chamado de `FUN_004a0920` (laço da CPU).

Como é chamado (de `dec_004a0920.c`, linha 73):

```
FUN_00490890(this+0x1000dc8, (int)(this[+0x40] * n) >> 4)     // n = ciclos ARM da fatia executada
```
`this+0x40` é o fator de escala (provavelmente a opção "ARM Scale"). Chamo o argumento de `c` = **ciclos ARM escalados**.

## 2. Os três relógios dentro de FUN_00490890

Todos seguem o mesmo padrão: acumulador += c × K; quando passa de um limiar, subtrai o período e dispara o evento.

| Campo | Peso por ciclo | Período | Evento |
|---|---|---|---|
| `clio+0x96690` | ×9 | **0x9f7 (2551)** | 1 amostra de áudio: DSP roda um passo (`FUN_0046d570(clio+0x4a8)`) e a amostra de 4 bytes vai para um buffer sob lock |
| `clio+0x9668c` | ×2 | **0x40 + R[0x220]** | 1 tick dos timers (`FUN_0048f540`) |
| `clio+0x96694` | ×7 | **0x15a8 NTSC / 0x15d7 PAL** | 1 linha de vídeo |

### 2.1 Áudio
`9 / 2551` por ciclo: se o ciclo nominal é 12,5 MHz, dá 12 500 000 × 9 / 2551 ≈ **44 099 Hz**, ou seja, 44,1 kHz.
Isso **confirma o clock ARM nominal de 12,5 MHz** e que o DSP é avançado **uma amostra por vez, dentro do laço do ARM**
(com `while`, então recupera atraso se `c` for grande). Há lock (`vtable+0xd0/+0xd8` de `_DAT_00cf4050`, mutex em
`clio+0x86670`) em volta, e um ramo alternativo (`clio+0x86680 != 0`) que drena um anel de pares em `clio+0x96708..0x96718`
para um callback — é o modo "DSP em thread" (as opções "Multy Task DSP").

### 2.2 Timers (FUN_0048f540, decompilada na parte 2)
- Período em `c`×2: `0x40 + R[0x220]` (R[0x220] = delay 10 bits, o mesmo registrador da seção 3 da parte 3).
  Logo um tick a cada `(0x40 + delay) / 2` ciclos escalados.
- 16 timers: contador de 16 bits em `R[0x100 + 8i]`, recarga em `R[0x104 + 8i]`.
- Controle em nibbles: timers 0–7 em `R[0x200]`, 8–15 em `R[0x208]`; por timer: **bit0 = habilita, bit1 = recarrega,
  bit2 = cascata** (só decrementa se o anterior estourou; o 1º da cadeia sempre pode decrementar).
- Ao estourar (contador estava em 0): se o índice é **ímpar**, levanta IRQ0 com a máscara `1 << (10 − i/2)`
  (timer 1 → bit 10, 3 → bit 9, … 15 → bit 3). Sem bit de recarga, o enable é limpo; com recarga, o contador volta ao valor
  de `R[0x104+8i]`.

### 2.3 Linhas de vídeo e VBL
A cada linha, sobre `R[0x34]` (= VCNT; bit 0x800 = campo, bits 0–10 = linha):

- **NTSC:** se `(R[0x34] & 0x7ff) < 0x106` → `R[0x34]++`; senão `R[0x34] = ~R[0x34] & 0x800` (zera a linha e **alterna o
  campo**). Resultado: 263 linhas (0..262) por campo.
- **PAL:** se `(R[0x34] & 0x7ff) > 0x137` → mesma reinicialização; senão `++`. Resultado: 313 linhas por campo.
- **Detalhe estranho:** o laço retorna se `acumulador < 0x15d8` em **ambos** os modos, mas em NTSC subtrai apenas `0x15a8`.
  Ou seja, em NTSC a linha dispara 0x30 unidades "atrasada" e o resto é carregado: a média continua sendo 0x15a8, mas há
  um jitter fixo. Mantenha isso se for reproduzir o comportamento exatamente.
- Depois de avançar a linha:
  1. `FUN_00474410(clio+0x122d0)` decide se esta linha é processada pelo VDLP; se sim, com lock `clio+0x86674`,
     chama `FUN_004788b0(clio+0x122d0, 0)`. `clio+0x122d0` é o objeto do VDLP (`vdlp_3do`) → `FUN_004788b0` é a
     **rotina de linha do VDLP**.
  2. Se a linha é **0x18 (24)** e existe callback em `clio+0x966e8/0x966f0/0x966f8` → chama (gancho de "início da área
     visível"/quadro; **[hipótese]** é onde o frame é entregue ao renderizador).
  3. **`R[0x3c] = R[0x3c] × 0x41c64e6d + 0x3039`** a cada linha: o registrador RandSample é um **LCG de 32 bits avançado por linha**
     (o Poke de 0x3c é ignorado e o Peek devolve o estado cru, pois só 0x30/0x17f0 têm tratamento próprio no Peek).
  4. Se `linha == (R[0x08] & 0x7ff)` → `FUN_0048ef80(clio, 1, 0)` = **IRQ0 bit 1 (VINT0)**.
     Se `linha == (R[0x0c] & 0x7ff)` → `FUN_0048ef80(clio, 2, 0)` = **IRQ0 bit 2 (VINT1)**.
     A comparação é só pela linha (o campo é ignorado).

`FUN_0048ef80(clio, irq0, irq1)` (decompilada na parte 4): com lock opcional, `R[0x40] |= irq0; R[0x60] |= irq1;`
e se `R[0x60] != 0` liga o bit 31 de `R[0x40]`.

Valores derivados (assumindo 12,5 MHz; **conta minha, não vem do código**): ≈15 783 Hz de linha em NTSC e ≈15 651 Hz em PAL
(a média real usa 0x15a8 e 0x15d7 sobre 7 unidades/ciclo; cada linha ≈ 792 ciclos em NTSC e ≈ 799 em PAL).

## 3. O bloco de "setor de CD" no início da função **[hipótese]**

As linhas iniciais (`clio+0x270..0x34c`) acumulam ciclos num contador `+0x270` limitado a `(+0x34c << 4)`; quando há
trabalho pendente (`+0x284 != 0`) e o anel (`+0x33c/+0x340/+0x338`) está vazio, ele chama métodos do objeto global
`_DAT_00cf4050` (vtable +0x130/+0xe0/+0x138) para **ler 0x800 bytes (2048) para `clio+0x78`**, incrementa `+0x280`,
sinaliza `FUN_004aa150(clio+0x50)` e mexe nos flags `+0x277` (0x10/0x20). 2048 bytes é o tamanho de setor de CD-ROM
dados, portanto isto é provavelmente a **entrega temporizada de setores do dispositivo de CD (XBUS)**. Não confirmei:
falta decompilar o objeto em `_DAT_00cf4050` e o dispositivo XBUS ID 0.

## 4. Sobre a vtable `0xc240f0` (CLIO)

Li 40 slots em `.rdata`. Os slots +0x48..+0x78 (`0x41ef80`, `0x41efe0`, `0x41efa0`, `0x41efc0`, `0x41f000`, `0x41f020`,
`0x41f040`) são **encaminhadores**: `mov rcx,[rcx+0x10]; test rcx,rcx; je ret; jmp [rcx->vtable + mesmo_offset]`.
Ou seja, o `vtable+0x78` que o Poke chama (ex.: ao escrever 0x220 e no SPORT) **repassa a chamada a um objeto-alvo guardado
em `this+0x10`** (um observador/"sinal" ligado à UI/renderizador). Slots +0x18 e +0x30 (`0x48f9e0`, `0x48ef50`) são
**resets**: ambos zeram os três acumuladores (`FUN_0048f950` escreve 0 em `+0x9668c/90/94`).
`vtable+0x40` (chamado no Poke de 0x300 com arg 3) aponta para `0x951760`, ainda não analisado.

## 5. Modelo executável (pseudocódigo fiel ao que foi lido)

```c
void clio_tick(clio_t *c, int cyc /* ciclos escalados */) {
    cd_sector_timing(c, cyc);                          // seção 3 [hipótese]
    FUN_0048f690(c);                                   // FIFOs/DMA, ainda não detalhado

    c->audio_acc += cyc * 9;
    while (c->audio_acc > 0x9f6) { c->audio_acc -= 0x9f7; dsp_step_and_emit_sample(c); }

    c->timer_acc += cyc * 2;
    while (c->timer_acc >= 0x40 + R[0x220]) { c->timer_acc -= 0x40 + R[0x220]; timers_tick(c); }

    c->line_acc += cyc * 7;
    while (c->line_acc >= 0x15d8) {
        if (!pal) { c->line_acc -= 0x15a8; step = ((R[0x34] & 0x7ff) < 0x106); }
        else      { c->line_acc -= 0x15d7; step = !((R[0x34] & 0x7ff) > 0x137); }
        if (step) R[0x34]++; else R[0x34] = ~R[0x34] & 0x800;
        if (vdlp_line_pending(c)) vdlp_line(c);
        line = R[0x34] & 0x7ff;
        if (line == 0x18 && c->frame_cb) c->frame_cb();
        R[0x3c] = R[0x3c] * 0x41c64e6d + 0x3039;
        if (line == (R[0x08] & 0x7ff)) raise_irq(c, 1, 0);
        if (line == (R[0x0c] & 0x7ff)) raise_irq(c, 2, 0);
    }
}
```
(Ordem das linhas 1–4 e dos `while` conferida contra a decompilação de `dec_00490890.c`.)

## 6. O que isso muda no plano

- A "Fase 2" da parte 6 (relógio por ticks) agora tem resposta **só a partir do .exe**: três acumuladores com razões
  9/2551, 2/(0x40+delay), 7/(0x15a8|0x15d7) sobre ciclos ARM escalados; o VDLP é executado **dentro do laço de linhas**, e as
  VINTs saem do mesmo laço.
- O 0x30 (HCNT) de Peek/Poke usa o **mesmo** `clio+0x96694`, então HCNT é uma projeção da fase da linha (já descrito na parte 3).
- Próximas partes sugeridas, todas sobre o .exe (precisam do Ghidra/REA de volta, ver seção 7):
  - **P8:** dispositivo XBUS ID 0 / CD (seção 3) e `_DAT_00cf4050`; `FUN_0048f690` (FIFOs/DMA por tick).
  - **P9:** `FUN_0046d570` (passo do DSP por amostra) e `FUN_00478c50` → métodos reais do VDLP (`FUN_00474410`, `FUN_004788b0`).
  - **P10:** MADAM/CEL (`FUN_0048b2a0`, `FUN_0048b310`) e consolidação em `phoenix_clio_poke_peek.md`.

## 7. O que preciso de você

Para continuar com o **REA/Ghidra** (decompilar funções novas, que é o que P8–P10 exigem), o sandbox precisa de rede para
`github.com` (Ghidra e REA) e `registry.npmjs.org` (REA); hoje ambos falham por resolução de DNS. Isso é configuração sua
(permitir esses domínios nas configurações do Claude). Enquanto isso só consigo desmontar com `objdump` e reler as
decompilações antigas, como fiz aqui.
