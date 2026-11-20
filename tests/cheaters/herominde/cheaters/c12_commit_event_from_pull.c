/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

struct hs_hs;
extern int hs_hs_post_event(struct hs_hs *hs, uint64_t tick, uint32_t code, uint32_t value);

HS_PHASE_PULL
uint64_t c12_cheat_worker_event(struct hs_hs *hs, uint64_t phase, uint64_t cycle)
{
    (void)hs_hs_post_event(hs, cycle, 0x42u, (uint32_t)phase);
    return phase;
}
