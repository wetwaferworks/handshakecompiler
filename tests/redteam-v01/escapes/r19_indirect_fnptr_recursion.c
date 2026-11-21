/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

static uint64_t rec_helper(uint64_t n)
{
    uint64_t (*self_fp)(uint64_t) = rec_helper;
    if (n == 0) {
        return 1u;
    }
    return self_fp(n - 1u) + 1u;
}

HS_PHASE_PULL
uint64_t r19_indirect_recursion(uint64_t phase)
{
    return rec_helper(phase);
}
