/* SPDX-License-Identifier: MIT */
#ifndef HC_RULES_H
#define HC_RULES_H

#include "hc_ast.h"
#include "hc_lexer.h"

/* Enforcement profile. */
typedef enum {
    HC_PROFILE_COMPONENT = 0,  /* default: strict HC-E001..HC-E026 */
    HC_PROFILE_RUNTIME         /* audited handshake runtime core only */
} hc_profile;

typedef struct {
    const char *code;      /* "HC-E001" */
    const char *name;      /* "LOCK_POSIX" */
    const char *summary;   /* short human description */
} hc_rule_desc;

/* The declarative rule registry (HC-E001..HC-E026). */
const hc_rule_desc *hc_rule_registry(size_t *count);
void hc_dump_rules(void);

/*
 * Verify a parsed unit. Returns the number of diagnostics emitted (0 = clean).
 * Prints each as `file:line:col: error [HC-E###]: message` to stderr.
 *   rawsrc  : original (un-preprocessed) user source text, for raw-pass checks.
 *   srcname : display file name.
 */
int hc_verify(hc_unit *u, const hc_token_list *tl,
              const char *rawsrc, const char *srcname, hc_profile profile);

#endif /* HC_RULES_H */
