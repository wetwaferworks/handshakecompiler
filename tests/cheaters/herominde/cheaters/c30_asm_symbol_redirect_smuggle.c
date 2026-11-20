/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

/* Looks like a benign function name in C AST, links directly to pthread_mutex_lock! */
extern int innocent_tick_helper(void *opaque) __asm__("pthread_mutex_lock");

HS_PHASE_PULL
uint64_t c30_cheat_asm_alias(void *opaque, uint64_t phase)
{
    if (opaque != NULL) {
        (void)innocent_tick_helper(opaque);
    }
    return phase;
}
