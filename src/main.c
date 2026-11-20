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
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

int hc_lower_hc(const char *src, FILE *out);

#define HC_VERSION "0.1.1"
#define MAXARG 512

static int ends_with(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

static int hc_copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb"), *out;
    char buf[65536]; size_t n; int rc = 0;
    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    if (ferror(in)) rc = -1;
    fclose(in);
    if (fclose(out) != 0) rc = -1;
    return rc;
}

/* Resolve the repo root (containing runtime/ and include/) from /proc/self/exe. */
static void resolve_root(char *root, size_t cap)
{
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s", exe);

        for (;;) {
            char *slash = strrchr(dir, '/');
            if (!slash) break;
            *slash = '\0';
            if (dir[0] == '\0') break;
            char probe[PATH_MAX * 2];
            snprintf(probe, sizeof(probe), "%s/runtime/hc_rt.h", dir);
            char probe2[PATH_MAX * 2];
            snprintf(probe2, sizeof(probe2), "%s/include/hc_contract.h", dir);
            if (access(probe, R_OK) == 0 && access(probe2, R_OK) == 0) {
                snprintf(root, cap, "%s", dir);
                return;
            }
        }
    }
    snprintf(root, cap, "%s", "/workspace/hc");   /* fallback */
}

/* Best-effort sweep of leftovers in the legacy shared scratch dir. */
static void sweep_legacy_tmp(void)
{
    const char *d = "/workspace/scratch/hc/tmp";
    DIR *dp = opendir(d);
    struct dirent *e;
    if (!dp) return;
    while ((e = readdir(dp)) != NULL) {
        if (strncmp(e->d_name, "hc_lower_", 9) == 0 ||
            strncmp(e->d_name, "hc_pp_", 6) == 0) {
            char path[PATH_MAX * 2];
            snprintf(path, sizeof(path), "%s/%s", d, e->d_name);
            unlink(path);
        }
    }
    closedir(dp);
    rmdir(d);   /* only succeeds if now empty */
}

static int is_dangerous_flag(const char *a)
{
    if (!strcmp(a, "-s") || !strcmp(a, "--strip-all")) return 1;
    if (!strcmp(a, "-Wl,-s") || !strcmp(a, "-Wl,--strip-all")) return 1;
    if (!strncmp(a, "-flto", 5)) return 1;
    if (!strncmp(a, "-Wp,", 4)) return 1;
    if (!strcmp(a, "-include")) return 1;
    if (!strncmp(a, "--include", 9)) return 1;
    if (!strcmp(a, "-imacros")) return 1;
    if (!strncmp(a, "-specs=", 7)) return 1;
    if (a[0] == '-' && a[1] == 'B') return 1;
    if (!strcmp(a, "-wrapper")) return 1;
    if (!strncmp(a, "-fplugin", 8)) return 1;
    if (a[0] == '@') return 1;
    if (!strncmp(a, "--param", 7)) return 1;
    if (!strncmp(a, "-D__HC_COMPILER__", 17)) return 1;
    if (!strncmp(a, "-U__HC_COMPILER__", 17)) return 1;
    return 0;
}

static void usage(void)
{
    printf("hc — Handshake Compiler v%s\n", HC_VERSION);
    printf("usage: hc [options] -c <file.c|file.hc> -o <out.o>\n");
    printf("  --check-only  --emit-c <p>  --target=<t>  --cross=<pfx>\n");
    printf("  --profile=component|runtime  --allow-runtime  --no-audit\n");
    printf("  --audit-elf <file>  --dump-rules  --version  --help\n");
}

