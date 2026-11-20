/* SPDX-License-Identifier: MIT */
#include "hc_ast.h"
#include <string.h>

hc_func *hc_find_func(hc_unit *u, const char *name)
{
    size_t i;
    for (i = 0; i < u->nfuncs; i++) {
        if (strcmp(u->funcs[i].name, name) == 0) {
            return &u->funcs[i];
        }
    }
    return NULL;
}

hc_var *hc_find_var(hc_unit *u, const char *name)
{
    size_t i;
    for (i = 0; i < u->nvars; i++) {
        if (strcmp(u->vars[i].name, name) == 0) {
            return &u->vars[i];
        }
    }
    return NULL;
}
