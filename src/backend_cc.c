/* SPDX-License-Identifier: MIT */
#include "hc_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

const char *hc_backend_driver(const char *target, const char *cross)
{
    static char buf[256];
    if (cross && cross[0]) { snprintf(buf, sizeof(buf), "%sgcc", cross); return buf; }
    if (target && target[0]) {
        if (strstr(target, "aarch64")) return "aarch64-linux-gnu-gcc";
        snprintf(buf, sizeof(buf), "%s-gcc", target);
        return buf;
    }
    return "gcc";
}

static int run(char **argv, const char *cc)
{
    pid_t pid = fork();
    int status;
    if (pid == 0) { execvp(cc, argv); fprintf(stderr, "hc: cannot exec %s\n", cc); _exit(127); }
    if (pid < 0) return -1;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

int hc_backend_compile_i(const char *cc, const char *ipath, const char *out,
                         char *const *cgflags, size_t ncg)
{
    char **argv;
    size_t n = 0, cap = ncg + 16, i;
    int have_std = 0, rc;
    for (i = 0; i < ncg; i++) if (strncmp(cgflags[i], "-std=", 5) == 0) have_std = 1;
    argv = (char **)calloc(cap, sizeof(char *));
    if (argv == NULL) { fprintf(stderr, "hc: out of memory building backend argv\n"); return -1; }
    argv[n++] = (char *)cc;
    if (!have_std) argv[n++] = (char *)"-std=c11";
    for (i = 0; i < ncg; i++) argv[n++] = cgflags[i];
    argv[n++] = (char *)"-c";
    argv[n++] = (char *)"-x";
    argv[n++] = (char *)"cpp-output";
    argv[n++] = (char *)ipath;
    argv[n++] = (char *)"-o";
    argv[n++] = (char *)out;
    argv[n] = NULL;
    rc = run(argv, cc);
    free(argv);
    return rc;
}

/* Legacy direct-source path (retained for the seam; unused by the .i pipeline). */
static int cc_compile(const char *cc, const char *src, const char *out,
                      char *const *passthru, size_t npass)
{
    char **argv;
    size_t n = 0, cap = npass + 16, i;
    int rc;
    argv = (char **)calloc(cap, sizeof(char *));
    if (argv == NULL) return -1;
    argv[n++] = (char *)cc;
    argv[n++] = (char *)"-std=c11";
    for (i = 0; i < npass; i++) argv[n++] = passthru[i];
    argv[n++] = (char *)"-c";
    argv[n++] = (char *)src;
    argv[n++] = (char *)"-o";
    argv[n++] = (char *)out;
    argv[n] = NULL;
    rc = run(argv, cc);
    free(argv);
    return rc;
}

static const hc_backend_ops CC_OPS = { "cc", cc_compile };
const hc_backend_ops *hc_backend_cc(void) { return &CC_OPS; }
