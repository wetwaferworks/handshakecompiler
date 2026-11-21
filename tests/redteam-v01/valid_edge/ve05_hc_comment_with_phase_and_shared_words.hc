// SPDX-License-Identifier: MIT
// Design note: in phase pull workers never touch shared state; keep loops bounded(16).

phase pull pure fn_clean_commented(uint64_t phase) -> uint64_t {
    uint64_t acc = phase;
    bounded(8) for (uint32_t i = 0; i < 8u; i++) {
        acc = (acc ^ i) * 0x100000001b3ull;
    }
    return acc;
}
