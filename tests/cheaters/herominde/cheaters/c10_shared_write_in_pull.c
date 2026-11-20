/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_SHARED static uint64_t g_shared_world_counter = 0;

HS_PHASE_PULL
uint64_t c10_cheat_pull_write(const struct hc_phase_snapshot *snap)
{
    g_shared_world_counter += snap ? snap->phase : 1u;
    return g_shared_world_counter;
}
