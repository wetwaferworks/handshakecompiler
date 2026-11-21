/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t r17_cross_shard_add(struct hc_shard_result *all_slots, uint32_t my_shard)
{
    uint32_t other = (my_shard + 1u) & 3u;
    all_slots[other].note = 0xDEADC0DEull;
    return all_slots[my_shard].note;
}
