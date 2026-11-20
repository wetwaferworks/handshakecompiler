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

static int cc_compile(const char *cc, const char *src, const char *out,
                      char *const *passthru, size_t npass)
{
    char **argv;
    size_t n = 0, cap = npass + 32, i;
    pid_t pid;
    int status;

    argv = (char **)calloc(cap, sizeof(char *));
    argv[n++] = (char *)cc;
    argv[n++] = (char *)"-std=c11";
    argv[n++] = (char *)"-Wall";
    argv[n++] = (char *)"-Wextra";
    argv[n++] = (char *)"-Wno-unused-function";
    argv[n++] = (char *)"-Wno-unused-variable";
    argv[n++] = (char *)"-Wno-unused-parameter";
    argv[n++] = (char *)"-Wno-unused-but-set-variable";
    for (i = 0; i < npass; i++) argv[n++] = passthru[i];
    argv[n++] = (char *)"-c";
    argv[n++] = (char *)src;
    argv[n++] = (char *)"-o";
    argv[n++] = (char *)out;
    argv[n] = NULL;

    pid = fork();
    if (pid == 0) {
        execvp(cc, argv);
        fprintf(stderr, "hc: cannot exec %s\n", cc);
        _exit(127);
    }
    free(argv);
    if (pid < 0) return -1;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

static const hc_backend_ops CC_OPS = { "cc", cc_compile };
const hc_backend_ops *hc_backend_cc(void) { return &CC_OPS; }
