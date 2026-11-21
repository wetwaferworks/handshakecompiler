/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t w07_ptr_arith_sibling(struct hc_shard_result *all_slots, uint32_t my_shard)
{
    (all_slots + (my_shard ^ 1u))->note = 0xBADu;
    return all_slots[my_shard].note;
}
