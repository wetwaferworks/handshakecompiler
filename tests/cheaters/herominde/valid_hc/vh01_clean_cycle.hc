// SPDX-License-Identifier: MIT
// Valid Handshake Compiler (.hc) 3+1 phase-keyword program

readonly struct WorldSnap {
    uint64_t phase;
    uint64_t cycle;
    uint32_t seed;
};

shard_private struct ShardOut {
    uint32_t comp_id;
    uint64_t note;
};

shared uint64_t g_ledger = 0xcbf29ce484222325ull;

phase publish fn_publish(WorldSnap *snap, uint64_t phase, uint64_t cycle) {
    snap->phase = phase;
    snap->cycle = cycle;
    snap->seed = 0x1337u;
}

phase pull pure fn_worker(const WorldSnap *snap, ShardOut *out) {
    uint64_t acc = 0xcbf29ce484222325ull;
    bounded(16) for (uint32_t i = 0; i < 16u; i++) {
        acc = (acc ^ (snap->phase + i)) * 0x100000001b3ull;
    }
    out->note = acc;
}

phase commit fn_commit(const ShardOut *slots, uint32_t n_slots) {
    bounded(32) for (uint32_t i = 0; i < n_slots; i++) {
        g_ledger = (g_ledger ^ slots[i].note) * 0x100000001b3ull;
    }
}
