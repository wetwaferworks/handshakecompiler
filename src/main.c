/* SPDX-License-Identifier: MIT */
#include "hc_lexer.h"
#include "hc_ast.h"
#include "hc_rules.h"
#include "hc_backend.h"
#include "hc_elf_audit.h"
#include "hc_preproc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

int hc_lower_hc(const char *src, FILE *out);

#define HC_VERSION "0.1.0"
#define MAXARG 256


static int ends_with(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}


static void usage(void)
{
    printf("hc — Handshake Compiler v%s\n", HC_VERSION);
    printf("usage: hc [options] -c <file.c|file.hc> -o <out.o>\n");
    printf("  --check-only         verify only, do not emit\n");
    printf("  --emit-c <path>      write lowered C11 for inspection\n");
    printf("  --target=<triple>    e.g. aarch64-linux-gnu\n");
    printf("  --cross=<prefix>     cross compiler prefix, e.g. aarch64-linux-gnu-\n");
    printf("  --profile=component|runtime  (default component)\n");
    printf("  --allow-runtime      alias for --profile=runtime\n");
    printf("  --audit-elf <file>   run ELF64 zero-lock symbol audit and exit\n");
    printf("  --no-audit           skip post-compile ELF audit\n");
    printf("  --dump-rules         print the HC-E001..HC-E026 rule registry\n");
    printf("  --version, --help\n");
}

