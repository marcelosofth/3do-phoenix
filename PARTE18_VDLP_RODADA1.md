# PARTE 18 — VDLP, rodada 1 (REA/Ghidra): FUN_00474410 e FUN_004788b0

Fonte: `rea decompile PhoenixEmuProject.exe <addr> --provider ghidra` (REA 6.7.0, Ghidra 12.1.3, JDK 21,
`GHIDRA_HEADLESS_MAXMEM=3G`, analisadores pesados desligados via `REA_GHIDRA_DISABLED_ANALYZERS`).
Estado: **só decompilação e leitura. Ainda não existe `vdlp.c`; nada foi ligado ao `machine.c`.**
Pendente nesta rodada: `FUN_004740d0` (leitura da VDL, ainda rodando), `FUN_00474e90` (conversão do frame),
`FUN_00476eb0` e quem grava HEADVDL no CLIO.

## 1. Quem chama (de `FUN_00490890`, o tick do CLIO)
`param_1` das duas funções = objeto VDLP = `clio + 0x122d0` (Parte 7). Por linha de vídeo:
- a cada 0x15d8 unidades acumuladas (`line_acc`, +7 por ciclo) o contador de linha `clio+0x866c0` avança;
  NTSC (`pal==0`): vira de campo quando `(linha & 0x7ff) >= 0x106`, PAL: quando `> 0x137`;
  na virada: `linha = ~linha & 0x800` (alterna o bit de campo, zera a linha);
- chama `FUN_00474410(vdlp)`; se devolver != 0 (linha == ultima linha visivel) o tick faz
  `host+0xd0`, **`FUN_004788b0(vdlp, 0)`**, `host+0xd8` (fim de frame / apresenta).

## 2. FUN_00474410(vdlp, linha) — captura de UMA linha (a que o `clio.c` chama de `vdlp_line_pending`)
Retorna `1` quando o indice da linha visivel (`(linha & 0x7ff) - +0x40`) e igual a `*(vdlp+0x38)` (numero de linhas); o tick usa isso como "ultima linha, apresentar o frame".
Campos deduzidos (offsets sobre `vdlp`):

| offset | significado (deduzido) | evidencia |
|---|---|---|
| +0x38 | numero de linhas visiveis (240 NTSC / 288 PAL?) | `iVar3 >= iVar9` descarta a linha |
| +0x3c | largura em pixels da linha (320/384?) | limita `iVar5 = min(+0x3c, +0x74380)` |
| +0x40 | primeira linha visivel (offset de linha) | `iVar3 = (linha & 0x7ff) - *(+0x40)` |
| +0x8c | byte: modo 2 buffers (640 / dois halfwords por pixel) | escolhe laco simples ou duplo |
| +0x94 + i*0x674 | buffer de pixels 16 bit da linha `i` (stride 0x674 bytes por linha) | escrita `*(short*)` |
| +0x690/+0x6b0/+0x6d0 + i*0x674 | 3 copias de 0x20 bytes (+4): CLUT B / G / R da linha | `memcmp(...,0x20)` com +0x74318/+0x74338/+0x74358 |
| +0x6f0 + i*0x674 | cabecalho da linha: +4 OUTCONTROLL, +8 CLUTDMA, +0xc BACKGROUND; +0x10 contador | comparacao e copia no fim |
| +0x700 / +0x704 + i*0x674 | **dirty**: primeiro / ultimo pixel alterado (`-1` = limpa) | usado pelo renderizador |
| +0x74314 | CLUTDMA (word) | grava em +0x6f8 |
| +0x74316 | byte alto do CLUTDMA: bit 0x20 = EnDMA (le pixels), bit 0x02 = interlace/VMode | testes `& 0x20`, `& 2` |
| +0x74318 / +0x74338 / +0x74358 | CLUTB / CLUTG / CLUTR (0x20 bytes cada) | memcmp |
| +0x74378 | BACKGROUND | passado ao host em `004788b0` como R,G,B |
| +0x7437c | HEADVDL | copiado p/ +0x74384 na linha 0 |
| +0x74380 | MODULO (em palavras; passo = MODULO*4 bytes) | formula de avanco |
| +0x74384 | CURRENTVDL | |
| +0x74388 | CURRENTBMP (endereco do pixel na RAM) | `((x ^ 2) & 0xfffff) + ram` |
| +0x7438c | PREVIOUSBMP (segundo buffer, modo 640) | idem |
| +0x74390 | OUTCONTROLL | grava em +0x6f4 |
| +0x74394 | LineDelay (decrementa a cada linha; 0 => rele a VDL com `FUN_004740d0`) | |
| +0x74398 | ponteiro de host p/ a RAM (base do `^2` acima) | soma em `lVar12` |

