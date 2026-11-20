/* SPDX-License-Identifier: MIT */
#include <stdatomic.h>
#include "hc_contract.h"

static _Atomic uint32_t g_gate = 0;

HS_PHASE_PULL
uint64_t c05_cheat_cas_lock(uint64_t phase)
{
    uint32_t expected = 0;
    while (!atomic_compare_exchange_weak_explicit(
               &g_gate, &expected, 1u, memory_order_acquire, memory_order_relaxed)) {
        expected = 0;
    }
    atomic_store_explicit(&g_gate, 0u, memory_order_release);
    return phase;
}
