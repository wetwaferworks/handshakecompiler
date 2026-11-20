// SPDX-License-Identifier: MIT
readonly struct Snap { uint64_t phase; };

phase pull fn_bad_cast(const Snap *s) -> uint64_t {
    Snap *w = (Snap *)s;
    w->phase += 1u;
    return w->phase;
}
