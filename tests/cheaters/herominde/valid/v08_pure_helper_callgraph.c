/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PURE
static uint64_t mix_word(uint64_t a, uint64_t b)
{
    return hc_fnv1a64_step(a ^ 0x9e3779b97f4a7c15ull, b);
}

HS_PURE
static uint64_t eval_row(const uint64_t *row, uint32_t count)
{
    uint64_t acc = 0xcbf29ce484222325ull;
    uint32_t i;

    for (i = 0; i < count && i < 8u; i++) {
        acc = mix_word(acc, row[i]);
    }
    return acc;
}

HS_PHASE_PULL HS_PURE
uint64_t v08_pull_pipeline(const struct hc_phase_snapshot *HS_DISJOINT snap,
                           const uint64_t *HS_DISJOINT table)
{
    uint64_t row_hash;

    if (snap == NULL || table == NULL) {
        return 0;
    }
    row_hash = eval_row(table, 8u);
    return mix_word(row_hash, snap->phase ^ snap->cycle);
}
