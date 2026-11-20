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
        "__asm__","asm","__asm","__attribute__","__extension__","_Thread_local",
        "__restrict","__restrict__","__inline","__inline__","__volatile__", NULL
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
        /* Skip __attribute__((...)) groups: annotation payload is not code, so
           idents inside (e.g. "annotate") are never calls / address-taken refs. */
        if (t[i].kind == HCT_IDENT && strcmp(t[i].text, "__attribute__") == 0 &&
            i + 1 < f->body_end && t[i+1].kind == HCT_PUNCT &&
            strcmp(t[i+1].text, "(") == 0) {
            i = match(tl, i + 1, "(", ")");
            continue;
        }
        if (t[i].kind == HCT_IDENT && !is_c_keyword(t[i].text)) {
            int prev_member = (i > 0 && t[i-1].kind == HCT_PUNCT &&
                               (strcmp(t[i-1].text, ".") == 0 || strcmp(t[i-1].text, "->") == 0));
            int next_call = (i + 1 < tl->count && t[i+1].kind == HCT_PUNCT &&
                             strcmp(t[i+1].text, "(") == 0);
            if (prev_member) continue;            /* member access, not a symbol ref */
            if (next_call) {
                addstr(&f->callees, &f->ncallees, t[i].text);
            } else {
                /* address-taken / value use of a (possibly) function name */
                addstr(&f->fnrefs, &f->nfnrefs, t[i].text);
            }
        }
    }
}

static void add_param(hc_func *f, const char *name, int is_ptr)
{
    f->params = (char **)realloc(f->params, (f->nparams + 1) * sizeof(char *));
    f->param_ptr = (int *)realloc(f->param_ptr, (f->nparams + 1) * sizeof(int));
    f->params[f->nparams] = strdup(name);
    f->param_ptr[f->nparams] = is_ptr;
    f->nparams++;
}

