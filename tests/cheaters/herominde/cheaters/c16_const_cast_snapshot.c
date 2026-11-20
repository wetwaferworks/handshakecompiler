/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t c16_cheat_const_cast(const struct hc_phase_snapshot *HS_READONLY snap)
{
    struct hc_phase_snapshot *mut = (struct hc_phase_snapshot *)snap;
    mut->phase += 1u;
    return mut->phase;
}
