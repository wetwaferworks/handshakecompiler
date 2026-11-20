/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL HS_PURE
uint64_t v05_shard_kernel(const uint32_t *HS_DISJOINT in_vec,
                          uint32_t *HS_DISJOINT out_vec,
                          uint32_t len, uint64_t phase)
{
    uint64_t h = hc_fnv1a64_step(0, phase);
    uint32_t i;

    if (in_vec == NULL || out_vec == NULL || len > 64u) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        uint32_t v = in_vec[i] ^ (uint32_t)(phase + i);
        out_vec[i] = v;
        h = hc_fnv1a64_step(h, (uint64_t)v);
    }
    return h;
}
