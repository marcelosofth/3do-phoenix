/* ph_audit.c - ver ph_audit.h */
#include "ph_audit.h"
#include <string.h>

static void addflag(ph_audit_run_t *r, const char *f) {
    size_t n = strlen(r->flags), k = strlen(f);
    if (n + k + 2 >= sizeof r->flags) return;
    if (n) r->flags[n++] = ' ';
    memcpy(r->flags + n, f, k + 1);
}
static void addflagf(ph_audit_run_t *r, const char *fmt, unsigned v) { char b[64]; snprintf(b, sizeof b, fmt, v); addflag(r, b); }

void ph_audit_finish(const ph_machine_t *m, ph_audit_run_t *r) {
    const vdlp_t *v = &m->vdlp;
    size_t n = (size_t)v->width * (size_t)v->lines, nb = 0;
    static uint8_t seen[4096 / 8];
    unsigned nc = 0; memset(seen, 0, sizeof seen);
    for (size_t i = 0; i < n; i++) {
        const uint8_t *p = v->rgb + i * 3;
        if (p[0] | p[1] | p[2]) nb++;
        unsigned h = ((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4);
        if (!(seen[h >> 3] & (1u << (h & 7)))) { seen[h >> 3] |= (uint8_t)(1u << (h & 7)); nc++; }
    }
    r->nonblack_pct = n ? 100.0 * (double)nb / (double)n : 0.0;
    r->distinct_colors = nc;
    r->flags[0] = 0;
    /* "!" = defeito provavel (investigar); "i:" = informativo (recurso aproximado/ausente, nao e erro por si so) */
    if (m->cpu.stat_undef)            addflagf(r, "!UNDEF_INSN(%u)", m->cpu.stat_undef);
    if (m->stat_bad_fetch)            addflagf(r, "!BAD_FETCH(%u)", m->stat_bad_fetch);
    if (m->cel.unsupported)           addflagf(r, "!CEL_UNSUPPORTED(%u)", m->cel.unsupported);
    if (m->cd.stat_unk_cmd)           addflagf(r, "!CD_UNKNOWN_CMD(op=%02x)", m->cd.unk_cmd_first);
    if (m->cd.stat_read_oob)          addflag(r, "!CD_READ_OOB");
    if (r->frames > 600 && m->clio.stat_irq11 == 0) addflag(r, "!NO_DSP_IRQ11");
    if (r->frames > 300 && r->nonblack_pct < 0.5) addflag(r, "!BLANK_SCREEN");
    if (r->frames > 900 && r->last_change_frame + 600 < r->frames) addflag(r, "!STATIC_IMAGE_10S");
    if (r->frames > 300 && r->pc_hi - r->pc_lo < 0x100) addflag(r, "!PC_LOOP");
    if (m->stat_bad_rd)               addflagf(r, "i:BAD_READ(%u)", m->stat_bad_rd);
    if (m->stat_bad_wr)               addflagf(r, "i:BAD_WRITE(%u)", m->stat_bad_wr);
    if (m->cel.poly)                  addflag(r, "i:CEL_POLY_APPROX");
    if (m->cel.n_lrform)              addflag(r, "i:CEL_LRFORM");
    if ((m->clio.xbus_seen & ~1u) && m->clio.xbus_seen != 0xffffu) addflagf(r, "i:XBUS_OTHER_DEV(mask=%x)", m->clio.xbus_seen);
    if (m->vdlp.stat_hires_lines)     addflag(r, "i:VIDEO_HIRES_DOWNSAMPLED");
    if (m->clio.stat_sem_ack)         addflag(r, "i:DSP_SEMAPHORE_HACK");
    if (!r->flags[0]) strcpy(r->flags, "-");
}

static unsigned popc(const uint32_t *a, unsigned n) { unsigned c = 0; for (unsigned i = 0; i < n; i++) c += (unsigned)__builtin_popcount(a[i]); return c; }

void ph_audit_reset_boot(ph_machine_t *m) {      /* descarta a sondagem normal da BIOS (RAM, XBUS) feita antes do jogo */
    m->stat_bad_rd = m->stat_bad_wr = m->stat_bad_fetch = 0; m->clio.xbus_seen = 0; m->clio.stat_xbus_nodev = 0;
}

void ph_audit_report(const ph_machine_t *m, const ph_audit_run_t *r, FILE *f) {
    static const char *bn[8] = { "1(a)", "1(b)", "2", "4", "6", "8", "16(a)", "16(b)" };
    fprintf(f, "frames=%u  PC=%08x (faixa %08x..%08x)  tela: %.1f%% nao-preta, %u cores\n", r->frames, m->cpu.r[15], r->pc_lo, r->pc_hi, r->nonblack_pct, r->distinct_colors);
    fprintf(f, "FLAGS: %s\n", r->flags);
    fprintf(f, "CPU : indefinidas=%u (1a: pc=%08x op=%08x)  FIQ=%u IRQ=%u\n", m->cpu.stat_undef, m->cpu.undef_pc, m->cpu.undef_op, m->cpu.stat_fiq, m->cpu.stat_irq);
    fprintf(f, "BUS : leitura nao mapeada=%u (1a %08x)  escrita=%u (1a %08x)  fetch=%u (1a %08x)\n", m->stat_bad_rd, m->bad_rd_addr, m->stat_bad_wr, m->bad_wr_addr, m->stat_bad_fetch, m->bad_fetch_addr);
    fprintf(f, "CLIO: regs lidos=%u escritos=%u  IRQ11(DSP->ARM)=%u  ack-forcado-semaforo=%u  XBUS ids=%x sem-dispositivo=%u\n", popc(m->clio.rtouch_r, 512), popc(m->clio.rtouch_w, 512), m->clio.stat_irq11, m->clio.stat_sem_ack, m->clio.xbus_seen, m->clio.stat_xbus_nodev);
    fprintf(f, "CEL : cadeias=%u desenhados=%u (empacotado=%u cru=%u)  poly=%u  nao-suportados=%u  escalados=%u hddx/y=%u LRFORM=%u  PPMP!=id=%u PIXC!=0=%u\n", m->cel.chains, m->cel.drawn, m->cel.n_packed, m->cel.n_unpacked, m->cel.poly, m->cel.unsupported, m->cel.n_scaled, m->cel.n_hdd, m->cel.n_lrform, m->cel.ppmp_nonid, m->cel.pixc_nonzero);
    fprintf(f, "      bpp:"); for (int i = 0; i < 8; i++) if (m->cel.bpp_n[i]) fprintf(f, " %s=%u", bn[i], m->cel.bpp_n[i]);
    fprintf(f, "  flags-vistos=%08x pre0=%08x pre1=%08x\n", m->cel.flags_seen, m->cel.pre0_seen, m->cel.pre1_seen);
    fprintf(f, "VDLP: ctl-bits=%08x outctl-bits=%08x larguras(mask)=%x linhas-hires=%u vdl-vazias=%u\n", m->vdlp.stat_ctl_seen, m->vdlp.stat_ctl_bits, m->vdlp.stat_width_seen, m->vdlp.stat_hires_lines, m->vdlp.stat_empty_vdl);
    fprintf(f, "CD  : lba=%d setores=%u  comandos:", m->cd.lba, (unsigned)m->cd.num_sectors);
    for (int i = 0; i < 256; i++) if (m->cd.stat_cmd[i]) fprintf(f, " %02x=%u", i, m->cd.stat_cmd[i]);
    fprintf(f, "  ignorados=%u leitura-fora=%u\n", m->cd.stat_unk_cmd, m->cd.stat_read_oob);
    /* ---- DIAGNOSTICO (desde o quadro 90; so existe se o phaudit chamou celexec_reset_window) ---- */
    fprintf(f, "\nRASTERIZADOR DE POLIGONOS (desde o quadro 90)  [cls0 = sem hddx/y, cls1 = com hddx/y]\n");
    for (int k = 0; k < 2; k++) {
        const cel_dbg_t *d = &g_cel_dbg[k];
        fprintf(f, "  cls%d: triangulos=%u descartados-enrolamento=%u area-zero=%u longe=%u  CEL-vazio(sc>=W)=%u  pixels: gravados=%u transparentes=%u fora-da-textura=%u\n",
                k, d->tri, d->tri_wind, d->tri_area0, d->tri_far, d->early, d->px, d->px_transp, d->px_oob);
    }
    fprintf(f, "VDLP: quadro exibido curbmp=%08x prevbmp=%08x curvdl=%08x\n", m->vdlp.curbmp, m->vdlp.prevbmp, m->vdlp.curvdl);
    fprintf(f, "GRUPOS POR CONTEXTO DE DESTINO (caminho 1=blit11 2=poly; hdd = tem hddx/hddy; clip = largura x altura da janela de recorte; cadeias = 1a..ultima):\n");
    {
        unsigned used[48] = {0};
        for (int rank = 0; rank < 48; rank++) {
            int best = -1; unsigned bn_ = 0;
            for (int q = 0; q < 48; q++) if (!used[q] && m->cel.grp[q].n > bn_) { bn_ = m->cel.grp[q].n; best = q; }
            if (best < 0) break;
            used[best] = 1;
            const cel_grp_t *G = &m->cel.grp[best];
            fprintf(f, "  n=%-6u path=%u hdd=%u clip=%ux%u (reg=%08x) wr=%08x rd=%08x pixc=%08x cadeias=%u..%u", G->n, G->path, G->hdd, (G->clip & 0x3ff) + 1, ((G->clip >> 16) & 0x3ff) + 1, G->clip, G->wrb, G->rdb, G->pixc, G->first, G->last);
            if (G->path == 2) fprintf(f, "  | triangulos=%u enrolamento=%u area0=%u pixels=%u", G->tri, G->wind, G->area0, G->px);
            fprintf(f, "\n");
            for (unsigned k = 0; k < G->ntr; k++) {
                const __typeof__(G->tr[0]) *t = &G->tr[k];
                fprintf(f, "      #%u pos=(%.2f,%.2f) vd=(%.3f,%.3f) hd=(%.3f,%.3f) hdd=(%.4f,%.4f) %ux%u flags=%08x pre0=%08x pre1=%08x px0=%04x pixels=%u\n",
                        k, t->x / 65536.0, t->y / 65536.0, t->vdx / 65536.0, t->vdy / 65536.0, t->hdx / 1048576.0, t->hdy / 1048576.0, t->hddx / 1048576.0, t->hddy / 1048576.0,
                        t->w, t->h, t->flags, t->pre0, t->pre1, t->px0, t->dpx);
            }
        }
        fprintf(f, "  (CELs em grupos nao listados: %u)\n", m->cel.grp_other);
    }
    fprintf(f, "HISTOGRAMA de formatos (n, caminho, hdd, WxH, flags pre0 pre1 ppmp pixc clip wr_base rd_base):\n");
    {
        unsigned used[160] = {0};
        for (int rank = 0; rank < 40; rank++) {
            int best = -1; unsigned bn_ = 0;
            for (int q = 0; q < 160; q++) if (!used[q] && m->cel.hist[q].n > bn_) { bn_ = m->cel.hist[q].n; best = q; }
            if (best < 0) break;
            used[best] = 1;
            const __typeof__(m->cel.hist[0]) *h = &m->cel.hist[best];
            fprintf(f, "  n=%-6u path=%u hdd=%u %ux%u  flags=%08x pre0=%08x pre1=%08x ppmp=%08x pixc=%08x clip=%08x wr=%08x rd=%08x\n",
                    h->n, h->path, h->hdd, h->w, h->h, h->flags, h->pre0, h->pre1, h->ppmp, h->pixc, h->clip, h->wrb, h->rdb);
        }
        fprintf(f, "  (formatos nao listados: %u CELs)\n", m->cel.hist_other);
    }
}

void ph_audit_csv_header(FILE *f) {
    fprintf(f, "imagem,frames,flags,pc_final,tela_nao_preta_pct,cores,undef,bad_fetch,bad_read,bad_write,cel_desenhados,cel_cru,cel_empacotado,cel_poly,cel_nao_suportado,cel_lrform,xbus_ids,cd_cmd_ignorados,cd_leitura_fora,video_hires_linhas,irq11,ack_semaforo,captura\n");
}
void ph_audit_csv_row(const ph_machine_t *m, const ph_audit_run_t *r, const char *name, const char *shot, FILE *f) {
    fprintf(f, "\"%s\",%u,\"%s\",%08x,%.1f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%x,%u,%u,%u,%u,%u,\"%s\"\n", name, r->frames, r->flags, m->cpu.r[15], r->nonblack_pct, r->distinct_colors,
            m->cpu.stat_undef, m->stat_bad_fetch, m->stat_bad_rd, m->stat_bad_wr, m->cel.drawn, m->cel.n_unpacked, m->cel.n_packed, m->cel.poly, m->cel.unsupported, m->cel.n_lrform,
            m->clio.xbus_seen, m->cd.stat_unk_cmd, m->cd.stat_read_oob, m->vdlp.stat_hires_lines, m->clio.stat_irq11, m->clio.stat_sem_ack, shot ? shot : "");
}
