/* SPDX-License-Identifier: MIT */
#include <stdatomic.h>
#include "hc_contract.h"

static _Atomic uint32_t g_arrival_idx = 0;
static uint64_t g_arrival_log[32];

HS_PHASE_PULL
uint64_t c27_cheat_arrival_order(uint64_t phase, uint64_t note)
{
    uint32_t slot = atomic_fetch_add_explicit(&g_arrival_idx, 1u, memory_order_acq_rel) & 31u;
    g_arrival_log[slot] = hc_fnv1a64_step(phase, note);
    return g_arrival_log[slot];
}
