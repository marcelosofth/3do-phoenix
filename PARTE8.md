# Phoenix 3DO — RE, parte 8: XBUS (DMA por tick), dispositivo de CD e classes `dev*`

Escopo: só o `PhoenixEmuProject.exe`. Convenções: `clio = this+0x1000dc8`; `R = clio+0x8668c` (registrador 0xNNN = `R+0xNNN`).
Método: igual à parte 7 (sem rede → sem Ghidra/REA): `objdump` do PE, leitura de vtables/RTTI em `.rdata` e a decompilação
antiga `dec_0048f690.c`. **[hipótese]** marca o que não está confirmado.

## 1. Classes C++ identificadas pelo RTTI (nomes mangled do GCC no `.rdata`)

| Classe | Typeinfo | Vtable (1º slot) | Papel |
|---|---|---|---|
| `devCLIO` | 0xbe2c00 | 0xc240f0 | o CLIO inteiro (confirma que a vtable da parte 7 é a do CLIO) |
| `Device` | 0xbe2a10 | — | classe base abstrata dos dispositivos |
| `devXBUS` | 0xbe2c40 | 0xc24230 | barramento de expansão, **embutido em `clio+0x28`** |
| `devFIFO<unsigned char>` | 0xbe2c20 | 0xc24190 | FIFOs de 8 bits (Command/Status/Data) |

Construtores (confirmados por `call` no desmontado):
- `FUN_00491a70` = ctor de `devCLIO`; chama `FUN_004986a0` (0x491c14).
- `FUN_004986a0` (14 510 B) = ctor de `devXBUS`. Registra `SelectedDevice`, `Identify`, `CD-Drive`.
  Dentro dele, **0x498832: `call FUN_004aca20` com `rcx = this_xbus + 0x28`** → o **CD-Drive é um objeto embutido em
  `clio+0x28+0x28 = clio+0x50`**.
- `FUN_004aca20` (9 767 B) = ctor do **CD-Drive** (registra `CommandFIFO`, `StatusFIFO`, `DataFIFO`, `PollReg`, `StatusReg`,
  `LastError`, `CurrSector`, ver strings em 0xb04913–0xb04953).

**Correção à parte 4 (offsets do XBUS):** os números `+0x3e0` (tabela de 16 dispositivos) e `+0x460` (seleção) eram
**relativos ao objeto devXBUS**, não ao CLIO. Em endereços absolutos do CLIO:

| Campo | Relativo a devXBUS (`clio+0x28`) | Absoluto no CLIO |
|---|---|---|
| tabela de 16 ponteiros de dispositivo | +0x3e0 | **clio+0x408** |
| byte de seleção (bits 3..0 = ID; bit 7 = máscara do poll) | +0x460 | **clio+0x488** |
| dispositivo ID 0 (= CD-Drive) | gravado em +0x3e0 (0x498941) | **ponteiro p/ clio+0x50** |

O ctor zera as entradas de ID 1–14 (loop 0x3e8..0x457) e uma palavra em +0x468. Ou seja, **só o ID 0 tem dispositivo**
(o CD); qualquer outro ID cai no "sem dispositivo → 0x30" da parte 4.

Também corrijo a leitura da parte 7, seção 4: os slots +0x98 e +0x138 daquela região **não** pertencem à vtable do CLIO; são
as **typeinfo de `devFIFO<uchar>` e `devXBUS`**, vtables vizinhas no `.rdata` (offset-to-top = 0 em ambas). A vtable do `devCLIO`
tem só 19 slots (+0x00..+0x90).

## 2. Motor de DMA do XBUS — `FUN_0048f690` (696 B), chamado **uma vez por tick** de `FUN_00490890`

Esta é a peça que a parte 3 tratou como "o Phoenix não executa o DMA no poke de 0x304". Agora está explicado **onde** ele executa.

