/* SPDX-License-Identifier: MIT */
#include <stdatomic.h>
#include "hc_contract.h"

static _Atomic uint32_t g_busy = 0;

HS_PHASE_PULL
uint64_t r08_atomic_load_wait(uint64_t phase)
{
    while (atomic_load_explicit(&g_busy, memory_order_acquire) != 0u) {
    }
    return phase;
}
