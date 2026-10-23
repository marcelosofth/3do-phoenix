# Parte 12 — Fecha os 5 itens em aberto da Parte 11 (fetch/read8/write8/blocos)

Método: sem Ghidra/REA (não há pacote novo deles ainda) — `objdump -d -M intel PhoenixEmuProject.exe`
direto no `.exe` que o usuário reenviou, mesmo método das partes 9/10. Decodificadas as 5 funções que a
Parte 11 deixou como "não verificado, faltam as decompilações no pacote":

| Função | Endereço | Papel |
|---|---|---|
| `FUN_004a0030` | 0x4a0030–0x4a0086 | fetch de instrução |
| `FUN_004a0130` | 0x4a0130–0x4a01e9 | read8 |
| `FUN_004a0530` | 0x4a0530–0x4a05d1 | write8 |
| `FUN_004a0090` | 0x4a0090–0x4a0126 | ponteiro de host p/ bloco de LEITURA (aceleração de LDM) |
| `FUN_004a0340` | 0x4a0340–0x4a03ab | ponteiro de host p/ bloco de ESCRITA (aceleração de STM) |

## Achados (respondem as 3 dúvidas (a)/(b)/(c) da Parte 11)

**(a) read8/write8 chamam `FUN_0058e9f0` (vram_touch) na região 2? Com que condição?**
Sim, os dois — com a **mesma condição** do read32: só se `*(máquina+0x3c)==0` (`vram_flag==0`). A
região 0/1 (RAM fora da "VRAM") nunca chama, em nenhum dos três (read32/read8/write8). **write8 chama**
`vram_touch`, diferente do **write32, que nunca chama** — essa assimetria (write32 não avisa, write8 avisa)
é real no binário, não erro de leitura; confirmada duas vezes (condição idêntica nos dois: `cmp byte
[rcx+0x3c],0; jne skip; call 0x58e9f0`).

**(b) índice de NVRAM em read8/write8: `(addr>>2)&0x7fff` ou `addr^3`?**
`(addr>>2)&0x7fff`, **sem** `^3` — confirmado, é diferente de RAM/BIOS (que usam `endereço^3` pra achar o
byte dentro da palavra big-endian). Ou seja, o acesso a byte da NVRAM no Phoenix não "desempacota" a
palavra por índice de byte: ele cai direto no mesmo índice de palavra que o read32/write32 usariam,
truncado pra `uint8_t`. Isso é estranho frente ao hardware real (NVRAM do 3DO é endereçada por palavra
de 32 bits mas o Phoenix a trata como array de bytes indexado por índice de *palavra*) — provavelmente um
detalhe de implementação do Phoenix que não precisa ser replicado byte a byte num port pro FreeDO/opera
(que já endereça NVRAM do jeito certo); documentado aqui só como fato do `.exe`.

**(c) o fetch repete o read32 byte a byte?**
Não — `FUN_004a0030` é uma função própria, **nunca chama `vram_touch`**, nem na região 2. O código antigo
(`mem_word()` compartilhada entre fetch e read32) estava **errado**: dava pra instrução buscada na região 2
disparar o aviso de VRAM, o que o `.exe` nunca faz. **Bug corrigido** nesta rodada (ver abaixo).

## Blocos (FUN_004a0090/004a0340) — só aceleração, não usados pelo laço

`FUN_004a0090`: ponteiro de host pra um intervalo `[a, a+len)` usado pelo LDM; exige que `a>>20 ==
(a+len)>>20` (bloco inteiro numa única região), senão devolve `NULL` (o chamador cai no caminho lento).
Cobre regiões 0/1 (RAM direto), 2 (RAM + `vram_touch` condicional, só **uma vez pro bloco inteiro**, não
por palavra) e 0x30/0x60 (BIOS, mesma regra de banco). Resto: `NULL`.

`FUN_004a0340`: ponteiro de host de **escrita** pra STM; só cobre RAM (`0..0x2fffff`), fora disso `NULL`
(BIOS/NVRAM/MMIO sempre caem no caminho lento). Depois de calcular o ponteiro, percorre a tabela de
*watchpoints* (`+0x159f568`) palavra por palavra chamando `FUN_557550` nas marcadas — é só depuração
(mesma tabela já registrada como ignorada na Parte 11), não muda o estado emulado. **Nunca** chama
`vram_touch` (igual ao write32 — escrita não avisa VRAM, só leitura).

Como o caminho lento (`ph_read32`/`ph_write32` palavra a palavra, já implementado no `arm60.c`) reproduz o
mesmo efeito observável (Parte 10), os ponteiros de bloco foram implementados em `ph_block_read_ptr`/
`ph_block_write_ptr` só como referência/possível otimização futura — **o laço de LDM/STM não foi mudado
para usá-los**.

## Bugs corrigidos no `machine.c`

1. **`ph_fetch` chamava `vram_touch` na região 2** (herdava o `case 2` de `mem_word()`, compartilhada com
   `read32`). `FUN_004a0030` nunca faz essa chamada. Corrigido: `ph_fetch` agora tem corpo próprio, sem a
   chamada.
2. **`ph_read8`/`ph_write8` chamavam `vram_touch` incondicionalmente** na região 2, sem checar
   `vram_flag` (a Parte 11 já tinha marcado isso como "não verificado" e deixou a chamada sem a guarda por
   segurança). Agora checam `!m->vram_flag` antes de chamar, igual ao `read32`.

Os dois bugs são comportamentais: antes da correção, uma leitura de instrução ou um acesso de byte em VRAM
(região `0x002xxxxx`) podia disparar `vram_touch` (ex.: um redraw/sync) em momentos em que o `.exe`
original não dispara — o que atrapalharia qualquer comparação A/B por savestate.

## Verificação
`make` roda `test_clio`, `test_arm60`, `test_machine` (seção nova "5) Parte 12"); `make asan` roda o mesmo
sob AddressSanitizer/UBSan. Todos passam. A seção nova cobre: fetch não chama `vram_touch` nem na região 2;
read8/write8 chamam só com `vram_flag==0`; região 0/1 nunca chama; NVRAM por byte usa `(addr>>2)&0x7fff`
sem `^3`; escrita de byte em BIOS é ignorada; os dois ponteiros de bloco devolvem o ponteiro certo dentro
da região e `NULL` fora dela.

## Itens que ainda ficam em aberto (não mudou nesta rodada)
Os grandes da Parte 11: `slice`/`mult` reais (`FUN_004a7f60`), vetor de reset e carregamento da BIOS na
RAM, MADAM (`FUN_0048adb0`/`0048b2a0`), CEL (`FUN_004894c0`), `FUN_004714d0/e0`. A Parte 13 ataca MADAM
Peek/Poke usando as decompilações que já vieram no zip anterior (`PhoenixRE_parte4.zip`).