int main(int argc, char **argv)
{
    char *incs[MAXARG]; size_t ninc = 0;   /* -I */
    char *defs[MAXARG]; size_t ndef = 0;   /* -D / -U */
    char *cg[MAXARG];   size_t ncg = 0;    /* -O / -std / -m / -g / -f / -W / -pthread / -static */
    char *pp[MAXARG];   size_t npp = 0;    /* preprocess = incs + defs + cg */
    const char *src = NULL, *out = NULL, *emitc = NULL, *auditfile = NULL;
    const char *target = NULL, *cross = NULL;
    int checkonly = 0, noaudit = 0;
    hc_profile profile = HC_PROFILE_COMPONENT;
    int i, ret = 0;
    const char *cc = "gcc";

    char tmpdir[PATH_MAX]; int have_tmp = 0;
    char lowered[PATH_MAX + 32]; int have_lowered = 0;
    char ppi[PATH_MAX + 32];     int have_ppi = 0;
    char ippath[PATH_MAX + 32];  int have_ipp = 0;
    char *raw = NULL, *ppbuf = NULL;
    char rti[PATH_MAX + 16], inci[PATH_MAX + 16], root[PATH_MAX];

    #define BOUNDS(n) do { if ((n) >= (size_t)(MAXARG - 4)) { \
        fprintf(stderr, "hc: too many arguments (limit %d)\n", MAXARG - 4); return 2; } } while (0)

    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        if (!strcmp(a, "--version")) { printf("hc %s\n", HC_VERSION); return 0; }
        if (!strcmp(a, "--dump-rules")) { hc_dump_rules(); return 0; }
        if (!strcmp(a, "--check-only")) { checkonly = 1; continue; }
        if (!strcmp(a, "--no-audit")) { noaudit = 1; continue; }
        if (!strcmp(a, "--allow-runtime") || !strcmp(a, "--profile=runtime")) { profile = HC_PROFILE_RUNTIME; continue; }
        if (!strcmp(a, "--profile=component")) { profile = HC_PROFILE_COMPONENT; continue; }
        if (!strncmp(a, "--target=", 9)) { target = a + 9; continue; }
        if (!strncmp(a, "--cross=", 8)) { cross = a + 8; continue; }
        if (!strcmp(a, "--emit-c")) { if (i+1<argc) emitc = argv[++i]; continue; }
        if (!strcmp(a, "--audit-elf")) { if (i+1<argc) auditfile = argv[++i]; continue; }
        if (!strcmp(a, "-c")) { continue; }
        if (!strcmp(a, "-o")) { if (i+1<argc) out = argv[++i]; continue; }
        if (is_dangerous_flag(a)) {
            fprintf(stderr, "hc: error [HC-E025]: forbidden/unsafe compiler flag '%s' "
                    "(verifier/backend split-brain or audit bypass) (SMUGGLE_MACRO)\n", a);
            return 1;
        }
        if (!strncmp(a, "-I", 2)) {
            BOUNDS(ninc);
            if (a[2]) incs[ninc++] = a;
            else if (i+1<argc) { static char b[MAXARG][PATH_MAX]; snprintf(b[ninc],PATH_MAX,"-I%s",argv[++i]); incs[ninc]=b[ninc]; ninc++; }
            continue;
        }
        if (!strncmp(a, "-D", 2) || !strncmp(a, "-U", 2)) { BOUNDS(ndef); defs[ndef++] = a; continue; }
        if (a[0] == '-') { BOUNDS(ncg); cg[ncg++] = a; continue; }  /* -O3 -std=* -m* -g -f* -W* -static -pthread */
        src = a;
    }

    cc = hc_backend_driver(target, cross);

    if (auditfile) {
        int r = hc_elf_audit(auditfile, profile);
        if (r != 0) { fprintf(stderr, "hc: ELF64 audit FAILED on %s (rc=%d)\n", auditfile, r); return 1; }
        printf("hc: ELF64 audit PASS: %s (0 banned symbols, profile=%s)\n",
               auditfile, profile == HC_PROFILE_RUNTIME ? "runtime" : "component");
        return 0;
    }

    if (!src) { fprintf(stderr, "hc: no input file\n"); usage(); return 2; }

    /* resolve runtime/include relative to the binary (fallback /workspace/hc) */
    resolve_root(root, sizeof(root));
    snprintf(rti, sizeof(rti), "-I%s/runtime", root);
    snprintf(inci, sizeof(inci), "-I%s/include", root);
    BOUNDS(ninc); incs[ninc++] = rti;
    BOUNDS(ninc); incs[ninc++] = inci;

    sweep_legacy_tmp();

    /* private per-invocation temp dir (0700), cleaned on every exit path */
    {
        const char *base = getenv("TMPDIR");
        if (!base || !base[0]) base = "/tmp";
        snprintf(tmpdir, sizeof(tmpdir), "%s/hc.XXXXXX", base);
        if (!mkdtemp(tmpdir)) { fprintf(stderr, "hc: cannot create temp dir\n"); return 2; }
        have_tmp = 1;
    }

    size_t rawlen;
    raw = hc_slurp(src, &rawlen);
    if (!raw) { fprintf(stderr, "hc: cannot read %s\n", src); ret = 2; goto done; }

    const char *to_compile = src;
    if (ends_with(src, ".hc")) {
        FILE *lf;
        snprintf(lowered, sizeof(lowered), "%s/lowered.c", tmpdir);
        lf = fopen(lowered, "w");
        if (!lf) { fprintf(stderr, "hc: cannot create lowered file\n"); ret = 2; goto done; }
        have_lowered = 1;
        hc_lower_hc(raw, lf);
        fclose(lf);
        to_compile = lowered;
    }

    if (emitc && hc_copy_file(to_compile, emitc) != 0)
        fprintf(stderr, "hc: warning: could not write --emit-c target %s\n", emitc);

    /* build the unified preprocess flag list: incs + defs + cg */
    { size_t k;
      for (k = 0; k < ninc && npp < MAXARG-2; k++) pp[npp++] = incs[k];
      for (k = 0; k < ndef && npp < MAXARG-2; k++) pp[npp++] = defs[k];
      for (k = 0; k < ncg  && npp < MAXARG-2; k++) pp[npp++] = cg[k];
    }

    snprintf(ppi, sizeof(ppi), "%s/pp.i", tmpdir); have_ppi = 1;
    if (hc_preprocess(cc, to_compile, pp, npp, ppi) != 0) {
        fprintf(stderr, "hc: preprocessing failed for %s\n", src); ret = 1; goto done;
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
        printf("hc: %s OK (profile=%s, 0 violations)\n", src, profile==HC_PROFILE_RUNTIME?"runtime":"component");
        ret = 0; goto done;
    }
    if (!out) { fprintf(stderr, "hc: no output (-o) specified\n"); ret = 2; goto done; }

    /* compile the EXACT verified TU: strip annotate -> gcc -x cpp-output */
    snprintf(ippath, sizeof(ippath), "%s/pp.stripped.i", tmpdir); have_ipp = 1;
    if (hc_strip_annotations(ppi, ippath) != 0) {
        fprintf(stderr, "hc: failed to strip annotations from verified TU\n"); ret = 1; goto done;
    }
    {
        int rc = hc_backend_compile_i(cc, ippath, out, cg, ncg);
        if (rc != 0) { fprintf(stderr, "hc: backend %s failed (rc=%d) on %s\n", cc, rc, src); ret = rc?rc:1; goto done; }
    }

    if (!noaudit) {
        int r = hc_elf_audit(out, profile);
        if (r != 0) {
            fprintf(stderr, "hc: post-compile ELF64 audit FAILED on %s (rc=%d) — removing object.\n", out, r);
            unlink(out);
            ret = 1; goto done;
        }
    }
    ret = 0;

done:
    if (have_lowered) unlink(lowered);
    if (have_ppi) unlink(ppi);
    if (have_ipp) unlink(ippath);
    if (have_tmp) rmdir(tmpdir);
    free(raw);
    free(ppbuf);
    return ret;
}