### 2.1 Condições de ativação
```
if ( (R[0x304] & 0x100000)          // bit 20 do registrador de DMA habilitado (byte clio+0x86992 & 0x10)
  && (R[0x400] & 0x800)             // direção "dispositivo → memória" (byte clio+0x86a8d & 8)
  && (poll_do_dispositivo & 0x20) ) // dispositivo selecionado tem dado pronto (FUN_00498570)
```

### 2.2 Parâmetros vêm do MADAM por callback
Endereço e tamanho são lidos com o ponteiro-membro de **MADAM Peek** (`clio+0x966b0/b8/c0`) nos offsets **0x540 (endereço)** e
**0x544 (tamanho)**; se o callback é nulo, usa um valor em cache (`clio+0x966c8`). Isto casa com a parte 4: MADAM Peek de 0x400–0x5ff
devolve `& 0xfffffc` (endereços de 24 bits alinhados em 4). Ao final, o estado atualizado é escrito de volta no MADAM com o
ponteiro-membro de **MADAM Poke** (`clio+0x96698/a0/a8`): **0x544 ← restante** e **0x540 ← novo endereço**
(o decompilado perdeu o 3º argumento da primeira chamada; assumo que é o restante **[inferido]**).

### 2.3 Sequência
1. `R[0x400] = (R[0x400] & ~0x80) | 0x400` — **fecha o gate XBUS (bit 0x80) e liga 0x400** durante o DMA.
2. Se `R[0x404]` bit 9 (0x200) está ligado (palavra crua, sem tratamento no Poke; **[hipótese]** "escrever na memória") e o tamanho é ≥ 0:
   laço de 4 bytes por vez enquanto `poll & 0x20`:
   - lê 4 bytes do FIFO de dados (`FUN_00498540`) e grava em RAM em `ram[(end + k) ^ 3]` (**big-endian dentro de cada palavra**);
   - **só grava se `end < 0x300000`** (3 MB: DRAM + VRAM); acima disso descarta mas o endereço continua avançando;
   - `end += 4`, `restante -= 4`; para quando `restante < 0` ou o dispositivo deixa de ter dado.
   - se escreveu algo, chama o callback de "RAM alterada" (`clio+0x966d0/d8/e0`) com `(início, nº de bytes)`.
   Se o bit 9 está desligado, nada é copiado e o DMA **termina imediatamente** (restante = −4).
3. Escreve de volta 0x544/0x540 no MADAM (item 2.2).
4. **Se terminou (`restante < 0`):**
   - se `R[0x400] & 0x40` → `FUN_00498690(base, R[0x500])`: **aplica a seleção de dispositivo que ficou guardada** (é o
     `R[0x500] = val; R[0x400] |= 0x40` do Poke com gate fechado, parte 5, item 1);
   - `R[0x304] &= ~0x100000` (desliga o canal de DMA 20);
   - `R[0x400] = (R[0x400] & 0xfffff3bf) | 0x80` (apaga 0x400, 0x800, 0x40 e **reabre o gate 0x80**);
   - `FUN_0048ef80(clio, 0x20000000, 0)` → **IRQ0 bit 29 = fim do DMA do XBUS**.

Se o dispositivo para de ter dado no meio, o DMA fica pendente e continua no tick seguinte (estado nos registradores do MADAM).

### 2.4 O que isso explica
- **Gate 0x80:** não é só um "enable"; é uma trava que o próprio DMA fecha e reabre. Durante o DMA, um Poke de seleção
  em 0x500–0x53f é guardado e só aplicado no fim. Isso explica por que o Poke de 0x400 força `|= 0x80` quando `0x800` está limpo.
- **Poke 0x304 sem executar:** o 0x304 só "arma"; quem move dados é este motor, com o relógio da parte 7.
- A transferência é **granular por tick**, limitada pelo que o dispositivo oferece (poll 0x20), sem bloquear o ARM.

## 3. O bloco de setor de CD no início de `FUN_00490890` — agora **ancorado**

Com o CD-Drive em `clio+0x50`, os campos antes "soltos" ficam no objeto do CD (offsets absolutos no CLIO):

