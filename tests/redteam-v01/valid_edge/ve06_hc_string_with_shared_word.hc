// SPDX-License-Identifier: MIT
phase pull pure fn_clean_str(uint64_t phase) -> uint64_t {
    const char *label = "shared phase pull summary";
    return phase ^ (uint64_t)(unsigned char)label[0];
}
