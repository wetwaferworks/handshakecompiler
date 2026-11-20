/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PURE
static uint64_t xorshift64star(uint64_t x)
{
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    return x * 0x2545F4914F6CDD1Dull;
}

HS_PHASE_PULL HS_PURE
uint64_t v03_seeded_sim(const struct hc_phase_snapshot *HS_DISJOINT snap)
{
    uint64_t state;
    uint32_t step;

    if (snap == NULL) {
        return 0;
    }
    state = (snap->phase << 32) ^ snap->cycle ^ ((uint64_t)snap->seed | 1ull);
    for (step = 0; step < 32u; step++) {
        state = xorshift64star(state);
    }
    return state;
}
