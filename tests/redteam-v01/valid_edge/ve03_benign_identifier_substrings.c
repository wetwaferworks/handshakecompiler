/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL HS_PURE
uint64_t ve03_benign_names(uint64_t time_step, uint64_t rand_seed_arg)
{
    uint64_t mutex_free_ticks = time_step + 1u;
    uint64_t lock_free_hash = hc_fnv1a64_step(mutex_free_ticks, rand_seed_arg);
    return lock_free_hash;
}
