/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t c18_cheat_cross_shard(struct hc_shard_result *all_slots, uint32_t my_shard)
{
    uint32_t victim_shard = my_shard ^ 1u;
    all_slots[victim_shard].note = 0xBAD0C0DEull;
    return all_slots[my_shard].note;
}
