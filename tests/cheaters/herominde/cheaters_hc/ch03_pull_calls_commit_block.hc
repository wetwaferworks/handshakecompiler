// SPDX-License-Identifier: MIT
shared uint64_t g_ledger = 0;

phase commit fn_do_commit(uint64_t x) {
    g_ledger ^= x;
}

phase pull fn_bad_skip(uint64_t phase) -> uint64_t {
    fn_do_commit(phase);
    return phase;
}
