/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

static volatile uint32_t g_ctor_lock = 0;

__attribute__((constructor))
static void r03_spoof_init(void)
{
#if defined(__x86_64__)
    uint32_t old = 1;
    do {
        __asm__ volatile("lock xchgl %0, %1; pause" : "+r"(old), "+m"(g_ctor_lock) :: "memory");
    } while (old != 0);
    g_ctor_lock = 0;
#endif
}

HS_PHASE_PULL HS_PURE
uint64_t r03_innocent_pull(uint64_t phase)
{
    return hc_fnv1a64_step(phase, 1u);
}