Avanco do bitmap ao fim da linha (identico para CURRENTBMP e PREVIOUSBMP, bit 1 do endereco = metade do word):
`se (bmp & 2)==0: bmp += 2; senao: bmp = bmp - 2 + MODULO*4`; PREVIOUSBMP so avanca "normal" quando
`+0x74316 & 2` (interlace), senao recebe o valor de CURRENTBMP.
Leitura de pixel: `*(short *)(ram + ((CURRENTBMP ^ 2) & 0xfffff) + i*4)`, i.e. um halfword a cada 4 bytes
(a RAM do Phoenix guarda cada palavra big-endian com indice ^3; os pixels de linhas pares/impares ficam
intercalados em halfwords).
Se linha == 0 (`(linha & 0x7ff)==0`): zera `+0x74394`, copia HEADVDL -> CURRENTVDL e chama `FUN_004740d0`.

### Decompilacao REA (bruta)
```c
undefined4 FUN_00474410(longlong param_1,uint param_2)

{
  longlong lVar1;
  short sVar2;
  int iVar3;
  uint uVar4;
  int iVar5;
  byte bVar6;
  int iVar7;
  longlong lVar8;
  int iVar9;
  longlong lVar10;
  int iVar11;
  longlong lVar12;
  longlong lVar13;
  longlong lVar14;
  longlong lVar15;
  
  if ((param_2 & 0x7ff) == 0) {
    *(undefined4 *)(param_1 + 0x74394) = 0;
    *(undefined4 *)(param_1 + 0x74384) = *(undefined4 *)(param_1 + 0x7437c);
    FUN_004740d0();
    iVar9 = *(int *)(param_1 + 0x74394);
    iVar3 = *(int *)(param_1 + 0x40);
  }
  else {
    iVar9 = *(int *)(param_1 + 0x74394);
    iVar3 = *(int *)(param_1 + 0x40);
  }
  iVar3 = (param_2 & 0x7ff) - iVar3;
  if (iVar9 == 0) {
    FUN_004740d0(param_1);
  }
  iVar9 = *(int *)(param_1 + 0x38);
  if ((iVar3 < 0) || (iVar9 <= iVar3)) {
    bVar6 = *(byte *)(param_1 + 0x74316);
    goto LAB_0047445a;
  }
  iVar7 = *(int *)(param_1 + 0x74380);
  lVar10 = (longlong)iVar3;
  iVar11 = *(int *)(param_1 + 0x3c);
  iVar5 = iVar7;
  if (iVar11 <= iVar7) {
    iVar5 = iVar11;
  }
  *(undefined4 *)(param_1 + 0x700 + lVar10 * 0x674) = 0xffffffff;
  bVar6 = *(byte *)(param_1 + 0x74316);
  if ((bVar6 & 0x20) != 0) {
    lVar8 = param_1 + 0x94 + lVar10 * 0x674;
    lVar1 = lVar8 + (longlong)iVar5 * 2;
    lVar12 = (ulonglong)((*(uint *)(param_1 + 0x74388) ^ 2) & 0xfffff) +
             *(longlong *)(param_1 + 0x74398);
    lVar15 = (ulonglong)((*(uint *)(param_1 + 0x7438c) ^ 2) & 0xfffff) +
             *(longlong *)(param_1 + 0x74398);
    if (iVar11 < iVar7) {
      lVar13 = (longlong)((iVar7 - iVar11) / 2) * 4;
      lVar12 = lVar12 + lVar13;
      lVar15 = lVar15 + lVar13;
    }
    if (*(char *)(param_1 + 0x8c) == '\0') {
      lVar8 = 0;
      iVar9 = 1;
      if (iVar5 != 0) {
        lVar15 = lVar10 * 0x674 + param_1;
        do {
          sVar2 = *(short *)(lVar12 + lVar8 * 4);
          if (*(short *)(lVar1 + lVar8 * 2) != sVar2) {
            *(short *)(lVar1 + lVar8 * 2) = sVar2;
            if (*(int *)(lVar15 + 0x700) < 0) {
              *(int *)(lVar15 + 0x700) = (int)lVar8;
            }
            *(int *)(lVar15 + 0x704) = iVar9;
          }
          iVar9 = iVar9 + 1;
          lVar8 = lVar8 + 1;
        } while (iVar9 != iVar5 + 1);
LAB_004745e4:
        lVar1 = param_1 + 0x6f0 + lVar10 * 0x674;
        iVar9 = *(int *)(lVar1 + 0x10);
        if (0 < iVar9) {
          *(int *)(lVar1 + 0x10) = iVar9 + -1;
        }
      }
    }
    else if (iVar5 != 0) {
      iVar9 = 1;
      lVar13 = 0;
      lVar14 = lVar10 * 0x674 + param_1;
      do {
        sVar2 = *(short *)(lVar12 + lVar13 * 4);
        if ((*(short *)(lVar1 + lVar13 * 2) != sVar2) ||
           (*(short *)(lVar8 + lVar13 * 2) != *(short *)(lVar15 + lVar13 * 4))) {
          *(short *)(lVar1 + lVar13 * 2) = sVar2;
          *(undefined2 *)(lVar8 + lVar13 * 2) = *(undefined2 *)(lVar15 + lVar13 * 4);
          if (*(int *)(lVar14 + 0x700) < 0) {
            *(int *)(lVar14 + 0x700) = (int)lVar13;
          }
          *(int *)(lVar14 + 0x704) = iVar9;
        }
        iVar9 = iVar9 + 1;
        lVar13 = lVar13 + 1;
      } while (iVar9 != iVar5 + 1);
      goto LAB_004745e4;
    }
    lVar1 = param_1 + 0x690 + lVar10 * 0x674;
    iVar9 = memcmp((void *)(lVar1 + 4),(void *)(param_1 + 0x74318),0x20);
    if (iVar9 != 0) {
      lVar8 = lVar10 * 0x674 + param_1;
      *(undefined8 *)(lVar1 + 4) = *(undefined8 *)(param_1 + 0x74318);
      *(undefined8 *)(lVar1 + 0xc) = *(undefined8 *)(param_1 + 0x74320);
      *(undefined8 *)(lVar1 + 0x14) = *(undefined8 *)(param_1 + 0x74328);
      *(undefined8 *)(lVar1 + 0x1c) = *(undefined8 *)(param_1 + 0x74330);
      *(undefined4 *)(lVar8 + 0x700) = 0;
      *(int *)(lVar8 + 0x704) = iVar5;
    }
    lVar1 = param_1 + 0x6b0 + lVar10 * 0x674;
    iVar9 = memcmp((void *)(lVar1 + 4),(void *)(param_1 + 0x74338),0x20);
    if (iVar9 != 0) {
      lVar8 = lVar10 * 0x674 + param_1;
      *(undefined8 *)(lVar1 + 4) = *(undefined8 *)(param_1 + 0x74338);
      *(undefined8 *)(lVar1 + 0xc) = *(undefined8 *)(param_1 + 0x74340);
      *(undefined8 *)(lVar1 + 0x14) = *(undefined8 *)(param_1 + 0x74348);
      *(undefined8 *)(lVar1 + 0x1c) = *(undefined8 *)(param_1 + 0x74350);
      *(undefined4 *)(lVar8 + 0x700) = 0;
      *(int *)(lVar8 + 0x704) = iVar5;
    }
    lVar1 = param_1 + 0x6d0 + lVar10 * 0x674;
    iVar9 = memcmp((void *)(lVar1 + 4),(void *)(param_1 + 0x74358),0x20);
    if (iVar9 != 0) {
      lVar8 = lVar10 * 0x674 + param_1;
      *(undefined8 *)(lVar1 + 4) = *(undefined8 *)(param_1 + 0x74358);
      *(undefined8 *)(lVar1 + 0xc) = *(undefined8 *)(param_1 + 0x74360);
      *(undefined8 *)(lVar1 + 0x14) = *(undefined8 *)(param_1 + 0x74368);
      *(undefined8 *)(lVar1 + 0x1c) = *(undefined8 *)(param_1 + 0x74370);
      *(undefined4 *)(lVar8 + 0x700) = 0;
      *(int *)(lVar8 + 0x704) = iVar5;
    }
    bVar6 = *(byte *)(param_1 + 0x74316);
    iVar9 = *(int *)(param_1 + 0x38);
  }
  lVar1 = param_1 + 0x6f0 + lVar10 * 0x674;
  if ((*(int *)(lVar1 + 4) == *(int *)(param_1 + 0x74390)) &&
     (iVar7 = *(int *)(lVar1 + 8), iVar7 == *(int *)(param_1 + 0x74314))) {
    iVar11 = *(int *)(param_1 + 0x74378);
    if (*(int *)(lVar1 + 0xc) == iVar11) goto LAB_0047445a;
  }
  else {
    iVar7 = *(int *)(param_1 + 0x74314);
    iVar11 = *(int *)(param_1 + 0x74378);
  }
  lVar10 = lVar10 * 0x674 + param_1;
  *(int *)(lVar10 + 0x6f4) = *(int *)(param_1 + 0x74390);
  *(int *)(lVar10 + 0x6f8) = iVar7;
  *(int *)(lVar10 + 0x6fc) = iVar11;
  *(undefined4 *)(lVar10 + 0x700) = 0;
  *(int *)(lVar10 + 0x704) = iVar5;
LAB_0047445a:
  if ((bVar6 & 2) == 0) {
    uVar4 = *(uint *)(param_1 + 0x74388);
    *(uint *)(param_1 + 0x7438c) = uVar4;
  }
  else {
    uVar4 = *(uint *)(param_1 + 0x7438c);
    if ((uVar4 & 2) == 0) {
      *(uint *)(param_1 + 0x7438c) = uVar4 + 2;
      uVar4 = *(uint *)(param_1 + 0x74388);
    }
    else {
      *(uint *)(param_1 + 0x7438c) = (uVar4 - 2) + *(int *)(param_1 + 0x74380) * 4;
      uVar4 = *(uint *)(param_1 + 0x74388);
    }
  }
  if ((uVar4 & 2) == 0) {
    iVar7 = uVar4 + 2;
    *(int *)(param_1 + 0x74388) = iVar7;
  }
  else {
    iVar7 = (uVar4 - 2) + *(int *)(param_1 + 0x74380) * 4;
    *(int *)(param_1 + 0x74388) = iVar7;
  }
  *(int *)(param_1 + 0x74394) = *(int *)(param_1 + 0x74394) + -1;
  return CONCAT31((int3)((uint)iVar7 >> 8),iVar3 == iVar9);
}
```