static void parse_params(const hc_token_list *tl, hc_func *f, size_t popen, size_t pclose)
{
    const hc_token *t = tl->toks;
    size_t i = popen + 1;
    size_t seg_start = i;
    int depth = 0;
    while (i <= pclose) {
        int at_end = (i == pclose);
        int is_comma = (t[i].kind == HCT_PUNCT && strcmp(t[i].text, ",") == 0 && depth == 0);
        if (t[i].kind == HCT_PUNCT && (strcmp(t[i].text,"(")==0)) depth++;
        else if (t[i].kind == HCT_PUNCT && (strcmp(t[i].text,")")==0) && !at_end) depth--;
        if (is_comma || at_end) {
            int has_const = 0, has_ro = 0, has_ptr = 0;
            const char *name = NULL;
            char ann[128];
            size_t k;
            for (k = seg_start; k < i; k++) {
                if (t[k].kind == HCT_PUNCT && strcmp(t[k].text, "*") == 0) has_ptr = 1;
                if (t[k].kind == HCT_IDENT && strcmp(t[k].text, "const") == 0) has_const = 1;
                if (t[k].kind == HCT_IDENT && annotate_at(tl, k, ann, sizeof(ann))) {
                    if (strcmp(ann, "hs:mem:readonly") == 0 ||
                        strcmp(ann, "hs:ptr:no_escape") == 0) has_ro = 1;
                }
                if (t[k].kind == HCT_IDENT && !is_c_keyword(t[k].text) &&
                    strcmp(t[k].text, "annotate") != 0) {
                    name = t[k].text;   /* last plain ident wins */
                }
            }
            if (name != NULL) {
                add_param(f, name, has_ptr);
                if (has_const || has_ro) addstr(&f->ro_params, &f->nro, name);
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
        /* System-header tokens (GNU linemarker flag 3, e.g. <string.h>) are not
           part of any user declaration. Advance stmt_start past them so the next
           user decl's scan does not bleed across the header (which would pick up
           stray const/'*' and mis-track an HS_SHARED global declared right after
           a system #include — Cortana V2/b09). */
        if (t[i].system) { i++; if (depth == 0) stmt_start = i; continue; }

        /* skip __attribute__((...)) / __asm__(...) groups but REMEMBER whether
         * a constructor/destructor attribute was present for the next decl. */
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
            size_t close = match(tl, i, "{", "}");
            i = close + 1;
            if (i < tl->count && t[i].kind == HCT_PUNCT && strcmp(t[i].text, ";") == 0) i++;
            stmt_start = i;
            continue;
        }

        if (depth == 0 && t[i].kind == HCT_IDENT && !is_c_keyword(t[i].text) &&
            i + 1 < tl->count && t[i+1].kind == HCT_PUNCT && strcmp(t[i+1].text, "(") == 0) {
            size_t popen = i + 1;
            size_t pclose = match(tl, popen, "(", ")");
            size_t j = pclose + 1;
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
                f.name = strdup(t[i].text);
                f.decl_start = stmt_start;
                f.body_start = bopen;
                f.body_end = bclose;
                f.phase = PH_NONE;
                /* scan [stmt_start, popen) for phase/pure/ctor annotations */
                for (k = stmt_start; k < popen; k++) {
                    if (t[k].kind == HCT_IDENT && annotate_at(tl, k, ann, sizeof(ann))) {
                        hc_phase ph = phase_from_ann(ann);
                        if (ph != PH_NONE) f.phase = ph;
                        if (strcmp(ann, "hs:pure:deterministic") == 0) f.is_pure = 1;
                    }
                    if (t[k].kind == HCT_IDENT &&
                        (strcmp(t[k].text, "constructor") == 0 ||
                         strcmp(t[k].text, "destructor") == 0))
                        f.is_ctor = 1;
                }
                parse_params(tl, &f, popen, pclose);
                scan_func_body(tl, &f);
                u->funcs = (hc_func *)realloc(u->funcs, (u->nfuncs + 1) * sizeof(hc_func));
                u->funcs[u->nfuncs++] = f;
                i = bclose + 1;
                stmt_start = i;
                continue;
            }
            i = pclose + 1;
            continue;
        }

        /* file-scope variable declaration */
        if (depth == 0 && t[i].kind == HCT_PUNCT &&
            (strcmp(t[i].text, "=") == 0 || strcmp(t[i].text, ";") == 0 ||
             strcmp(t[i].text, "[") == 0)) {
            int is_ptr = 0, is_const = 0, is_shared = 0;
            int const_before_star = 0, seen_star = 0;
            const char *name = NULL;
            char ann[128];
            size_t k;
            int sawtype = 0;
            for (k = stmt_start; k < i; k++) {
                if (t[k].kind == HCT_PUNCT && strcmp(t[k].text, "*") == 0) { is_ptr = 1; seen_star = 1; }
                if (t[k].kind == HCT_IDENT && strcmp(t[k].text, "const") == 0) {
                    is_const = 1;
                    if (!seen_star) const_before_star = 1;
                }
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
                v.name = strdup(name);
                v.is_pointer = is_ptr;
                v.is_const = is_const;
                /* pointee is const when const qualifies it: `const T *p`
                   (const before the '*'), or a const object with no pointer.
                   `T * const p` (const after '*') leaves the pointee MUTABLE. */
                v.is_const_pointee = is_ptr ? const_before_star : is_const;
                v.is_array = (strcmp(t[i].text, "[") == 0);
                if (!v.is_array) { size_t z; for (z = stmt_start; z < i; z++)
                    if (t[z].kind == HCT_PUNCT && strcmp(t[z].text, "[") == 0) { v.is_array = 1; break; } }
                v.is_shared = is_shared;
                if (hc_find_var(u, name) == NULL) {
                    u->vars = (hc_var *)realloc(u->vars, (u->nvars + 1) * sizeof(hc_var));
                    u->vars[u->nvars++] = v;
                } else {
                    free(v.name);
                }
            }
            if (strcmp(t[i].text, "=") == 0) {
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
        for (k = 0; k < u->funcs[i].nparams; k++) free(u->funcs[i].params[k]);
        free(u->funcs[i].callees);
        free(u->funcs[i].fnrefs);
        free(u->funcs[i].ro_params);
        free(u->funcs[i].params);
        free(u->funcs[i].param_ptr);
        free(u->funcs[i].name);
    }
    for (i = 0; i < u->nvars; i++) free(u->vars[i].name);
    free(u->funcs);
    free(u->vars);
    memset(u, 0, sizeof(*u));
}
