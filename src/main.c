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
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

int hc_lower_hc(const char *src, FILE *out);

#define HC_VERSION "0.1.0"
#define MAXARG 256

static int ends_with(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

/* pure-POSIX recursive mkdir (no shell) */
static int mkdir_p(const char *path)
{
    char tmp[1024];
    size_t len = strlen(path), i;
    if (len == 0 || len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (i = 1; tmp[i]; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            tmp[i] = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* pure C file copy (no shell) */
static int hc_copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    FILE *out;
    char buf[65536];
    size_t n;
    int rc = 0;
    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    }
    if (ferror(in)) rc = -1;
    fclose(in);
    if (fclose(out) != 0) rc = -1;
    return rc;
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
    int i, ret = 0;
    const char *cc = "gcc";

    /* temp files (unlinked on every exit path) */
    char lowered[1024]; int have_lowered = 0;
    char ppi[1024];     int have_ppi = 0;
    char *raw = NULL, *ppbuf = NULL;

    #define BOUNDS(n) do { if ((n) >= (size_t)(MAXARG - 4)) { \
        fprintf(stderr, "hc: too many arguments (limit %d)\n", MAXARG - 4); return 2; } } while (0)

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
            BOUNDS(ninc); BOUNDS(npass);
            if (a[2]) { incs[ninc++] = a; pass[npass++] = a; }
            else if (i+1<argc) { static char buf[MAXARG][512]; snprintf(buf[ninc],512,"-I%s",argv[++i]);
                incs[ninc]=buf[ninc]; pass[npass++]=buf[ninc]; ninc++; }
            continue;
        }
        if (!strncmp(a, "-D", 2)) { BOUNDS(ndef); BOUNDS(npass); defs[ndef++] = a; pass[npass++] = a; continue; }
        if (!strncmp(a, "-U", 2)) { BOUNDS(ndef); BOUNDS(npass); defs[ndef++] = a; pass[npass++] = a; continue; }
        if (a[0] == '-') { BOUNDS(npass); pass[npass++] = a; continue; } /* -O3 -static -pthread -f* -g -m* */
        src = a;
    }

    /* resolve the target driver up front so --target preprocessing expands
     * the correct arch #ifdefs (e.g. __aarch64__ ldaxr/stlxr, cntvct_el0). */
    cc = hc_backend_driver(target, cross);

    if (auditfile) {
        int r = hc_elf_audit(auditfile, profile);
        if (r != 0) { /* fail closed: -1 (corrupt/unreadable) or >0 (banned) */
            fprintf(stderr, "hc: ELF64 audit FAILED on %s (rc=%d)\n", auditfile, r);
            return 1;
        }
        printf("hc: ELF64 audit PASS: %s (0 banned symbols, profile=%s)\n",
               auditfile, profile == HC_PROFILE_RUNTIME ? "runtime" : "component");
        return 0;
    }

    if (!src) { fprintf(stderr, "hc: no input file\n"); usage(); return 2; }

    /* add our own include/runtime dirs so hc_rt.h / hc_contract.h resolve */
    static char rti[512], inci[512];
    snprintf(rti, sizeof(rti), "-I%s", "/workspace/hc/runtime");
    snprintf(inci, sizeof(inci), "-I%s", "/workspace/hc/include");
    BOUNDS(ninc); BOUNDS(npass); incs[ninc++] = rti; pass[npass++] = rti;
    BOUNDS(ninc); BOUNDS(npass); incs[ninc++] = inci; pass[npass++] = inci;

    /* scratch dir (pure POSIX, no shell) */
    const char *scratch = "/workspace/scratch/hc/tmp";
    if (mkdir_p(scratch) != 0) { fprintf(stderr, "hc: cannot create %s\n", scratch); return 2; }

    /* read raw source for raw-pass checks */
    size_t rawlen;
    raw = hc_slurp(src, &rawlen);
    if (!raw) { fprintf(stderr, "hc: cannot read %s\n", src); ret = 2; goto done; }

    /* if .hc: lower to a unique temp .c */
    const char *to_compile = src;
    int is_hc = ends_with(src, ".hc");
    if (is_hc) {
        int fd;
        FILE *lf;
        snprintf(lowered, sizeof(lowered), "%s/hc_lower_XXXXXX.c", scratch);
        fd = mkstemps(lowered, 2);
        if (fd < 0) { fprintf(stderr, "hc: cannot create temp lowered file\n"); ret = 2; goto done; }
        have_lowered = 1;
        lf = fdopen(fd, "w");
        if (!lf) { close(fd); ret = 2; goto done; }
        hc_lower_hc(raw, lf);
        fclose(lf);
        to_compile = lowered;
    }

    if (emitc) {
        if (hc_copy_file(to_compile, emitc) != 0)
            fprintf(stderr, "hc: warning: could not write --emit-c target %s\n", emitc);
    }

    /* preprocess for analysis (use target cc so arch #ifdefs expand) */
    {
        int fd;
        snprintf(ppi, sizeof(ppi), "%s/hc_pp_XXXXXX.i", scratch);
        fd = mkstemps(ppi, 2);
        if (fd < 0) { fprintf(stderr, "hc: cannot create temp .i file\n"); ret = 1; goto done; }
        close(fd);
        have_ppi = 1;
    }
    {
        int pr = hc_preprocess(cc, to_compile, incs, ninc, defs, ndef, ppi);
        if (pr != 0) { fprintf(stderr, "hc: preprocessing failed for %s\n", src); ret = 1; goto done; }
    }

    size_t pplen;
    ppbuf = hc_slurp(ppi, &pplen);
    if (!ppbuf) { fprintf(stderr, "hc: cannot read preprocessed TU\n"); ret = 1; goto done; }

    hc_token_list tl;
    hc_lex_preprocessed(ppbuf, pplen, &tl);
    hc_unit u;
    hc_parse_unit(&tl, &u);
    int diags = hc_verify(&u, &tl, raw, src, profile);
    hc_unit_free(&u);
    hc_tokens_free(&tl);

    if (diags > 0) {
        fprintf(stderr, "hc: %d fair-play/phase-isolation violation(s) in %s — refusing to emit.\n", diags, src);
        ret = 1; goto done;
    }

    if (checkonly) {
        printf("hc: %s OK (profile=%s, 0 violations)\n", src,
               profile == HC_PROFILE_RUNTIME ? "runtime" : "component");
        ret = 0; goto done;
    }

    if (!out) { fprintf(stderr, "hc: no output (-o) specified\n"); ret = 2; goto done; }

    /* codegen via backend (no __HC_COMPILER__ => HS_* are empty) */
    {
        const hc_backend_ops *be = hc_backend_cc();
        int rc = be->compile(cc, to_compile, out, pass, npass);
        if (rc != 0) { fprintf(stderr, "hc: backend %s failed (rc=%d) on %s\n", cc, rc, src); ret = rc ? rc : 1; goto done; }
    }

    if (!noaudit) {
        int r = hc_elf_audit(out, profile);
        if (r != 0) { /* fail closed, and never leave a poisoned object */
            fprintf(stderr, "hc: post-compile ELF64 audit FAILED on %s (rc=%d) — removing object.\n", out, r);
            unlink(out);
            ret = 1; goto done;
        }
    }

    ret = 0;

done:
    if (have_lowered) unlink(lowered);
    if (have_ppi) unlink(ppi);
    free(raw);
    free(ppbuf);
    return ret;
}
