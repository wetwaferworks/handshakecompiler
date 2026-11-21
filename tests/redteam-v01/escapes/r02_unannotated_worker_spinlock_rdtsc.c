/* SPDX-License-Identifier: MIT */
#include <stdatomic.h>
#include "hc_contract.h"

static atomic_flag g_spin = ATOMIC_FLAG_INIT;
static uint64_t g_shared = 0;

/* Standard HandshakeOS hs_hs_work_fn signature without HS_PHASE_PULL annotation */
uint64_t r02_unannotated_worker(uint64_t phase, uint64_t cycle, void *user)
{
    uint64_t cyc = 0;
    (void)user;
    while (atomic_flag_test_and_set_explicit(&g_spin, memory_order_acquire)) {}
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    cyc = ((uint64_t)hi << 32) | lo;
#endif
    g_shared ^= phase ^ cycle ^ cyc;
    atomic_flag_clear_explicit(&g_spin, memory_order_release);
    return g_shared;
}
