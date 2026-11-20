/* SPDX-License-Identifier: MIT */
#ifndef HC_BACKEND_H
#define HC_BACKEND_H

#include <stddef.h>

/*
 * Clean backend seam. HC's frontend (lexer/parser/verifier/lowering) is
 * decoupled from code emission through this vtable, so a standalone native
 * machine-code emitter can later replace the cc-driven backend by providing
 * another hc_backend_ops implementation.
 */
typedef struct {
    const char *name;
    /* Compile one translation unit to an object/binary. Returns child exit code. */
    int (*compile)(const char *cc, const char *src, const char *out,
                   char *const *passthru, size_t npass);
} hc_backend_ops;

/* The default cc-driven backend (gcc host / aarch64-linux-gnu-gcc cross). */
const hc_backend_ops *hc_backend_cc(void);

/* Resolve the compiler driver for a target triple / cross prefix. */
const char *hc_backend_driver(const char *target, const char *cross);

#endif /* HC_BACKEND_H */
