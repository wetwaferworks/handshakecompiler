/* SPDX-License-Identifier: MIT */
#include "hc_lexer.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *xstrndup(const char *s, size_t n)
{
    char *p = (char *)malloc(n + 1);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static void push(hc_token_list *tl, hct_kind k, const char *s, size_t n,
                 int line, int col, int system, char *file)
{
    hc_token *t;
    if (tl->count == tl->cap) {
        size_t nc = tl->cap ? tl->cap * 2 : 256;
        hc_token *nt = (hc_token *)realloc(tl->toks, nc * sizeof(*nt));
        if (nt == NULL) {
            return;
        }
        tl->toks = nt;
        tl->cap = nc;
    }
    t = &tl->toks[tl->count++];
    t->kind = k;
    t->text = xstrndup(s, n);
    t->line = line;
    t->col = col;
    t->system = system;
    t->file = file;
}

/* Interned file-name table so tokens can share a pointer. */
static char *intern_file(char ***tab, size_t *n, size_t *cap, const char *name)
{
    size_t i;
    for (i = 0; i < *n; i++) {
        if (strcmp((*tab)[i], name) == 0) {
            return (*tab)[i];
        }
    }
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 16;
        char **t = (char **)realloc(*tab, nc * sizeof(char *));
        if (t == NULL) {
            return NULL;
        }
        *tab = t;
        *cap = nc;
    }
    (*tab)[*n] = xstrndup(name, strlen(name));
    return (*tab)[(*n)++];
}

static const char *PUNCTS[] = {
    "...", "<<=", ">>=",
    "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "::", "##",
    "+", "-", "*", "/", "%", "&", "|", "^", "~", "!", "<", ">", "=",
    "(", ")", "{", "}", "[", "]", ";", ",", ".", "?", ":", "#", "@",
    NULL
};

int hc_lex_preprocessed(const char *buf, size_t len, hc_token_list *out)
{
    size_t i = 0;
    int line = 1, col = 1;
    int system = 0;
    char *curfile = NULL;
    char **ftab = NULL;
    size_t fn = 0, fcap = 0;
    int at_line_start = 1;

    memset(out, 0, sizeof(*out));

    while (i < len) {
        char c = buf[i];

        /* GNU linemarker / #line: only at line start. */
        if (at_line_start && c == '#') {
            size_t j = i + 1;
            while (j < len && (buf[j] == ' ' || buf[j] == '\t')) j++;
            /* optional "line" keyword */
            if (j + 4 < len && strncmp(buf + j, "line", 4) == 0) {
                j += 4;
                while (j < len && (buf[j] == ' ' || buf[j] == '\t')) j++;
            }
            if (j < len && isdigit((unsigned char)buf[j])) {
                int newline = 0;
                while (j < len && isdigit((unsigned char)buf[j])) {
                    newline = newline * 10 + (buf[j] - '0');
                    j++;
                }
                while (j < len && (buf[j] == ' ' || buf[j] == '\t')) j++;
                if (j < len && buf[j] == '"') {
                    char namebuf[1024];
                    size_t k = 0;
                    j++;
                    while (j < len && buf[j] != '"' && k < sizeof(namebuf) - 1) {
                        namebuf[k++] = buf[j++];
                    }
                    namebuf[k] = '\0';
                    if (j < len && buf[j] == '"') j++;
                    curfile = intern_file(&ftab, &fn, &fcap, namebuf);
                    /* flags */
                    int flag3 = 0;
                    while (j < len && buf[j] != '\n') {
                        while (j < len && (buf[j] == ' ' || buf[j] == '\t')) j++;
                        if (j < len && isdigit((unsigned char)buf[j])) {
                            if (buf[j] == '3') flag3 = 1;
                            while (j < len && isdigit((unsigned char)buf[j])) j++;
                        } else if (j < len && buf[j] != '\n') {
                            j++;
                        }
                    }
                    system = flag3;   /* F10: linemarker flag 3 ONLY, never a path substring */
                }
                line = newline;
                /* skip to end of line */
                while (j < len && buf[j] != '\n') j++;
                if (j < len) j++;
                i = j;
                col = 1;
                at_line_start = 1;
                continue;
            }
            /* a stray '#' token (e.g. _Pragma lowering) — emit as punct */
        }

        if (c == '\n') {
            line++;
            col = 1;
            i++;
            at_line_start = 1;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            i++;
            col++;
            continue;
        }
        at_line_start = 0;

        /* line comment */
        if (c == '/' && i + 1 < len && buf[i + 1] == '/') {
            while (i < len && buf[i] != '\n') { i++; col++; }
            continue;
        }
        /* block comment */
        if (c == '/' && i + 1 < len && buf[i + 1] == '*') {
            i += 2; col += 2;
            while (i + 1 < len && !(buf[i] == '*' && buf[i + 1] == '/')) {
                if (buf[i] == '\n') { line++; col = 1; } else { col++; }
                i++;
            }
            if (i + 1 < len) { i += 2; col += 2; }
            continue;
        }
        /* string literal (with optional prefix handled by ident path) */
        if (c == '"') {
            size_t s = i;
            int sc = col;
            i++; col++;
            while (i < len && buf[i] != '"') {
                if (buf[i] == '\\' && i + 1 < len) { i += 2; col += 2; }
                else { if (buf[i] == '\n') { line++; col = 1; } else col++; i++; }
            }
            if (i < len) { i++; col++; }
            push(out, HCT_STRING, buf + s, i - s, line, sc, system, curfile);
            continue;
        }
        /* char literal */
        if (c == '\'') {
            size_t s = i;
            int sc = col;
            i++; col++;
            while (i < len && buf[i] != '\'') {
                if (buf[i] == '\\' && i + 1 < len) { i += 2; col += 2; }
                else { i++; col++; }
            }
            if (i < len) { i++; col++; }
            push(out, HCT_CHAR, buf + s, i - s, line, sc, system, curfile);
            continue;
        }
        /* identifier / keyword */
        if (isalpha((unsigned char)c) || c == '_') {
            size_t s = i;
            int sc = col;
            while (i < len && (isalnum((unsigned char)buf[i]) || buf[i] == '_')) { i++; col++; }
            push(out, HCT_IDENT, buf + s, i - s, line, sc, system, curfile);
            continue;
        }
        /* number */
        if (isdigit((unsigned char)c) || (c == '.' && i + 1 < len && isdigit((unsigned char)buf[i + 1]))) {
            size_t s = i;
            int sc = col;
            while (i < len && (isalnum((unsigned char)buf[i]) || buf[i] == '.' ||
                   ((buf[i] == '+' || buf[i] == '-') &&
                    (buf[i-1] == 'e' || buf[i-1] == 'E' || buf[i-1] == 'p' || buf[i-1] == 'P')))) {
                i++; col++;
            }
            push(out, HCT_NUMBER, buf + s, i - s, line, sc, system, curfile);
            continue;
        }
        /* punctuation */
        {
            int matched = 0;
            for (const char **p = PUNCTS; *p; p++) {
                size_t pl = strlen(*p);
                if (i + pl <= len && strncmp(buf + i, *p, pl) == 0) {
                    push(out, HCT_PUNCT, buf + i, pl, line, col, system, curfile);
                    i += pl; col += (int)pl;
                    matched = 1;
                    break;
                }
            }
            if (!matched) { i++; col++; }
        }
    }
    push(out, HCT_EOF, "", 0, line, col, system, curfile);
    out->files = ftab;      /* list now owns the interned file names */
    out->nfiles = fn;
    return 0;
}

