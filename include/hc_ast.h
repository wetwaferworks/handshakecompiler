/* SPDX-License-Identifier: MIT */
#ifndef HC_AST_H
#define HC_AST_H

#include "hc_lexer.h"
#include <stddef.h>

typedef enum {
    PH_NONE = 0,
    PH_IDLE,
    PH_PUBLISH,
    PH_PULL,
    PH_BARRIER,
    PH_COMMIT
} hc_phase;

typedef struct {
    char   name[160];
    hc_phase phase;
    int    is_pure;
    size_t decl_start;   /* token index at start of declaration/attributes */
    size_t body_start;   /* index of '{' */
    size_t body_end;     /* index of matching '}' */
    /* callee names (direct call: ident followed by '(') */
    char **callees;  size_t ncallees;
    /* function addresses taken (ident == known fn, not followed by '(') */
    char **fnrefs;   size_t nfnrefs;
    /* const/readonly pointer param names */
    char **ro_params; size_t nro;
    /* reachability flags (filled by analysis) */
    int reach_strict;   /* reachable from a PULL/BARRIER kernel */
    int reach_commit;   /* reachable from a COMMIT kernel */
    int visited;
} hc_func;

typedef struct {
    char name[160];
    int  is_pointer;
    int  is_const;
    int  is_shared;     /* HS_SHARED annotated */
} hc_var;

typedef struct {
    hc_func *funcs; size_t nfuncs;
    hc_var  *vars;  size_t nvars;
} hc_unit;

/* Build the structural model from a preprocessed token list (user tokens). */
int  hc_parse_unit(const hc_token_list *tl, hc_unit *u);
void hc_unit_free(hc_unit *u);

hc_func *hc_find_func(hc_unit *u, const char *name);
hc_var  *hc_find_var(hc_unit *u, const char *name);

#endif /* HC_AST_H */
