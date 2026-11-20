/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include "hc_contract.h"

HS_PHASE_PULL HS_PURE
uint64_t worker(const struct hc_phase_snapshot *HS_DISJOINT snap,
                struct hc_shard_result *HS_DISJOINT out)
{
    uint64_t acc = 0xcbf29ce484222325ull;
    uint32_t i;
    if (!snap || !out) return 0;
    acc = hc_fnv1a64_step(acc, snap->phase);
    for (i = 0; i < 8u; i++) acc = hc_fnv1a64_step(acc, (uint64_t)(snap->seed + i));
    out->note = acc;
    return acc;
}

int main(void)
{
    struct hc_phase_snapshot s = {1, 1, 7, 3, 0, 42};
    struct hc_shard_result r = {0,0,0,0};
    uint64_t n = worker(&s, &r);
    printf("p02 worker note=%016llx\n", (unsigned long long)n);
    return n == r.note ? 0 : 1;
}
