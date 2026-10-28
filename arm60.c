/* arm60.c - CPU ARM60 reconstruida do PhoenixEmuProject.exe. Ver PARTE9_ARM60.md e PARTE10_ARM60_FECHAMENTO.md.
 * Convencoes do .exe: r[15] = endereco da instrucao + 4 durante a execucao (leituras de R15 somam +4, ou +8
 * com shift por registrador); contador de ciclos negativo (cyc), retorno = -cyc.
 * Ciclos: base 1; B/BL/SWI/UND 5; DP shift-reg 2; DP/LDR com PC 5/9; STR 6; SWP 8; MUL p/2+2;
 *         LDM n+4 (com PC n+8); STM n+5. */
#include "arm60.h"
#include <string.h>
#include <stdio.h>

static const uint16_t cond_tab[16] = {0xf0f0,0x0f0f,0xcccc,0x3333,0xff00,0x00ff,0xaaaa,0x5555,
                                      0x0c0c,0xf3f3,0xaa55,0x55aa,0x0a05,0xf5fa,0xffff,0x0000};
/* tabela 0xaf66e0: modo -> indice de banco; -1 = invalido */
static int bank_of(uint32_t m){ switch(m&0x1f){case 0x10:return 0;case 0x11:return 1;case 0x12:return 2;
    case 0x13:return 3;case 0x17:return 4;case 0x1b:return 5;} return -1; }

/* FUN_00422800 (e as 6 rotinas 421eb0/422020/422200/422380/422500/422680): troca de banco */
void arm60_set_mode(arm60_t *c, uint32_t m){
    int nb=bank_of(m), ob=c->cur_bank;
    if(nb<0 || nb==ob) return;
    if(ob==1){ for(int i=0;i<7;i++) c->fiq[i]=c->r[8+i]; for(int i=0;i<5;i++) c->r[8+i]=c->usrhi[i]; }
    else { c->b13[ob]=c->r[13]; c->b14[ob]=c->r[14]; }
    if(nb==1){ for(int i=0;i<5;i++) c->usrhi[i]=c->r[8+i]; for(int i=0;i<7;i++) c->r[8+i]=c->fiq[i]; }
    else { c->r[13]=c->b13[nb]; c->r[14]=c->b14[nb]; }
    c->cur_bank=nb;
}
/* FUN_00423170 / FUN_004234c0: escrita/leitura de registrador do banco USR (LDM^/STM^/LDRT/STRT) */
static uint32_t user_get(arm60_t *c,int n){
    if(n==15||c->cur_bank==0) return c->r[n];
    if(c->cur_bank==1){ if(n<=7) return c->r[n]; if(n<=12) return c->usrhi[n-8]; return c->b13[0]*(n==13)+c->b14[0]*(n==14); }
    if(n<=12) return c->r[n];
    return n==13?c->b13[0]:c->b14[0];
}
static void user_set(arm60_t *c,int n,uint32_t v){
    if(n==15||c->cur_bank==0){ c->r[n]=v; return; }
    if(c->cur_bank==1){ if(n<=7){ c->r[n]=v; return; } if(n<=12){ c->usrhi[n-8]=v; return; } }
    else if(n<=12){ c->r[n]=v; return; }
    if(n==13) c->b13[0]=v; else c->b14[0]=v;
}
/* FUN_009369b0: grava CPSR (MSR completo e retorno de excecao) */
static void load_cpsr(arm60_t *c, uint32_t v){
    arm60_set_mode(c, v|0x10);
    c->cpsr=(v&0xf00000dfu)|0x10;
    if(c->mode_hook) c->mode_hook(c->ud);
}
static void restore_spsr(arm60_t *c){ int b=bank_of(c->cpsr); if(b>0) load_cpsr(c,c->spsr[b]); }

void arm60_reset(arm60_t *c){
    arm60_t t=*c; memset(c,0,sizeof *c);
    c->ud=t.ud;c->rd32=t.rd32;c->fetch=t.fetch;c->rd8=t.rd8;c->wr32=t.wr32;c->wr8=t.wr8;
    c->swi_hook=t.swi_hook;c->mode_hook=t.mode_hook;c->debug_log=t.debug_log;c->trace=t.trace;
    c->cpsr=0xc0|M_SVC; c->cur_bank=3;
}
static void enter_exc(arm60_t *c, uint32_t mode, uint32_t ret, uint32_t vec, uint32_t extra){
    if (c->trace && c->debug_log) c->debug_log(c->ud, "[ARM60] ENTER_EXC: PC=%08x -> vec=%08x mode=%d extra=%08x", ret, vec, mode, extra);
    if (mode == M_FIQ) c->stat_fiq++; else if (mode == M_IRQ) c->stat_irq++;
    uint32_t old=c->cpsr;
    arm60_set_mode(c,mode);
    c->spsr[bank_of(mode)]=old;
    c->cpsr=(old&~0x1fu)|mode|0x80|extra;
    c->r[14]=ret; c->r[15]=vec;
    if(c->mode_hook) c->mode_hook(c->ud);
}
void arm60_irq(arm60_t *c){ if(!(c->cpsr&0x80)) enter_exc(c,M_IRQ,c->r[15]+4,0x18,0); }
void arm60_fiq(arm60_t *c){ if(!(c->cpsr&0x40)) enter_exc(c,M_FIQ,c->r[15]+4,0x1c,0x40); }

