/* SPDX-License-Identifier: MIT */
#include "hc_preproc.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define HC_PP_MAXARG 256

char *hc_slurp(const char *path, size_t *len)
{
    FILE *fp = fopen(path, "rb");
    char *b; long sz;
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz < 0) { fclose(fp); return NULL; }
    b = (char *)malloc((size_t)sz + 1);
    if (!b) { fclose(fp); return NULL; }
    if (fread(b, 1, (size_t)sz, fp) != (size_t)sz) { free(b); fclose(fp); return NULL; }
    b[sz] = '\0';
    if (len) *len = (size_t)sz;
    fclose(fp);
    return b;
}

int hc_preprocess(const char *cc, const char *src,
                  char *const *incs, size_t ninc,
                  char *const *defs, size_t ndef,
                  const char *out)
{
    char *argv[HC_PP_MAXARG]; size_t n = 0, i;
    pid_t pid; int status, fd;
    argv[n++] = (char *)cc;
    argv[n++] = (char *)"-E";
    argv[n++] = (char *)"-std=c11";
    argv[n++] = (char *)"-D__HC_COMPILER__=1";
    for (i = 0; i < ninc && n < HC_PP_MAXARG - 4; i++) argv[n++] = incs[i];
    for (i = 0; i < ndef && n < HC_PP_MAXARG - 4; i++) argv[n++] = defs[i];
    argv[n++] = (char *)src;
    argv[n] = NULL;
    pid = fork();
    if (pid == 0) {
        fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) _exit(127);
        dup2(fd, 1); close(fd);
        execvp(cc, argv);
        _exit(127);
    }
    if (pid < 0) return -1;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
