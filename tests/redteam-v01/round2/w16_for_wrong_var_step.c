/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t w16_wrong_step(uint64_t phase)
{
    uint64_t acc = phase;
    for (uint32_t i = 0, j = 0; i < 10u; j++) {
        acc ^= j;
    }
    return acc;
}
