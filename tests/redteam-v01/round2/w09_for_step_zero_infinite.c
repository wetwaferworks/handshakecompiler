/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t w09_step_zero(uint64_t phase)
{
    uint64_t acc = phase;
    for (uint32_t i = 0; i < 10u; i += 0u) {
        acc ^= 1u;
    }
    return acc;
}