## 3. FUN_004788b0(vdlp, flag) — fim de frame / apresentacao
```c
/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_004788b0(longlong param_1,char param_2)

{
  uint uVar1;
  char cVar2;
  
  uVar1 = *(uint *)(param_1 + 0x74378);
  (**(code **)(*_DAT_00cf4050 + 0x18))
            (_DAT_00cf4050,*(undefined4 *)(param_1 + 0x88),uVar1 & 0xff,uVar1 >> 8 & 0xff,
             uVar1 >> 0x10 & 0xff);
  if ((*(int *)(param_1 + 0x90) == 2) && (param_2 == '\0')) {
    FUN_00476eb0(param_1);
    return;
  }
  cVar2 = FUN_00474e90(param_1,0,*(undefined4 *)(param_1 + 0x38),*(undefined1 *)(param_1 + 0x8c));
  if ((cVar2 == '\0') &&
     (((int *)_DAT_00cf4050[2] == (int *)0x0 || (*(int *)_DAT_00cf4050[2] == 0)))) {
    return;
  }
                    /* WARNING: Could not recover jumptable at 0x00478947. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)(*_DAT_00cf4050 + 0x30))(_DAT_00cf4050,*(undefined4 *)(param_1 + 0x88),0);
  return;
}
```
Leitura: chama o host (`_DAT_00cf4050`, vtable +0x18) com o id do frame (`vdlp+0x88`) e o BACKGROUND em
R,G,B; se `vdlp+0x90 == 2 && flag == 0` chama `FUN_00476eb0` (outro caminho de saida); senao converte o frame
com `FUN_00474e90(vdlp, 0, vdlp+0x38 /*linhas*/, vdlp+0x8c /*modo*/)` e apresenta pelo host (vtable +0x30).

## 4. O que falta para escrever o `vdlp.c`
1. `FUN_004740d0` — leitura da lista (VDL): como CLUTDMA/HEADVDL/MODULO/CLUT sao lidos da RAM.
2. `FUN_00474e90` — conversao do frame para RGB (e o tamanho 384x288 / 320x240).
3. Onde o CLIO grava HEADVDL/CLUT/BACKGROUND (pokes em 0x03400400..0x034005ff e 0x034017d0..?).
