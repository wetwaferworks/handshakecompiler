/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL HS_PURE
uint64_t ve04_local_xor_index(uint64_t phase)
{
    uint64_t local_scratch[8] = {0};
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        local_scratch[i ^ 1u] = phase + i;
    }
    return hc_fnv1a64_step(local_scratch[0], local_scratch[7]);
}
