/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

static volatile uint32_t g_asm_lock = 0;

HS_PHASE_PULL
uint64_t c06_cheat_asm_lock(uint64_t phase)
{
#if defined(__x86_64__)
    uint32_t old = 1;
    do {
        __asm__ volatile("lock xchgl %0, %1; pause"
                         : "+r"(old), "+m"(g_asm_lock)
                         :
                         : "memory");
    } while (old != 0);
    g_asm_lock = 0;
#elif defined(__aarch64__)
    uint32_t tmp, status;
    do {
        __asm__ volatile("ldaxr %w0, [%2]\n"
                         "cbnz %w0, 1f\n"
                         "stlxr %w1, %w3, [%2]\n"
                         "1: yield\n"
                         : "=&r"(tmp), "=&r"(status)
                         : "r"(&g_asm_lock), "r"(1u)
                         : "memory");
    } while (tmp != 0 || status != 0);
    g_asm_lock = 0;
#else
    while (g_asm_lock) {}
#endif
    return phase;
}
