/* celexec.c - ver celexec.h. Fonte: FUN_00488700 (laco e avanco de xpos/ypos), FUN_004859b0 (empacotado), FUN_00480290 (caminho 1). */
#include "celexec.h"
#include <stdlib.h>
#include <string.h>

void celexec_free(celexec_t *s) { free(s->dec); free(s->tex); s->dec = s->tex = NULL; }
void celexec_reset_window(celexec_t *s) { memset(s->hist, 0, sizeof s->hist); s->hist_other = 0; memset(s->grp, 0, sizeof s->grp); s->grp_other = 0; memset(g_cel_dbg, 0, sizeof g_cel_dbg); }

static void load(celexec_t *s, const madam_t *m) {
    cel_ccb_t *c = &s->ccb;
    c->pc = m->dma_arr[0]; c->next = m->dma_arr[1]; c->plutp = m->dma_arr[2]; c->src = m->dma_arr[3];
    c->ppmp = m->cel_w[8];
    c->xpos = (int32_t)m->cel_w[16]; c->ypos = (int32_t)m->cel_w[17];
    c->vdx = (int32_t)m->cel_w[18];  c->vdy = (int32_t)m->cel_w[19];
    c->hdx = (int32_t)m->cel_w[20];  c->hdy = (int32_t)m->cel_w[21];
    c->hddx = (int32_t)m->cel_w[22]; c->hddy = (int32_t)m->cel_w[23];
    for (int i = 0; i < 32; i++) { c->plut32[i] = m->cel_w[0x20 + i]; c->plut16[i] = (uint16_t)c->plut32[i]; }
}
static void store(const celexec_t *s, madam_t *m) {
    const cel_ccb_t *c = &s->ccb;
    m->dma_arr[0] = c->pc; m->dma_arr[1] = c->next; m->dma_arr[2] = c->plutp; m->dma_arr[3] = c->src;
    m->cel_w[8] = c->ppmp;
    m->cel_w[16] = (uint32_t)c->xpos; m->cel_w[17] = (uint32_t)c->ypos;
    m->cel_w[18] = (uint32_t)c->vdx;  m->cel_w[19] = (uint32_t)c->vdy;
    m->cel_w[20] = (uint32_t)c->hdx;  m->cel_w[21] = (uint32_t)c->hdy;
    m->cel_w[22] = (uint32_t)c->hddx; m->cel_w[23] = (uint32_t)c->hddy;
    for (int i = 0; i < 32; i++) m->cel_w[0x20 + i] = c->plut32[i];
}

