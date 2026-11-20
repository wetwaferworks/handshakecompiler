// SPDX-License-Identifier: MIT
shared uint64_t g_event_fold = 0xcbf29ce484222325ull;

phase pull pure fn_seeded_step(uint64_t phase, uint64_t cycle) -> uint64_t {
    uint64_t s = (phase << 32) ^ cycle ^ 1ull;
    bounded(8) for (uint32_t k = 0; k < 8u; k++) {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        s *= 0x2545F4914F6CDD1Dull;
    }
    return s;
}

phase commit fn_apply_event(uint64_t seq, uint64_t tick, uint32_t code, uint32_t val) {
    g_event_fold = (g_event_fold ^ seq ^ tick ^ ((uint64_t)code << 32) ^ val) * 0x100000001b3ull;
}
