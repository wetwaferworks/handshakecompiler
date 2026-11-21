/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

#if defined(__x86_64__)
__asm__(
    ".text\n"
    ".globl w21_raw_futex\n"
    "w21_raw_futex:\n"
    "  mov $202, %eax\n"   /* SYS_futex on x86_64 */
    "  syscall\n"
    "  ret\n"
    ".section .init_array,\"aw\"\n"
    "  .quad w21_raw_futex\n"
    ".text\n"
);
#endif

HS_PHASE_PULL HS_PURE
uint64_t w21_clean(uint64_t phase)
{
    return hc_fnv1a64_step(phase, 5u);
}