| Campo | Uso observado |
|---|---|
| `+0x78` | buffer de setor (= CD+0x28), 0x800 bytes |
| `+0x270` | acumulador de ciclos do CD, truncado em `(+0x34c) << 4` |
| `+0x34c` | intervalo por setor (em ciclos escalados) |
| `+0x280` | contador de setor (candidato a **CurrSector**) |
| `+0x284` | trabalho pendente; se negativo liga o flag `+0x277 \|= 0x10` e passa a positivo |
| `+0x277` | bits de status 0x10/0x20, com sinal via `FUN_004aa150(clio+0x50)` |
| `+0x338/+0x33c/+0x340` | anel do FIFO de dados (tamanho / escrita / leitura) |
| `+0x348` | identificador do disco no objeto de serviços do host |

Quando o anel está vazio e `+0x270 ≥ +0x34c`: pergunta ao objeto do host `_DAT_00cf4050` se o setor existe (vtable +0x130),
lê 0x800 bytes para `+0x78` (+0xe0), incrementa `+0x280`, reabastece o anel (+0x138) e atualiza flags. **[hipótese]** `_DAT_00cf4050`
é um objeto "serviços do host" (mutex +0xd0/+0xd8, saída de áudio +0xa8, leitura do disco). É o mesmo objeto que protege o DSP na parte 7.

Resumo: **o CD é entregue por tempo em ciclos ARM** (um setor por `+0x34c` ciclos), enche um FIFO, e o motor de DMA da seção 2
drena esse FIFO para a RAM via XBUS.

## 4. Pseudocódigo do motor de DMA (fiel ao lido)

```c
void xbus_dma_tick(clio_t *c) {
    if (!(R[0x304] & 0x100000) || !(R[0x400] & 0x800)) return;
    if (!(xbus_poll(c) & 0x20)) return;
    uint32_t addr = madam_peek(0x540), len = (int32_t)madam_peek(0x544);   // ou cache
    R[0x400] = (R[0x400] & ~0x80) | 0x400;
    int32_t left = -4;
    if ((R[0x404] & 0x200) && len >= 0) {
        uint32_t a = addr;
        do {
            left = len;
            if (!(xbus_poll(c) & 0x20)) break;
            for (int k = 0; k < 4; k++) { uint8_t b = xbus_read_data(c); if (a + k < 0x300000) ram8[(a + k) ^ 3] = b; }
            a += 4; len -= 4; left = len;
        } while (len >= 0);
        if (a != addr) ram_written_cb(addr, a - addr);
        addr = a;  if (left < 0) left = -4;
    }
    madam_poke(0x544, left); madam_poke(0x540, addr);
    if (left < 0) {
        if (R[0x400] & 0x40) xbus_select(c, R[0x500]);
        R[0x304] &= ~0x100000;
        R[0x400] = (R[0x400] & 0xfffff3bf) | 0x80;
        raise_irq(c, 0x20000000, 0);
    }
}
```

## 5. Pendências

1. Método virtual do CD-Drive para poll/status/FIFO (vtable do objeto em `clio+0x50`): ainda não localizei a **vtable do CD**
   (o ctor `FUN_004aca20` só registra campos; a vtable é gravada fora dele). Precisa de Ghidra para seguir xrefs com confiança.
2. `R[0x404]` bit 9: confirmar quem o liga (Poke cru de 0x404?) — só com decompilação do caminho de escrita.
3. Objeto `_DAT_00cf4050` (serviços do host) e seus slots +0x130/+0xe0/+0x138/+0xa8/+0xd0/+0xd8.
4. `FUN_0046d570` (passo do DSP por amostra) e os métodos do VDLP (`FUN_00474410`, `FUN_004788b0`) — partes 9 e 10.

## 6. Sobre rede / REA

Continua sem rede no sandbox (DNS de `github.com` falha), então o REA/Ghidra não rodam aqui. O que foi feito nesta parte
(RTTI, vtables, tabelas de dispositivos, DMA) não depende deles, mas as pendências 1–3 dependem de decompilar funções novas.
Libere `github.com` e `registry.npmjs.org` nas configurações para eu retomar o REA.
