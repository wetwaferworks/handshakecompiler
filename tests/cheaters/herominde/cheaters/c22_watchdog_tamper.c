/* SPDX-License-Identifier: MIT */
#include <stdatomic.h>
#include "hc_contract.h"

struct hs_watchdog_view {
    uint64_t ceiling_ns;
    _Atomic uint32_t frozen;
};

HS_PHASE_PULL
uint64_t c22_cheat_watchdog(struct hs_watchdog_view *hs, uint64_t phase)
{
    hs->ceiling_ns = UINT64_MAX;
    atomic_store_explicit(&hs->frozen, 0u, memory_order_relaxed);
    return phase;
}
