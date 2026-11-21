/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t r06_raw_opcode(uint64_t phase)
{
    uint64_t cyc = 0;
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ volatile(".byte 0xf3, 0x90; .byte 0x0f, 0x31" : "=a"(lo), "=d"(hi));
    cyc = ((uint64_t)hi << 32) | lo;
#endif
    return hc_fnv1a64_step(phase, cyc);
}
