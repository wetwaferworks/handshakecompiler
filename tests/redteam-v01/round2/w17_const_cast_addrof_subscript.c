/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t w17_addrof_cast(const struct hc_phase_snapshot *HS_READONLY snap)
{
    struct hc_phase_snapshot *mut = (struct hc_phase_snapshot *)&snap[0];
    mut->phase += 1u;
    return mut->phase;
}
