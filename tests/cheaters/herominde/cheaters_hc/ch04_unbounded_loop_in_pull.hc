// SPDX-License-Identifier: MIT
phase pull fn_bad_starve(uint64_t phase) -> uint64_t {
    uint64_t acc = phase;
    while (1) {
        acc ^= 1ull;
    }
    return acc;
}