static uint32_t rdreg(arm60_t *c,int n,int extra){ return n==15 ? c->r[15]+4+extra : c->r[n]; }

/* FUN_00423010: type 0 LSL,1 LSR,2 ASR,3 ROR,4 RRX (tabela 0xaf639c) */
static uint32_t shifter(arm60_t *c, uint32_t v, uint32_t amt, int type){
    uint32_t cin=(c->cpsr>>29)&1;
    if(type==4){ c->shc=v&1; return (v>>1)|(cin<<31); }
    if(amt==0){ c->shc=cin; return v; }
    switch(type){
    case 0: if(amt>32){ c->shc=0; return 0; } c->shc=(v>>(32-amt))&1; return amt==32?0:v<<amt;   /* LSL: C = bit (32-amt) */
    case 1: if(amt>32){ c->shc=0; return 0; } c->shc=(v>>(amt-1))&1; return amt==32?0:v>>amt;
    case 2: if(amt>=32){ c->shc=v>>31; return (uint32_t)((int32_t)v>>31); }
            c->shc=((int32_t)v>>(amt-1))&1; return (uint32_t)((int32_t)v>>amt);
    default: if((amt&31)==0){ c->shc=v>>31; return v; }
            c->shc=(v>>((amt&31)-1))&1; return (v>>(amt&31))|(v<<(32-(amt&31)));
    }
}
/* decodifica o operando 2 por registrador como o passo faz (imediato: 0 => LSR/ASR #32, ROR #0 => RRX) */
static uint32_t op2_reg(arm60_t *c,uint32_t op,uint32_t rm,int by_reg){
    int type=(op>>5)&3;
    if(by_reg) return shifter(c,rm,c->r[(op>>8)&15]&0xff,type);
    uint32_t amt=(op>>7)&31;
    if(amt==0){ if(type==0) return shifter(c,rm,0,0); if(type==3) return shifter(c,rm,0,4); amt=32; }
    return shifter(c,rm,amt,type);
}

/* FUN_00422860: ALU, despachada por (op>>20)&0x1f = opcode*2+S (tabela 0xaf631c).
 * Os opcodes TST/TEQ/CMP/CMN sem S sao MRS/MSR (ARMv3). */
static void alu(arm60_t *c,uint32_t op,uint32_t a,uint32_t b){
    int idx=(op>>20)&0x1f, opc=idx>>1, s=idx&1, rd=(op>>12)&15;
    int md=bank_of(c->cpsr); if(md<0) md=0;
    switch(idx){
    case 16: c->r[rd]=c->cpsr; return;                                   /* MRS Rd,CPSR */
    case 20: c->r[rd]=c->spsr[md]; return;                               /* MRS Rd,SPSR */
    case 18:                                                             /* MSR CPSR */
        if((op&0x10000) && md!=0) load_cpsr(c,b);
        else c->cpsr=(c->cpsr&0x0fffffffu)|(b&0xf0000000u);
        return;
    case 22:                                                             /* MSR SPSR */
        if((op&0x10000) && md!=0) c->spsr[md]=b&0xf00000dfu;
        else c->spsr[md]=(c->spsr[md]&0x0fffffffu)|(b&0xf0000000u);
        return;
    }
    uint32_t res=0, cf=(c->cpsr>>29)&1, carry=0, ov=0; uint64_t t; int logic=0, write=1;
    switch(opc){
    case 0: res=a&b; logic=1; break;          case 1: res=a^b; logic=1; break;
    case 2: t=(uint64_t)a+(~b)+1; res=(uint32_t)t; carry=t>>32; ov=((a^b)&(a^res))>>31; break;
    case 3: t=(uint64_t)b+(~a)+1; res=(uint32_t)t; carry=t>>32; ov=((b^a)&(b^res))>>31; break;
    case 4: t=(uint64_t)a+b; res=(uint32_t)t; carry=t>>32; ov=(~(a^b)&(a^res))>>31; break;
    case 5: t=(uint64_t)a+b+cf; res=(uint32_t)t; carry=t>>32; ov=(~(a^b)&(a^res))>>31; break;
    case 6: t=(uint64_t)a+(~b)+cf; res=(uint32_t)t; carry=t>>32; ov=((a^b)&(a^res))>>31; break;
    case 7: t=(uint64_t)b+(~a)+cf; res=(uint32_t)t; carry=t>>32; ov=((b^a)&(b^res))>>31; break;
    case 8: res=a&b; logic=1; write=0; break; case 9: res=a^b; logic=1; write=0; break;
    case 10: t=(uint64_t)a+(~b)+1; res=(uint32_t)t; carry=t>>32; ov=((a^b)&(a^res))>>31; write=0; break;
    case 11: t=(uint64_t)a+b; res=(uint32_t)t; carry=t>>32; ov=(~(a^b)&(a^res))>>31; write=0; break;
    case 12: res=a|b; logic=1; break;         case 13: res=b; logic=1; break;
    case 14: res=a&~b; logic=1; break;        default: res=~b; logic=1; break;
    }
    if(s){ /* o .exe atualiza os flags mesmo com Rd=PC (e depois restaura o SPSR) */
        uint32_t f=c->cpsr&0x0fffffffu;
        if(logic){ carry=c->shc&1; ov=(c->cpsr>>28)&1; }
        c->cpsr=f|(res&0x80000000u)|(res?0:0x40000000u)|(carry<<29)|(ov<<28);
    }
    if(!write) return;
    if(rd==15){ c->r[15]=res; c->cyc=-5; if(s) restore_spsr(c); }
    else c->r[rd]=res;
}

