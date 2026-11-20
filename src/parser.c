/* SPDX-License-Identifier: MIT */
#include "hc_ast.h"
#include <stdlib.h>
#include <string.h>

static int is_c_keyword(const char *s)
{
    static const char *kw[] = {
        "if","for","while","switch","return","sizeof","do","else","case",
        "default","break","continue","goto","typedef","struct","union","enum",
        "static","extern","const","volatile","unsigned","signed","void","char",
        "short","int","long","float","double","_Bool","_Atomic","inline",
        "restrict","register","_Static_assert","_Alignof","_Generic","_Noreturn",
        "__asm__","asm","__attribute__","__extension__","_Thread_local", NULL
    };
    const char **p;
    for (p = kw; *p; p++) if (strcmp(*p, s) == 0) return 1;
    return 0;
}

static void addstr(char ***arr, size_t *n, const char *s)
{
    size_t i;
    for (i = 0; i < *n; i++) if (strcmp((*arr)[i], s) == 0) return;
    *arr = (char **)realloc(*arr, (*n + 1) * sizeof(char *));
    (*arr)[*n] = strdup(s);
    (*n)++;
}

/* Decode an annotate("hs:...") payload sitting at token index of "annotate". */
static int annotate_at(const hc_token_list *tl, size_t i, char *out, size_t cap)
{
    const hc_token *t = tl->toks;
    if (i + 2 >= tl->count) return 0;
    if (t[i].kind == HCT_IDENT && strcmp(t[i].text, "annotate") == 0 &&
        t[i+1].kind == HCT_PUNCT && strcmp(t[i+1].text, "(") == 0 &&
        t[i+2].kind == HCT_STRING) {
        hc_decode_string_literal(t[i+2].text, out, cap);
        return 1;
    }
    return 0;
}

static hc_phase phase_from_ann(const char *a)
{
    if (strcmp(a, "hs:phase:idle") == 0) return PH_IDLE;
    if (strcmp(a, "hs:phase:publish") == 0) return PH_PUBLISH;
    if (strcmp(a, "hs:phase:pull") == 0) return PH_PULL;
    if (strcmp(a, "hs:phase:barrier") == 0) return PH_BARRIER;
    if (strcmp(a, "hs:phase:commit") == 0) return PH_COMMIT;
    return PH_NONE;
}

/* Find matching close punct for an open at index `open` (counting open/close). */
static size_t match(const hc_token_list *tl, size_t open, const char *o, const char *c)
{
    const hc_token *t = tl->toks;
    int d = 0;
    size_t i;
    for (i = open; i < tl->count; i++) {
        if (t[i].kind == HCT_PUNCT && strcmp(t[i].text, o) == 0) d++;
        else if (t[i].kind == HCT_PUNCT && strcmp(t[i].text, c) == 0) {
            d--;
            if (d == 0) return i;
        }
    }
    return tl->count - 1;
}

static void scan_func_body(const hc_token_list *tl, hc_func *f)
{
    const hc_token *t = tl->toks;
    size_t i;
    for (i = f->body_start + 1; i < f->body_end; i++) {
        if (t[i].kind == HCT_IDENT && !is_c_keyword(t[i].text)) {
            if (i + 1 < tl->count && t[i+1].kind == HCT_PUNCT &&
                strcmp(t[i+1].text, "(") == 0) {
                addstr(&f->callees, &f->ncallees, t[i].text);
            }
        }
    }
}

static void parse_params(const hc_token_list *tl, hc_func *f, size_t popen, size_t pclose)
{
    const hc_token *t = tl->toks;
    size_t i = popen + 1;
    size_t seg_start = i;
    int depth = 0;
    /* iterate params split on top-level commas */
    while (i <= pclose) {
        int at_end = (i == pclose);
        int is_comma = (t[i].kind == HCT_PUNCT && strcmp(t[i].text, ",") == 0 && depth == 0);
        if (t[i].kind == HCT_PUNCT && (strcmp(t[i].text,"(")==0)) depth++;
        else if (t[i].kind == HCT_PUNCT && (strcmp(t[i].text,")")==0) && !at_end) depth--;
        if (is_comma || at_end) {
            /* segment [seg_start, i) */
            int has_const = 0, has_ro = 0;
            const char *name = NULL;
            char ann[128];
            size_t k;
            for (k = seg_start; k < i; k++) {
                if (t[k].kind == HCT_IDENT && strcmp(t[k].text, "const") == 0) has_const = 1;
                if (t[k].kind == HCT_IDENT && annotate_at(tl, k, ann, sizeof(ann))) {
                    if (strcmp(ann, "hs:mem:readonly") == 0 ||
                        strcmp(ann, "hs:ptr:no_escape") == 0) has_ro = 1;
                }
                if (t[k].kind == HCT_IDENT && !is_c_keyword(t[k].text) &&
                    strcmp(t[k].text, "annotate") != 0 &&
                    strcmp(t[k].text, "restrict") != 0) {
                    /* candidate name (last plain ident wins) */
                    name = t[k].text;
                }
            }
            if (name != NULL && (has_const || has_ro)) {
                addstr(&f->ro_params, &f->nro, name);
            }
            seg_start = i + 1;
        }
        i++;
    }
}

