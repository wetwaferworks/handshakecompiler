/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL HS_PURE
uint64_t v01_pure_pull_worker(const struct hc_phase_snapshot *HS_DISJOINT snap,
                              struct hc_shard_result *HS_DISJOINT out)
{
    uint64_t acc = 0xcbf29ce484222325ull;
    uint32_t i;

    if (snap == NULL || out == NULL) {
        return 0;
    }
    acc = hc_fnv1a64_step(acc, snap->phase);
    acc = hc_fnv1a64_step(acc, snap->cycle);
    for (i = 0; i < 16u; i++) {
        acc = hc_fnv1a64_step(acc, (uint64_t)(snap->seed + i));
    }
    out->note = acc;
    out->work_units = 16u;
    return acc;
}