/* FUN_004231d0 (LDM) e FUN_00423510 (STM) */
static void ldm_stm(arm60_t *c,uint32_t op){
    int rn=(op>>16)&15, L=(op>>20)&1, W=(op>>21)&1, S=(op>>22)&1, U=(op>>23)&1, P=(op>>24)&1;
    uint32_t list=op&0xffff, n=__builtin_popcount(list), base=rdreg(c,rn,4);
    if(rn!=15) base=c->r[rn];
    uint32_t addr, nb;
    switch((op>>23)&3){ case 0: addr=base-4*n+4; nb=base-4*n; break; case 1: addr=base; nb=base+4*n; break;
                        case 2: addr=base-4*n; nb=base-4*n; break; default: addr=base+4; nb=base+4*n; break; }
    (void)U;(void)P;
    if(L){
        int pcin=(list>>15)&1, user=S&&!pcin;
        c->cyc -= 3+(int)n;                                 /* o chamador ja pos -5 se PC na lista */
        if(W){ if(user) user_set(c,rn,nb); else c->r[rn]=nb; }   /* writeback ANTES das cargas */
        for(int i=0;i<16;i++) if((list>>i)&1){ uint32_t v=c->rd32(c->ud,addr&~3u); addr+=4;
            if(user) user_set(c,i,v); else c->r[i]=v; }
        if(S&&pcin) restore_spsr(c);
    } else {
        c->cyc -= 4+(int)n;
        int low = (op & ((1u<<rn)-1)) != 0;                 /* ha registrador abaixo de Rn na lista */
        int user=S;
        if(W && low){ if(user) user_set(c,rn,nb); else c->r[rn]=nb; }   /* escreve base antes */
        for(int i=0;i<15;i++) if((list>>i)&1){ uint32_t v=user?user_get(c,i):c->r[i]; c->wr32(c->ud,addr&~3u,v); addr+=4; }
        if((list>>15)&1){ c->wr32(c->ud,addr&~3u,c->r[15]+8); }
        if(W && !low){ if(user) user_set(c,rn,nb); else c->r[rn]=nb; } /* senao, depois das escritas */
    }
}

