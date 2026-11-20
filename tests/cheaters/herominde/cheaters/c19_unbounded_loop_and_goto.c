/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t c19_cheat_unbounded(uint64_t phase)
{
    uint64_t x = phase;
retry:
    x = hc_fnv1a64_step(x, 1u);
    if (x != 0) {
        goto retry;
    }
    return x;
}