void hc_tokens_free(hc_token_list *tl)
{
    size_t i;
    if (tl == NULL) return;
    for (i = 0; i < tl->count; i++) {
        free(tl->toks[i].text);
    }
    free(tl->toks);
    for (i = 0; i < tl->nfiles; i++) free(tl->files[i]);
    free(tl->files);
    tl->toks = NULL;
    tl->files = NULL;
    tl->count = tl->cap = tl->nfiles = 0;
}

size_t hc_decode_string_literal(const char *lexeme, char *dst, size_t cap)
{
    size_t o = 0;
    const char *p = lexeme;
    if (*p == 'u' || *p == 'U' || *p == 'L') p++;
    if (*p == '8') p++;
    if (*p == '"') p++;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'x') {
                int v = 0, nd = 0;
                p++;
                while (isxdigit((unsigned char)*p) && nd < 2) {
                    char hc = *p;
                    int d = (hc <= '9') ? hc - '0' : (tolower(hc) - 'a' + 10);
                    v = v * 16 + d; p++; nd++;
                }
                if (o < cap - 1) dst[o++] = (char)v;
            } else if (*p >= '0' && *p <= '7') {
                int v = 0, nd = 0;
                while (*p >= '0' && *p <= '7' && nd < 3) { v = v * 8 + (*p - '0'); p++; nd++; }
                if (o < cap - 1) dst[o++] = (char)v;
            } else {
                char e = *p++;
                char out = e;
                switch (e) {
                    case 'n': out = '\n'; break;
                    case 't': out = '\t'; break;
                    case 'r': out = '\r'; break;
                    case '0': out = '\0'; break;
                    default: out = e; break;
                }
                if (o < cap - 1) dst[o++] = out;
            }
        } else {
            if (o < cap - 1) dst[o++] = *p;
            p++;
        }
    }
    if (o < cap) dst[o] = '\0';
    return o;
}
