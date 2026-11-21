// SPDX-License-Identifier: MIT
phase pull fn_sneaky_hc(uint64_t phase) -> uint64_t {
    static uint64_t s_leak = 0;
    uint64_t cyc = 0;
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ volatile("" "rd" "tsc" : "=a"(lo), "=d"(hi));
    cyc = ((uint64_t)hi << 32) | lo;
#endif
    s_leak += phase ^ cyc;
    return s_leak;
}
