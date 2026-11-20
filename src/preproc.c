/* SPDX-License-Identifier: MIT */
#include "hc_preproc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define HC_PP_MAXARG 512

char *hc_slurp(const char *path, size_t *len)
{
    FILE *fp = fopen(path, "rb");
    char *b; long sz;
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    sz = ftell(fp); if (sz < 0) { fclose(fp); return NULL; }
    rewind(fp);
    b = (char *)malloc((size_t)sz + 1);
    if (!b) { fclose(fp); return NULL; }
    if (fread(b, 1, (size_t)sz, fp) != (size_t)sz) { free(b); fclose(fp); return NULL; }
    b[sz] = '\0';
    if (len) *len = (size_t)sz;
    fclose(fp);
    return b;
}

int hc_preprocess(const char *cc, const char *src,
                  char *const *flags, size_t nflags,
                  const char *out)
{
    char *argv[HC_PP_MAXARG]; size_t n = 0, i;
    pid_t pid; int status, fd;
    int have_std = 0;
    for (i = 0; i < nflags; i++)
        if (strncmp(flags[i], "-std=", 5) == 0) have_std = 1;
    argv[n++] = (char *)cc;
    argv[n++] = (char *)"-E";
    argv[n++] = (char *)"-D__HC_COMPILER__=1";
    if (!have_std) argv[n++] = (char *)"-std=c11";
    for (i = 0; i < nflags && n < HC_PP_MAXARG - 4; i++) argv[n++] = flags[i];
    argv[n++] = (char *)src;
    argv[n] = NULL;
    pid = fork();
    if (pid == 0) {
        fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) _exit(127);
        dup2(fd, 1); close(fd);
        execvp(cc, argv);
        _exit(127);
    }
    if (pid < 0) return -1;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/*
 * Strip __attribute__((annotate("hs:..."))) groups. String/char-literal aware
 * so parentheses inside string constants never miscount. Only groups whose
 * balanced body contains BOTH `annotate` and `hs:` are removed; every other
 * attribute (e.g. glibc __nothrow__) is preserved verbatim.
 */
int hc_strip_annotations(const char *inpath, const char *outpath)
{
    size_t len = 0;
    char *in = hc_slurp(inpath, &len);
    FILE *out;
    size_t i = 0;
    if (!in) return -1;
    out = fopen(outpath, "wb");
    if (!out) { free(in); return -1; }

    while (i < len) {
        char c = in[i];
        /* skip over string / char literals verbatim */
        if (c == '"' || c == '\'') {
            char q = c; fputc(c, out); i++;
            while (i < len && in[i] != q) {
                if (in[i] == '\\' && i + 1 < len) { fputc(in[i], out); fputc(in[i+1], out); i += 2; }
                else { fputc(in[i], out); i++; }
            }
            if (i < len) { fputc(in[i], out); i++; }
            continue;
        }
        /* __attribute__ ... (( ... )) */
        if (c == '_' && strncmp(in + i, "__attribute__", 13) == 0 &&
            (i + 13 >= len || !(isalnum((unsigned char)in[i+13]) || in[i+13] == '_'))) {
            size_t j = i + 13;
            while (j < len && isspace((unsigned char)in[j])) j++;
            if (j + 1 < len && in[j] == '(' && in[j+1] == '(') {
                /* find the balanced end of the outer '(' at j */
                size_t k = j; int d = 0; int instr = 0; char sq = 0;
                int has_ann = 0, has_hs = 0;
                while (k < len) {
                    char ch = in[k];
                    if (instr) {
                        if (ch == '\\' && k + 1 < len) { k += 2; continue; }
                        if (ch == sq) instr = 0;
                        k++; continue;
                    }
                    if (ch == '"' || ch == '\'') { instr = 1; sq = ch; k++; continue; }
                    if (ch == '(') d++;
                    else if (ch == ')') { d--; if (d == 0) { k++; break; } }
                    k++;
                }
                /* k is one past the matching ')' */
                { size_t z; for (z = j; z + 8 <= k; z++) {
                      if (strncmp(in + z, "annotate", 8) == 0) has_ann = 1;
                      if (strncmp(in + z, "hs:", 3) == 0) has_hs = 1;
                  } }
                if (d == 0 && has_ann && has_hs) {
                    /* drop the whole __attribute__((...)) group */
                    i = k;
                    continue;
                }
                /* keep verbatim */
                fwrite(in + i, 1, k - i, out);
                i = k;
                continue;
            }
            fwrite(in + i, 1, 13, out);
            i += 13;
            continue;
        }
        fputc(c, out);
        i++;
    }
    free(in);
    if (fclose(out) != 0) return -1;
    return 0;
}
