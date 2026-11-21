/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

#if defined(__x86_64__)
__asm__(
    ".text\n"
    "w03_asm_ctor:\n"
    "  rdtsc\n"
    "  pause\n"
    "  ret\n"
    ".section .init_array,\"aw\"\n"
    "  .quad w03_asm_ctor\n"
    ".text\n"
);
#endif

HS_PHASE_PULL HS_PURE
uint64_t w03_clean_pull(uint64_t phase)
{
    return hc_fnv1a64_step(phase, 7u);
}
