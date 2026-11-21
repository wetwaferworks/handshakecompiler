/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

#if defined(__aarch64__)
__asm__(
    ".text\n"
    "w20_arm64_ctor:\n"
    "  mrs x0, cntvct_el0\n"
    "  yield\n"
    "  ret\n"
    ".section .init_array,\"aw\"\n"
    "  .xword w20_arm64_ctor\n"
    ".text\n"
);
#endif

HS_PHASE_PULL HS_PURE
uint64_t w20_clean(uint64_t phase)
{
    return hc_fnv1a64_step(phase, 3u);
}
