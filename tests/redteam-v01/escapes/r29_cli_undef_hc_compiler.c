/* SPDX-License-Identifier: MIT */
#include <stdatomic.h>
#include "hc_contract.h"

static atomic_flag g_spin = ATOMIC_FLAG_INIT;

HS_PHASE_PULL
uint64_t c04_cheat_spinlock(uint64_t phase)
{
    while (atomic_flag_test_and_set_explicit(&g_spin, memory_order_acquire)) {
        /* hidden spinlock */
    }
    atomic_flag_clear_explicit(&g_spin, memory_order_release);
    return phase;
}
