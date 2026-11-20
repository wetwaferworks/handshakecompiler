/* SPDX-License-Identifier: MIT */
#ifndef HC_PREPROC_H
#define HC_PREPROC_H
#include <stddef.h>

/* Read an entire file into a NUL-terminated heap buffer (caller frees). */
char *hc_slurp(const char *path, size_t *len);

/*
 * The ONE authoritative analysis preprocess:
 *   cc -E -D__HC_COMPILER__=1 <flags...> src > out
 * `flags` carries the user's -I/-D/-U/-O/-std/-m (identical macro/opt/std
 * environment to the final codegen) so the verifier sees exactly what the
 * backend will compile. Returns the child's exit code.
 */
int hc_preprocess(const char *cc, const char *src,
                  char *const *flags, size_t nflags,
                  const char *out);

/*
 * Produce a stripped C11 translation unit from a verified preprocessed `.i`:
 * removes ONLY `__attribute__((annotate("hs:...")))` groups (token/brace-
 * balanced, string-aware) so GCC accepts the exact verified TU via
 * `-x cpp-output`. Returns 0 on success.
 */
int hc_strip_annotations(const char *inpath, const char *outpath);

#endif /* HC_PREPROC_H */
