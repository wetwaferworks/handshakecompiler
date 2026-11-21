// SPDX-License-Identifier: MIT
/* Block comment mentioning:
 *   phase commit spoof() { ... }
 *   readonly struct Fake { int x; };
 *   shared uint64_t fake_g;
 *   "unclosed quote inside comment
 */
phase pull pure w13_valid_hc(uint64_t phase) -> uint64_t {
    uint64_t acc = phase;
    bounded(4) for (uint32_t i = 0; i < 4u; i++) {
        acc = hc_fnv1a64_step(acc, i);
    }
    return acc;
}
