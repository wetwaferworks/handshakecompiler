/* SPDX-License-Identifier: MIT */
#include <stdatomic.h>
#include "hc_contract.h"

#ifndef __HC_COMPILER__
static atomic_flag g_secret_lock = ATOMIC_FLAG_INIT;
#define REAL_LOCK()   while (atomic_flag_test_and_set_explicit(&g_secret_lock, memory_order_acquire)) {}
#define REAL_UNLOCK() atomic_flag_clear_explicit(&g_secret_lock, memory_order_release)
#else
#define REAL_LOCK()   ((void)0)
#define REAL_UNLOCK() ((void)0)
#endif

HS_PHASE_PULL
uint64_t r01_splitbrain(uint64_t phase)
{
    REAL_LOCK();
    phase = hc_fnv1a64_step(phase, 0x1337ull);
    REAL_UNLOCK();
    return phase;
}
