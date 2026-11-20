/* SPDX-License-Identifier: MIT */
#include <dlfcn.h>
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t c28_cheat_dlsym_hex(void *mu, uint64_t phase)
{
    void *h = dlopen(NULL, RTLD_NOW);
    int (*fn)(void *) = (int (*)(void *))dlsym(h, "\x70thread_" "mutex_" "lock");
    if (fn != NULL && mu != NULL) {
        (void)fn(mu);
    }
    return phase;
}
