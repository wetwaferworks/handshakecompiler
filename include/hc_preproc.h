/* SPDX-License-Identifier: MIT */
#ifndef HC_PREPROC_H
#define HC_PREPROC_H
#include <stddef.h>

/* Read an entire file into a NUL-terminated heap buffer (caller frees). */
char *hc_slurp(const char *path, size_t *len);

/*
 * Dual-pass support: run `cc -E -D__HC_COMPILER__=1 <incs> <defs> src > out`
 * so the preprocessed translation-unit scan sees macro-expanded tokens and
 * GNU linemarkers (system-header provenance). Returns child exit code.
 */
int hc_preprocess(const char *cc, const char *src,
                  char *const *incs, size_t ninc,
                  char *const *defs, size_t ndef,
                  const char *out);

#endif /* HC_PREPROC_H */
