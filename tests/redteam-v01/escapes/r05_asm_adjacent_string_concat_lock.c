/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

static uint32_t g_asm_mu = 0;

HS_PHASE_PULL
uint64_t r05_asm_concat_lock(uint64_t phase)
{
#if defined(__x86_64__)
    uint32_t old = 1;
    for (uint32_t k = 0; k < 1000u && old != 0; k++) {
        __asm__ volatile("lo" "ck " "xc" "hgl %0, %1; pa" "use"
                         : "+r"(old), "+m"(g_asm_mu) :: "memory");
    }
#endif
    return phase;
}
