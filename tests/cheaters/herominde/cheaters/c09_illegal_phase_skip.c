/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_COMMIT
static uint64_t secret_commit(uint64_t v)
{
    return hc_fnv1a64_step(0, v);
}

HS_PHASE_PULL
uint64_t c09_cheat_skip_barrier(uint64_t phase)
{
    /* Illegal: PULL directly invokes COMMIT without BARRIER */
    return secret_commit(phase);
}