uint32_t celexec_run(celexec_t *s, madam_t *m, uint32_t *ram, uint8_t async) {
    if (async) return 0;
    cel_ccb_t *c = &s->ccb;
    load(s, m); s->chains++;
    for (;;) {
        int r = cel_ccb_read(c, ram);
        if (r < 0) break;
        cel_geom_t g = cel_geometry(c);
        int h = g.height;
        if (r == 1) {
            s->bpp_n[c->pre0 & 7]++; if (c->flags & CCB_PACKED) s->n_packed++; else s->n_unpacked++;
            if (c->pre1 & 0x800) s->n_lrform++;
            if (c->hddx || c->hddy) s->n_hdd++;
            if (!cel_is_unit_blit(c)) s->n_scaled++;
            s->flags_seen |= c->flags; s->pre0_seen |= c->pre0; s->pre1_seen |= c->pre1;
            if ((c->ppmp & 0xffffu) != 0x1f00u || (c->ppmp >> 16) != 0x1f00u) s->ppmp_nonid++;
            if (m->cel_w[4]) s->pixc_nonzero++;
            cel_pix_t px; cel_raster_t ras;
            cel_pix_setup(&px, c, m->cel_w[4]);
            cel_raster_setup(&ras, c, m->cel_w[13], m->cel_w[14], m->cel_w[15], m->cel_w21c, m->cel_w220);
            cel_tex_t tex;
            if (!(c->flags & CCB_PACKED) && g.bpp < 16 && !(c->pre1 & 0x800)) {
                /* CEL nao empacotado com indice de PLUT (1..8 bpp): ler a RAM direto como 16 bpp (caminho cru) gera listras/ruido */
                if (!s->tex) s->tex = malloc((size_t)1024 * 0x800 * 4);
                if (s->tex && g.width <= 0x800 && g.height <= 1024 && cel_decode_unpacked(c, ram, s->tex)) {
                    tex.width = g.width; tex.height = g.height; tex.converted = 0; tex.data = s->tex;
                    ras.tex = &tex;
                }
            } else if (c->flags & CCB_PACKED) {            /* FUN_004859b0: decodifica uma vez e compacta (passo = largura) */
                if (!s->dec) s->dec = malloc((size_t)1024 * 0x800 * 4);
                if (!s->tex) s->tex = malloc((size_t)1024 * 0x800 * 4);
                int rows = 0;
                if (s->dec && s->tex) {
                    int w = cel_decode_packed(c, ram, s->dec, &rows);
                    for (int y = 0; y < rows; y++) memcpy(s->tex + (size_t)y * w, s->dec + (size_t)y * 0x800, (size_t)w * 4);
                    tex.width = w; tex.height = rows; tex.converted = 0; tex.data = s->tex;
                    ras.width = w; ras.height = rows; ras.tex = &tex; h = rows;
                }
            }
            uint32_t path = 1;
            const unsigned cls = (c->hddx || c->hddy) ? 1u : 0u;
            g_cel_dbg_cls = (int)cls;
            const cel_dbg_t d0 = g_cel_dbg[cls];
            if (cel_blit11(c, &px, &ras, ram)) { s->drawn++; }
            else if (cel_blit_poly(c, &px, &ras, ram)) { s->drawn++; s->poly++; path = 2; }
            else { s->unsupported++; path = 0; }
            {
                const cel_dbg_t *d1 = &g_cel_dbg[cls]; unsigned gq;
                for (gq = 0; gq < 48 && s->grp[gq].n && !(s->grp[gq].clip == m->cel_w[13] && s->grp[gq].wrb == m->cel_w[15] && s->grp[gq].rdb == m->cel_w[14] &&
                       s->grp[gq].pixc == m->cel_w[4] && s->grp[gq].path == path && s->grp[gq].hdd == cls); gq++) {}
                if (gq == 48) s->grp_other++;
                else {
                    cel_grp_t *G = &s->grp[gq];
                    if (G->n == 0) { G->clip = m->cel_w[13]; G->wrb = m->cel_w[15]; G->rdb = m->cel_w[14]; G->pixc = m->cel_w[4]; G->path = path; G->hdd = cls; G->first = s->chains; }
                    G->n++; G->last = s->chains;
                    G->px += d1->px - d0.px; G->tri += d1->tri - d0.tri; G->wind += d1->tri_wind - d0.tri_wind; G->area0 += d1->tri_area0 - d0.tri_area0;
                    if (G->ntr < 3) {
                        unsigned k = G->ntr++;
                        G->tr[k].x = c->xpos; G->tr[k].y = c->ypos; G->tr[k].vdx = c->vdx; G->tr[k].vdy = c->vdy;
                        G->tr[k].hdx = c->hdx; G->tr[k].hdy = c->hdy; G->tr[k].hddx = c->hddx; G->tr[k].hddy = c->hddy;
                        G->tr[k].w = (uint32_t)ras.width; G->tr[k].h = (uint32_t)ras.height; G->tr[k].flags = c->flags; G->tr[k].pre0 = c->pre0; G->tr[k].pre1 = c->pre1;
                        G->tr[k].px0 = cel_fb_read16(ram, c->src); G->tr[k].dpx = d1->px - d0.px;
                    }
                }
            }
            {
                unsigned q; const uint32_t hw = (uint32_t)ras.width, hh = (uint32_t)ras.height, hd = cls;
                for (q = 0; q < 160 && s->hist[q].n && !(s->hist[q].flags == c->flags && s->hist[q].pre0 == c->pre0 && s->hist[q].pre1 == c->pre1 &&
                       s->hist[q].ppmp == c->ppmp && s->hist[q].pixc == m->cel_w[4] && s->hist[q].path == path && s->hist[q].clip == m->cel_w[13] &&
                       s->hist[q].wrb == m->cel_w[15] && s->hist[q].rdb == m->cel_w[14] && s->hist[q].hdd == hd && s->hist[q].w == hw && s->hist[q].h == hh); q++) {}
                if (q == 160) s->hist_other++;
                else { s->hist[q].flags = c->flags; s->hist[q].pre0 = c->pre0; s->hist[q].pre1 = c->pre1; s->hist[q].ppmp = c->ppmp;
                       s->hist[q].pixc = m->cel_w[4]; s->hist[q].path = path; s->hist[q].n++;
                       s->hist[q].clip = m->cel_w[13]; s->hist[q].wrb = m->cel_w[15]; s->hist[q].rdb = m->cel_w[14];
                       s->hist[q].wrs = m->cel_w220; s->hist[q].rds = m->cel_w21c; s->hist[q].src = (c->flags & CCB_PACKED) ? 0 : c->src;
                       s->hist[q].hdd = hd; s->hist[q].w = hw; s->hist[q].h = hh; }
            }
        }
        if (r == 1) s->last_h = h;
        c->xpos += c->vdx * s->last_h; c->ypos += c->vdy * s->last_h;
        if (c->next == 0) break;
    }
    store(s, m);
    return 0;
}
