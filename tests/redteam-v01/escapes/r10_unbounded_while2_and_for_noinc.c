/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t r10_unbounded_variants(uint64_t phase)
{
    uint64_t acc = phase;
    while (2) {
        acc ^= 1ull;
        if (acc == 0xFFFFFFFFFFFFFFFFull) {
            break;
        }
    }
    for (uint32_t i = 0; i < 1u; ) {
        acc += i;
    }
    return acc;
}
