/* SPDX-License-Identifier: MIT */
/*
 * lower.c — translate the dedicated .hc phase-keyword syntax into annotated
 * C11 that shares HC's HS_* contract and the hc_rt.h runtime. Deterministic,
 * zero runtime overhead; phase keywords become HS_PHASE_* annotations so the
 * same verifier runs over .c and .hc uniformly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int is_ident(int ch) { return isalnum(ch) || ch == '_'; }

static size_t read_word(const char *p, char *out, size_t cap)
{
    size_t n = 0;
    while (p[n] && is_ident((unsigned char)p[n]) && n < cap - 1) { out[n] = p[n]; n++; }
    out[n] = '\0';
    return n;
}

/* copy balanced {...} starting at p[0]=='{'; returns consumed length */
static size_t copy_block(const char *p, char *out, size_t cap)
{
    size_t i = 0, o = 0; int d = 0;
    do {
        if (p[i] == '{') d++;
        else if (p[i] == '}') d--;
        if (o < cap - 1) out[o++] = p[i];
        i++;
    } while (p[i - 1] && d > 0);
    out[o] = '\0';
    return i;
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    size_t i = 0; while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}

int hc_lower_hc(const char *src, FILE *out)
{
    const char *p = src;
    fputs("/* lowered from .hc by HC */\n", out);
    fputs("#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n", out);
    fputs("#include \"hc_rt.h\"\n#include \"hc_contract.h\"\n", out);

    while (*p) {
        if (is_ident((unsigned char)*p) &&
            (p == src || !is_ident((unsigned char)p[-1]))) {
            char w[128];
            size_t wl = read_word(p, w, sizeof(w));

            if (!strcmp(w, "readonly") || !strcmp(w, "shard_private")) {
                const char *q = p + wl;
                while (*q && isspace((unsigned char)*q)) q++;
                char kw[32]; size_t kl = read_word(q, kw, sizeof(kw));
                if (!strcmp(kw, "struct")) {
                    q += kl;
                    while (*q && isspace((unsigned char)*q)) q++;
                    char nm[128]; size_t nl = read_word(q, nm, sizeof(nm));
                    q += nl;
                    while (*q && isspace((unsigned char)*q)) q++;
                    if (*q == '{') {
                        char blk[4096];
                        size_t bl = copy_block(q, blk, sizeof(blk));
                        q += bl;
                        while (*q && isspace((unsigned char)*q)) q++;
                        if (*q == ';') q++;
                        fprintf(out, "typedef struct %s %s %s;", nm, blk, nm);
                        p = q;
                        continue;
                    }
                }
                /* fall through: emit the word literally */
                fwrite(p, 1, wl, out); p += wl; continue;
            }
            if (!strcmp(w, "shared")) { fputs("HS_SHARED ", out); p += wl; continue; }
            if (!strcmp(w, "bounded")) {
                const char *q = p + wl;
                while (*q && isspace((unsigned char)*q)) q++;
                if (*q == '(') { int d = 0; do { if (*q=='(') d++; else if (*q==')') d--; q++; } while (*q && d>0); }
                p = q; continue;
            }
            if (!strcmp(w, "phase")) {
                const char *q = p + wl;
                while (*q && isspace((unsigned char)*q)) q++;
                char ph[32]; size_t pl = read_word(q, ph, sizeof(ph));
                const char *ann = NULL;
                if (!strcmp(ph,"publish")) ann = "HS_PHASE_PUBLISH";
                else if (!strcmp(ph,"pull")) ann = "HS_PHASE_PULL";
                else if (!strcmp(ph,"barrier")) ann = "HS_PHASE_BARRIER";
                else if (!strcmp(ph,"commit")) ann = "HS_PHASE_COMMIT";
                else if (!strcmp(ph,"idle")) ann = "HS_PHASE_IDLE";
                if (ann) {
                    q += pl;
                    while (*q && isspace((unsigned char)*q)) q++;
                    int pure = 0;
                    char maybe[32]; size_t ml = read_word(q, maybe, sizeof(maybe));
                    if (!strcmp(maybe, "pure")) { pure = 1; q += ml; while (*q && isspace((unsigned char)*q)) q++; }
                    /* capture signature up to matching '{' at paren depth 0 */
                    char sig[2048]; size_t so = 0; int d = 0;
                    while (*q && !(*q == '{' && d == 0)) {
                        if (*q == '(') d++;
                        else if (*q == ')') d--;
                        if (so < sizeof(sig) - 1) sig[so++] = *q;
                        q++;
                    }
                    sig[so] = '\0';
                    char head[2048], ret[256];
                    char *arrow = strstr(sig, "->");
                    if (arrow) {
                        size_t hlen = (size_t)(arrow - sig);
                        memcpy(head, sig, hlen); head[hlen] = '\0';
                        snprintf(ret, sizeof(ret), "%s", arrow + 2);
                    } else {
                        snprintf(head, sizeof(head), "%s", sig);
                        snprintf(ret, sizeof(ret), "void");
                    }
                    trim(head); trim(ret);
                    fprintf(out, "%s %s%s %s {", ann, pure ? "HS_PURE " : "", ret, head);
                    if (*q == '{') q++;
                    p = q;
                    continue;
                }
                fwrite(p, 1, wl, out); p += wl; continue;
            }
            /* ordinary identifier */
            fwrite(p, 1, wl, out);
            p += wl;
            continue;
        }
        fputc(*p, out);
        p++;
    }
    fputc('\n', out);
    return 0;
}