int main(int argc, char **argv)
{
    char *incs[MAXARG]; size_t ninc = 0;
    char *defs[MAXARG]; size_t ndef = 0;
    char *pass[MAXARG]; size_t npass = 0;
    const char *src = NULL, *out = NULL, *emitc = NULL, *auditfile = NULL;
    const char *target = NULL, *cross = NULL;
    int checkonly = 0, noaudit = 0;
    hc_profile profile = HC_PROFILE_COMPONENT;
    int i;

    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        if (!strcmp(a, "--version")) { printf("hc %s\n", HC_VERSION); return 0; }
        if (!strcmp(a, "--dump-rules")) { hc_dump_rules(); return 0; }
        if (!strcmp(a, "--check-only")) { checkonly = 1; continue; }
        if (!strcmp(a, "--no-audit")) { noaudit = 1; continue; }
        if (!strcmp(a, "--allow-runtime")) { profile = HC_PROFILE_RUNTIME; continue; }
        if (!strcmp(a, "--profile=runtime")) { profile = HC_PROFILE_RUNTIME; continue; }
        if (!strcmp(a, "--profile=component")) { profile = HC_PROFILE_COMPONENT; continue; }
        if (!strncmp(a, "--target=", 9)) { target = a + 9; continue; }
        if (!strncmp(a, "--cross=", 8)) { cross = a + 8; continue; }
        if (!strcmp(a, "--emit-c")) { if (i+1<argc) emitc = argv[++i]; continue; }
        if (!strcmp(a, "--audit-elf")) { if (i+1<argc) auditfile = argv[++i]; continue; }
        if (!strcmp(a, "-c")) { continue; }
        if (!strcmp(a, "-o")) { if (i+1<argc) out = argv[++i]; continue; }
        if (!strncmp(a, "-I", 2)) {
            if (a[2]) { incs[ninc++] = a; pass[npass++] = a; }
            else if (i+1<argc) { static char buf[MAXARG][512]; snprintf(buf[ninc],512,"-I%s",argv[++i]);
                incs[ninc]=buf[ninc]; pass[npass++]=buf[ninc]; ninc++; }
            continue;
        }
        if (!strncmp(a, "-D", 2)) { defs[ndef++] = a; pass[npass++] = a; continue; }
        if (!strncmp(a, "-U", 2)) { defs[ndef++] = a; pass[npass++] = a; continue; }
        if (a[0] == '-') { pass[npass++] = a; continue; } /* -O3 -static -pthread -f* -g -m* */
        src = a;
    }

    if (auditfile) {
        int r = hc_elf_audit(auditfile, profile);
        if (r > 0) { fprintf(stderr, "hc: audit failed (%d banned symbol(s))\n", r); return 1; }
        printf("hc: ELF64 audit PASS: %s (0 banned lock symbols)\n", auditfile);
        return 0;
    }

    if (!src) { fprintf(stderr, "hc: no input file\n"); usage(); return 2; }

    /* add our own include/runtime dirs so hc_rt.h / hc_contract.h resolve */
    static char rti[512], inci[512];
    snprintf(rti, sizeof(rti), "-I%s", "/workspace/hc/runtime");
    snprintf(inci, sizeof(inci), "-I%s", "/workspace/hc/include");
    incs[ninc++] = rti; pass[npass++] = rti;
    incs[ninc++] = inci; pass[npass++] = inci;

    /* scratch dir */
    const char *scratch = "/workspace/scratch/hc/tmp";
    char mk[600]; snprintf(mk, sizeof(mk), "mkdir -p %s", scratch); if (system(mk)) {}

    /* read raw source for raw-pass checks */
    size_t rawlen;
    char *raw = hc_slurp(src, &rawlen);
    if (!raw) { fprintf(stderr, "hc: cannot read %s\n", src); return 2; }

    /* if .hc: lower to a temp .c */
    char lowered[600];
    const char *to_compile = src;
    int is_hc = ends_with(src, ".hc");
    if (is_hc) {
        snprintf(lowered, sizeof(lowered), "%s/hc_lower_%d.c", scratch, (int)getpid());
        FILE *lf = fopen(lowered, "w");
        if (!lf) { fprintf(stderr, "hc: cannot write lowered file\n"); return 2; }
        hc_lower_hc(raw, lf);
        fclose(lf);
        to_compile = lowered;
    }

    if (emitc) {
        if (is_hc) {
            char cp[1400]; snprintf(cp, sizeof(cp), "cp %s %s", lowered, emitc); if (system(cp)) {}
        } else {
            char cp[1400]; snprintf(cp, sizeof(cp), "cp %s %s", src, emitc); if (system(cp)) {}
        }
    }

    /* preprocess for analysis */
    char ppi[600]; snprintf(ppi, sizeof(ppi), "%s/hc_pp_%d.i", scratch, (int)getpid());
    int pr = hc_preprocess("gcc", to_compile, incs, ninc, defs, ndef, ppi);
    if (pr != 0) {
        fprintf(stderr, "hc: preprocessing failed for %s\n", src);
        free(raw);
        return 1;
    }

    size_t pplen;
    char *ppbuf = hc_slurp(ppi, &pplen);
    if (!ppbuf) { fprintf(stderr, "hc: cannot read preprocessed TU\n"); free(raw); return 1; }

    hc_token_list tl;
    hc_lex_preprocessed(ppbuf, pplen, &tl);
    hc_unit u;
    hc_parse_unit(&tl, &u);

    int diags = hc_verify(&u, &tl, raw, src, profile);

    hc_unit_free(&u);
    hc_tokens_free(&tl);
    free(ppbuf);

    if (diags > 0) {
        fprintf(stderr, "hc: %d fair-play/phase-isolation violation(s) in %s — refusing to emit.\n", diags, src);
        free(raw);
        return 1;
    }

    if (checkonly) {
        printf("hc: %s OK (profile=%s, 0 violations)\n", src,
               profile == HC_PROFILE_RUNTIME ? "runtime" : "component");
        free(raw);
        return 0;
    }

    if (!out) { fprintf(stderr, "hc: no output (-o) specified\n"); free(raw); return 2; }

    /* codegen via backend (no __HC_COMPILER__ => HS_* are empty) */
    const hc_backend_ops *be = hc_backend_cc();
    const char *cc = hc_backend_driver(target, cross);
    int rc = be->compile(cc, to_compile, out, pass, npass);
    if (rc != 0) {
        fprintf(stderr, "hc: backend %s failed (rc=%d) on %s\n", cc, rc, src);
        free(raw);
        return rc ? rc : 1;
    }

    if (!noaudit) {
        int r = hc_elf_audit(out, profile);
        if (r > 0) {
            fprintf(stderr, "hc: post-compile ELF64 audit FAILED on %s (%d banned symbol(s))\n", out, r);
            free(raw);
            return 1;
        }
    }

    free(raw);
    return 0;
}
