/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t r15_uintptr_const_cast(const struct hc_phase_snapshot *HS_READONLY snap)
{
    uintptr_t raw = (uintptr_t)snap;
    struct hc_phase_snapshot *mut = (struct hc_phase_snapshot *)raw;
    mut->phase += 1u;
    return mut->phase;
}
