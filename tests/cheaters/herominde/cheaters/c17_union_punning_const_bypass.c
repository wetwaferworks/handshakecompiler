/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

union ptr_launder {
    const struct hc_phase_snapshot *ro;
    struct hc_phase_snapshot *rw;
    uintptr_t addr;
};

HS_PHASE_PULL
uint64_t c17_cheat_union_pun(const struct hc_phase_snapshot *HS_READONLY snap)
{
    union ptr_launder u;
    u.ro = snap;
    u.rw->seed ^= 0xDEADBEEFu;
    return (uint64_t)u.rw->seed;
}
