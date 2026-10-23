/* ph_audit.h - coleta e classificacao dos contadores de auditoria do nucleo (ver AUDITORIA_PHOENIX_LIBRETRO.md).
 * Os contadores vivem dentro de cada modulo (arm60, clio, cd, vdlp, celexec, machine); aqui so se le e se traduz em FLAGS. */
#ifndef PH_AUDIT_H
#define PH_AUDIT_H
#include <stdio.h>
#include "machine.h"

typedef struct {
    unsigned frames;              /* quadros fechados pelo VDLP */
    double   nonblack_pct;        /* % de pixels nao pretos no ultimo quadro (0 = tela preta) */
    unsigned distinct_colors;     /* cores distintas (satura em 4096) no ultimo quadro */
    unsigned last_change_frame;   /* ultimo quadro em que a imagem mudou */
    uint32_t pc_lo, pc_hi;        /* faixa de PC nos ultimos ~5 s (detecta laco) */
    char     flags[512];          /* lista de FLAGS separadas por espaco */
} ph_audit_run_t;

void ph_audit_reset_boot(ph_machine_t *m);                            /* chamar ~90 quadros depois do boot */
void ph_audit_finish(const ph_machine_t *m, ph_audit_run_t *r);       /* calcula nonblack/cores e monta r->flags */
void ph_audit_report(const ph_machine_t *m, const ph_audit_run_t *r, FILE *f);
void ph_audit_csv_header(FILE *f);
void ph_audit_csv_row(const ph_machine_t *m, const ph_audit_run_t *r, const char *name, const char *shot, FILE *f);
#endif
