/* SPDX-License-Identifier: MIT */
#ifndef HC_BACKEND_H
#define HC_BACKEND_H

#include <stddef.h>

/*
 * Clean backend seam. HC's frontend (lexer/parser/verifier/lowering) is
 * decoupled from code emission through this vtable, so a standalone native
 * machine-code emitter can later replace the cc-driven backend by providing
 * another hc_backend_ops implementation. (See README "Architecture & backend
 * seam" — v0.1.x drives gcc; a native emitter is v0.2+.)
 */
typedef struct {
    const char *name;
    int (*compile)(const char *cc, const char *src, const char *out,
                   char *const *passthru, size_t npass);
} hc_backend_ops;

const hc_backend_ops *hc_backend_cc(void);

/* Resolve the compiler driver for a target triple / cross prefix. */
const char *hc_backend_driver(const char *target, const char *cross);

/*
 * Compile the EXACT verified, annotation-stripped preprocessed TU (`ipath`,
 * a `.i`) with `-x cpp-output` so gcc NEVER re-preprocesses the original
 * source. `cgflags` carries -std/-O/-m/-g (identical to the analysis pass);
 * NO -D/-U/-I/-include are forwarded. Returns the child's exit code.
 */
int hc_backend_compile_i(const char *cc, const char *ipath, const char *out,
                         char *const *cgflags, size_t ncg);

#endif /* HC_BACKEND_H */