int arm60_step(arm60_t *c){
    uint32_t pc=c->r[15], op=c->fetch(c->ud,pc);
    c->cyc=-1; c->shc=(c->cpsr>>29)&1;
    if(!((cond_tab[op>>28]>>(c->cpsr>>28))&1)){ c->r[15]=pc+4; return 1; }
    c->r[15]=pc+4;
    uint32_t idx=((op>>23)&0x1e)|((op>>4)&1);
    switch(idx){
    case 1: case 3:
        if((op&0x0fc000f0)==0x90){                                  /* MUL / MLA */
            int rd=(op>>16)&15, rs=(op>>8)&15, rm=op&15, rn=(op>>12)&15, A=(op>>21)&1, S=(op>>20)&1;
            uint32_t s=c->r[rs], r;
            c->cyc = s ? -(((31-__builtin_clz(s))/2)+2) : -2;       /* igual para MLA (sem ciclo extra) */
            if(rd==rm) r = A ? c->r[rn] : 0;                        /* Rd==Rm: MUL zera, MLA devolve Rn */
            else { r=c->r[rm]*s; if(A) r+=c->r[rn]; }
            c->r[rd]=r;
            if(S) c->cpsr=(c->cpsr&0x3fffffffu)|(r&0x80000000u)|(r?0:0x40000000u);
            break; }
        if(idx==3 && (op&0x0fb00ff0)==0x01000090){                  /* SWP */
            int rn=(op>>16)&15, rd=(op>>12)&15, rm=op&15; uint32_t a=c->r[rn], old;
            if(op&0x400000){ old=c->rd8(c->ud,a); c->wr8(c->ud,a,(uint8_t)c->r[rm]); }
            else { old=c->rd32(c->ud,a&~3u); if(a&3) old=(old>>((a&3)*8))|(old<<(32-(a&3)*8)); c->wr32(c->ud,a&~3u,c->r[rm]); }
            c->r[rd]=old; c->cyc=-8; break; }
        { uint32_t rm=rdreg(c,op&15,4), rn=rdreg(c,(op>>16)&15,4);  /* DP com shift por registrador */
          uint32_t b=op2_reg(c,op,rm,1); c->cyc=-2; alu(c,op,rn,b); }
        break;
    case 0: case 2: { uint32_t rm=rdreg(c,op&15,0), rn=rdreg(c,(op>>16)&15,0);
          uint32_t b=op2_reg(c,op,rm,0); alu(c,op,rn,b); } break;
    case 4:case 5:case 6:case 7: { uint32_t rot=((op>>8)&15)*2, v=op&0xff, b=rot?(v>>rot)|(v<<(32-rot)):v;
          if(rot){ c->shc=b>>31; } alu(c,op,rdreg(c,(op>>16)&15,0),b); } break;
    case 8:case 9:case 10:case 11:case 12:case 14: {
        int rn=(op>>16)&15, rd=(op>>12)&15, L=(op>>20)&1, B=(op>>22)&1, U=(op>>23)&1, P=(op>>24)&1, W=(op>>21)&1;
        int T=(!P)&&W;                                              /* LDRT/STRT: registrador do banco USR */
        uint32_t off = idx<12 ? (op&0xfff) : op2_reg(c,op,rdreg(c,op&15,0),0);
        uint32_t base=rdreg(c,rn,0), nb=U?base+off:base-off, ea=P?nb:base;
        if(L){ uint32_t v;
            if(B) v=c->rd8(c->ud,ea); else { v=c->rd32(c->ud,ea&~3u); if(ea&3) v=(v>>((ea&3)*8))|(v<<(32-(ea&3)*8)); }
            if((!P)||W) c->r[rn]=nb;                                /* writeback antes: a carga vence */
            if(rd==15){ c->r[15]=v; c->cyc=-9; }
            else { if(T) user_set(c,rd,v); else c->r[rd]=v; c->cyc=-5; } }
        else { uint32_t v = rd==15 ? c->r[15]+8 : (T?user_get(c,rd):c->r[rd]);
            if(B) c->wr8(c->ud,ea,(uint8_t)v); else c->wr32(c->ud,ea&~3u,v);
            if((!P)||W){ c->r[rn]=nb; } c->cyc=-6; } } break;
    case 16:case 17:case 18:case 19:
        if((op&0x100000)&&(op&0x8000)) c->cyc=-5;                   /* LDM com PC */
        ldm_stm(c,op); break;
    case 20:case 21:case 22:case 23: { int32_t o=(int32_t)(op<<8)>>6;
        if(op&0x01000000) c->r[14]=pc+4;
        c->r[15]=pc+8+o; c->cyc=-5; } break;
    case 30:case 31: { uint32_t imm=op&0xffffff;
        if(c->swi_hook && c->swi_hook(c->ud,imm)){ c->cyc-=4; break; }
        if (c->trace && c->debug_log) c->debug_log(c->ud, "[ARM60] SWI at PC=%08x imm=%06x", pc, imm);
        enter_exc(c,M_SVC,pc+4,8,0); c->cyc=-5; } break;
    default: 
        if (c->stat_undef++ == 0) { c->undef_pc = pc; c->undef_op = op; }
        if (c->trace && c->debug_log) c->debug_log(c->ud, "[ARM60] UNDEFINED INSTRUCTION at PC=%08x OP=%08x", pc, op);
        enter_exc(c,M_UND,pc+4,4,0); 
        c->cyc=-5; 
        break;
    }
    return -c->cyc;
}
