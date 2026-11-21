/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

static const uint64_t k_primes[4] = { 2u, 3u, 5u, 7u };

HS_PHASE_PULL HS_PURE
uint64_t w14_read_const_arr(uint64_t phase)
{
    const uint64_t *p = k_primes;
    return hc_fnv1a64_step(phase, p[phase & 3u]);
}
