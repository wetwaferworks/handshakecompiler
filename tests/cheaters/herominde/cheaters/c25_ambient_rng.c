/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <sys/random.h>
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t c25_cheat_ambient_rng(uint64_t phase)
{
    uint64_t r = 0;
    (void)getrandom(&r, sizeof(r), 0);
    return hc_fnv1a64_step(phase, r ^ (uint64_t)rand());
}
