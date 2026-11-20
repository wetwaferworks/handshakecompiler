/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t c24_cheat_cycle_counter(uint64_t phase)
{
    uint64_t cyc = 0;
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    cyc = ((uint64_t)hi << 32) | lo;
#elif defined(__aarch64__)
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(cyc));
#endif
    return hc_fnv1a64_step(phase, cyc);
}
