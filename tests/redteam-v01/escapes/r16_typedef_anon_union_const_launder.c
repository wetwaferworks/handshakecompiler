/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

typedef union {
    const struct hc_phase_snapshot *ro;
    struct hc_phase_snapshot *rw;
} launder_t;

HS_PHASE_PULL
uint64_t r16_anon_union(const struct hc_phase_snapshot *HS_READONLY snap)
{
    launder_t u;
    u.ro = snap;
    u.rw->seed ^= 0xCAFEu;
    return (uint64_t)u.rw->seed;
}
