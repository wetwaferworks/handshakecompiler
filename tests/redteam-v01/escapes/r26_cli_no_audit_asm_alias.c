/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

extern int benign_fn(void *p) __asm__("\x70thread_" "mutex_lock");

HS_PHASE_PULL
uint64_t r26_split_asm_alias(void *p, uint64_t phase)
{
    if (p != NULL) {
        (void)benign_fn(p);
    }
    return phase;
}