int hc_parse_unit(const hc_token_list *tl, hc_unit *u)
{
    const hc_token *t = tl->toks;
    size_t i = 0;
    int depth = 0;
    size_t stmt_start = 0;

    memset(u, 0, sizeof(*u));

    while (i < tl->count && t[i].kind != HCT_EOF) {
        if (t[i].system) { i++; continue; }

        /* skip __attribute__((...)) and __asm__(...) groups entirely */
        if (t[i].kind == HCT_IDENT &&
            (strcmp(t[i].text,"__attribute__")==0 || strcmp(t[i].text,"__asm__")==0 ||
             strcmp(t[i].text,"__asm")==0 || strcmp(t[i].text,"asm")==0)) {
            size_t j = i + 1;
            while (j < tl->count && !(t[j].kind==HCT_PUNCT && strcmp(t[j].text,"(")==0) &&
                   !(t[j].kind==HCT_PUNCT && strcmp(t[j].text,";")==0)) j++;
            if (j < tl->count && t[j].kind==HCT_PUNCT && strcmp(t[j].text,"(")==0) {
                i = match(tl, j, "(", ")") + 1;
            } else { i = j; }
            continue;
        }

        if (t[i].kind == HCT_PUNCT && strcmp(t[i].text, "{") == 0 && depth == 0) {
            /* a brace at depth 0 that is not a function body: an aggregate-type
             * body (struct/enum/union def) or a stray init list. Consume the
             * whole definition including a trailing ';' so its members are not
             * mistaken for file-scope declarators. */
            size_t close = match(tl, i, "{", "}");
            i = close + 1;
            if (i < tl->count && t[i].kind == HCT_PUNCT && strcmp(t[i].text, ";") == 0) i++;
            stmt_start = i;
            continue;
        }

        if (depth == 0 && t[i].kind == HCT_IDENT && !is_c_keyword(t[i].text) &&
            i + 1 < tl->count && t[i+1].kind == HCT_PUNCT && strcmp(t[i+1].text, "(") == 0) {
            /* possible function def: name ( params ) { ... } */
            size_t popen = i + 1;
            size_t pclose = match(tl, popen, "(", ")");
            size_t j = pclose + 1;
            /* skip attribute tokens / const between ) and { */
            while (j < tl->count && t[j].kind != HCT_EOF) {
                if (t[j].kind == HCT_PUNCT && strcmp(t[j].text, "{") == 0) break;
                if (t[j].kind == HCT_PUNCT && (strcmp(t[j].text, ";") == 0 ||
                    strcmp(t[j].text, "=") == 0 || strcmp(t[j].text, ",") == 0)) { j = 0; break; }
                j++;
            }
            if (j != 0 && j < tl->count && t[j].kind == HCT_PUNCT && strcmp(t[j].text, "{") == 0) {
                size_t bopen = j;
                size_t bclose = match(tl, bopen, "{", "}");
                hc_func f;
                char ann[128];
                size_t k;
                memset(&f, 0, sizeof(f));
                strncpy(f.name, t[i].text, sizeof(f.name) - 1);
                f.decl_start = stmt_start;
                f.body_start = bopen;
                f.body_end = bclose;
                f.phase = PH_NONE;
                /* scan [stmt_start, popen) for phase/pure annotations */
                for (k = stmt_start; k < popen; k++) {
                    if (t[k].kind == HCT_IDENT && annotate_at(tl, k, ann, sizeof(ann))) {
                        hc_phase ph = phase_from_ann(ann);
                        if (ph != PH_NONE) f.phase = ph;
                        if (strcmp(ann, "hs:pure:deterministic") == 0) f.is_pure = 1;
                    }
                }
                parse_params(tl, &f, popen, pclose);
                scan_func_body(tl, &f);
                u->funcs = (hc_func *)realloc(u->funcs, (u->nfuncs + 1) * sizeof(hc_func));
                u->funcs[u->nfuncs++] = f;
                i = bclose + 1;
                stmt_start = i;
                continue;
            }
            /* not a def — fall through, advance past the paren group */
            i = pclose + 1;
            continue;
        }

        /* file-scope variable declaration: capture name at '=' or ';' or '[' */
        if (depth == 0 && t[i].kind == HCT_PUNCT &&
            (strcmp(t[i].text, "=") == 0 || strcmp(t[i].text, ";") == 0 ||
             strcmp(t[i].text, "[") == 0)) {
            /* find declared name = last plain ident in [stmt_start, i) */
            int is_ptr = 0, is_const = 0, is_shared = 0;
            const char *name = NULL;
            char ann[128];
            size_t k;
            int sawtype = 0;
            for (k = stmt_start; k < i; k++) {
                if (t[k].kind == HCT_PUNCT && strcmp(t[k].text, "*") == 0) is_ptr = 1;
                if (t[k].kind == HCT_IDENT && strcmp(t[k].text, "const") == 0) is_const = 1;
                if (t[k].kind == HCT_IDENT && annotate_at(tl, k, ann, sizeof(ann))) {
                    if (strcmp(ann, "hs:mem:shared") == 0) is_shared = 1;
                }
                if (t[k].kind == HCT_IDENT && !is_c_keyword(t[k].text) &&
                    strcmp(t[k].text, "annotate") != 0) {
                    if (sawtype) name = t[k].text;
                    sawtype = 1;
                }
            }
            if (name != NULL && !is_c_keyword(name)) {
                hc_var v;
                memset(&v, 0, sizeof(v));
                strncpy(v.name, name, sizeof(v.name) - 1);
                v.is_pointer = is_ptr;
                v.is_const = is_const;
                v.is_shared = is_shared;
                if (hc_find_var(u, name) == NULL) {
                    u->vars = (hc_var *)realloc(u->vars, (u->nvars + 1) * sizeof(hc_var));
                    u->vars[u->nvars++] = v;
                }
            }
            if (strcmp(t[i].text, "=") == 0) {
                /* skip initializer to the terminating ';' at depth 0 */
                size_t j = i + 1;
                int pd = 0, bd = 0;
                while (j < tl->count && t[j].kind != HCT_EOF) {
                    if (t[j].kind == HCT_PUNCT) {
                        if (strcmp(t[j].text,"(")==0) pd++;
                        else if (strcmp(t[j].text,")")==0) pd--;
                        else if (strcmp(t[j].text,"{")==0) bd++;
                        else if (strcmp(t[j].text,"}")==0) bd--;
                        else if (strcmp(t[j].text,";")==0 && pd<=0 && bd<=0) break;
                    }
                    j++;
                }
                i = j + 1;
                stmt_start = i;
                continue;
            }
            if (strcmp(t[i].text, "[") == 0) {
                size_t close = match(tl, i, "[", "]");
                i = close + 1;
                continue;
            }
            i++;
            stmt_start = i;
            continue;
        }

        if (t[i].kind == HCT_PUNCT && strcmp(t[i].text, "{") == 0) depth++;
        else if (t[i].kind == HCT_PUNCT && strcmp(t[i].text, "}") == 0) { if (depth) depth--; if (depth==0) stmt_start = i+1; }
        else if (t[i].kind == HCT_PUNCT && strcmp(t[i].text, ";") == 0 && depth == 0) stmt_start = i + 1;
        i++;
    }
    return 0;
}

void hc_unit_free(hc_unit *u)
{
    size_t i, k;
    for (i = 0; i < u->nfuncs; i++) {
        for (k = 0; k < u->funcs[i].ncallees; k++) free(u->funcs[i].callees[k]);
        for (k = 0; k < u->funcs[i].nfnrefs; k++) free(u->funcs[i].fnrefs[k]);
        for (k = 0; k < u->funcs[i].nro; k++) free(u->funcs[i].ro_params[k]);
        free(u->funcs[i].callees);
        free(u->funcs[i].fnrefs);
        free(u->funcs[i].ro_params);
    }
    free(u->funcs);
    free(u->vars);
    memset(u, 0, sizeof(*u));
}
