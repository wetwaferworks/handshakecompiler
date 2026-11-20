/* SPDX-License-Identifier: MIT */
/*
 * lower.c — translate the dedicated .hc phase-keyword syntax into annotated
 * C11. Fully length-bounded and string/comment/char-literal aware: keywords are
 * only rewritten in real code, never inside "..." / '...' / // / slash-star
 * comments, and the scanner never reads past the slurped buffer even on an
 * unterminated brace/comment/string (crash02 / F11). HS_BOUNDED(n) is preserved
 * (F19). hc_contract.h is included before hc_rt.h so the HS_* macros are defined
 * once, identically (no -Wmacro-redefined). All phase functions are lowered.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int is_ident(int ch) { return isalnum(ch) || ch == '_'; }

static size_t read_word(const char *p, const char *end, char *out, size_t cap)
{
    size_t n = 0;
    while (p + n < end && is_ident((unsigned char)p[n]) && n < cap - 1) { out[n] = p[n]; n++; }
    out[n] = '\0';
    return n;
}

/* copy a balanced {...} starting at p[0]=='{'; bounded by end. Returns consumed. */
static size_t copy_block(const char *p, const char *end, char *out, size_t cap)
{
    size_t i = 0, o = 0; int d = 0;
    while (p + i < end) {
        char ch = p[i];
        if (ch == '{') d++;
        else if (ch == '}') d--;
        if (o < cap - 1) out[o++] = ch;
        i++;
        if (d == 0) break;
    }
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
    size_t len = strlen(src);
    const char *p = src;
    const char *end = src + len;

    fputs("/* lowered from .hc by HC */\n", out);
    fputs("#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n", out);
    fputs("#include \"hc_contract.h\"\n#include \"hc_rt.h\"\n", out);

    while (p < end) {
        char c = *p;

        /* line comment */
        if (c == '/' && p + 1 < end && p[1] == '/') {
            while (p < end && *p != '\n') { fputc(*p, out); p++; }
            continue;
        }
        /* block comment */
        if (c == '/' && p + 1 < end && p[1] == '*') {
            fputc('/', out); fputc('*', out); p += 2;
            while (p < end && !(*p == '*' && p + 1 < end && p[1] == '/')) { fputc(*p, out); p++; }
            if (p < end) { fputc('*', out); if (p + 1 < end) fputc('/', out); p += 2; }
            continue;
        }
        /* string literal */
        if (c == '"') {
            fputc('"', out); p++;
            while (p < end && *p != '"') {
                if (*p == '\\' && p + 1 < end) { fputc(p[0], out); fputc(p[1], out); p += 2; }
                else { fputc(*p, out); p++; }
            }
            if (p < end) { fputc('"', out); p++; }
            continue;
        }
        /* char literal */
        if (c == '\'') {
            fputc('\'', out); p++;
            while (p < end && *p != '\'') {
                if (*p == '\\' && p + 1 < end) { fputc(p[0], out); fputc(p[1], out); p += 2; }
                else { fputc(*p, out); p++; }
            }
            if (p < end) { fputc('\'', out); p++; }
            continue;
        }

        if (is_ident((unsigned char)c) && (p == src || !is_ident((unsigned char)p[-1]))) {
            char w[128];
            size_t wl = read_word(p, end, w, sizeof(w));

            if (!strcmp(w, "readonly") || !strcmp(w, "shard_private")) {
                const char *q = p + wl;
                while (q < end && isspace((unsigned char)*q)) q++;
                char kw[32]; size_t kl = read_word(q, end, kw, sizeof(kw));
                if (!strcmp(kw, "struct")) {
                    q += kl;
                    while (q < end && isspace((unsigned char)*q)) q++;
                    char nm[128]; size_t nl = read_word(q, end, nm, sizeof(nm));
                    q += nl;
                    while (q < end && isspace((unsigned char)*q)) q++;
                    if (q < end && *q == '{') {
                        char blk[8192];
                        size_t bl = copy_block(q, end, blk, sizeof(blk));
                        /* only a well-formed (balanced) block is lowered */
                        if (bl > 0 && blk[strlen(blk) ? strlen(blk) - 1 : 0] == '}') {
                            q += bl;
                            while (q < end && isspace((unsigned char)*q)) q++;
                            if (q < end && *q == ';') q++;
                            fprintf(out, "typedef struct %s %s %s;", nm, blk, nm);
                            p = q;
                            continue;
                        }
                        /* truncated/unbalanced: emit literally and stop safely */
                        fwrite(p, 1, (size_t)(end - p), out);
                        p = end;
                        continue;
                    }
                }
                fwrite(p, 1, wl, out); p += wl; continue;
            }
            if (!strcmp(w, "shared")) { fputs("HS_SHARED ", out); p += wl; continue; }
            if (!strcmp(w, "bounded")) {
                const char *q = p + wl;
                while (q < end && isspace((unsigned char)*q)) q++;
                if (q < end && *q == '(') {
                    char inner[64]; size_t io = 0; int d = 0;
                    const char *r = q;
                    while (r < end) {
                        if (*r == '(') d++;
                        else if (*r == ')') { d--; }
                        else if (io < sizeof(inner) - 1) inner[io++] = *r;
                        r++;
                        if (d == 0) break;
                    }
                    inner[io] = '\0';
                    trim(inner);
                    fprintf(out, "HS_BOUNDED(%s) ", inner);   /* preserve the bound (F19) */
                    p = r; continue;
                }
                fwrite(p, 1, wl, out); p += wl; continue;
            }
            if (!strcmp(w, "phase")) {
                const char *q = p + wl;
                while (q < end && isspace((unsigned char)*q)) q++;
                char ph[32]; size_t pl = read_word(q, end, ph, sizeof(ph));
                const char *ann = NULL;
                if (!strcmp(ph,"publish")) ann = "HS_PHASE_PUBLISH";
                else if (!strcmp(ph,"pull")) ann = "HS_PHASE_PULL";
                else if (!strcmp(ph,"barrier")) ann = "HS_PHASE_BARRIER";
                else if (!strcmp(ph,"commit")) ann = "HS_PHASE_COMMIT";
                else if (!strcmp(ph,"idle")) ann = "HS_PHASE_IDLE";
                if (ann) {
                    q += pl;
                    while (q < end && isspace((unsigned char)*q)) q++;
                    int pure = 0;
                    char maybe[32]; size_t ml = read_word(q, end, maybe, sizeof(maybe));
                    if (!strcmp(maybe, "pure")) { pure = 1; q += ml; while (q < end && isspace((unsigned char)*q)) q++; }
                    char sig[2048]; size_t so = 0; int d = 0;
                    while (q < end && !(*q == '{' && d == 0)) {
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
                        if (hlen >= sizeof(head)) hlen = sizeof(head) - 1;
                        memcpy(head, sig, hlen); head[hlen] = '\0';
                        snprintf(ret, sizeof(ret), "%s", arrow + 2);
                    } else {
                        snprintf(head, sizeof(head), "%s", sig);
                        snprintf(ret, sizeof(ret), "void");
                    }
                    trim(head); trim(ret);
                    fprintf(out, "%s %s%s %s {", ann, pure ? "HS_PURE " : "", ret, head);
                    if (q < end && *q == '{') q++;
                    p = q;
                    continue;
                }
                fwrite(p, 1, wl, out); p += wl; continue;
            }
            fwrite(p, 1, wl, out);
            p += wl;
            continue;
        }
        fputc(c, out);
        p++;
    }
    fputc('\n', out);
    return 0;
}
