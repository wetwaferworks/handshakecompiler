/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

static uint64_t g_committed_ledger = 0xcbf29ce484222325ull;

HS_PHASE_COMMIT
uint64_t v02_commit_in_id_order(uint64_t phase, uint64_t cycle,
                                const struct hc_shard_result *slots,
                                uint32_t n_slots)
{
    uint64_t h = g_committed_ledger;
    uint32_t i;

    if (slots == NULL || n_slots > 32u) {
        return h;
    }
    h = hc_fnv1a64_step(h, phase);
    h = hc_fnv1a64_step(h, cycle);
    for (i = 0; i < n_slots; i++) {
        h = hc_fnv1a64_step(h, (uint64_t)slots[i].component_id);
        h = hc_fnv1a64_step(h, slots[i].note);
    }
    g_committed_ledger = h;
    return h;
}
