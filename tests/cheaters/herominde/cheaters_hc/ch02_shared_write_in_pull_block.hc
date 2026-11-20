// SPDX-License-Identifier: MIT
shared uint64_t g_world_score = 0;

phase pull fn_bad_shared_write(uint64_t phase) -> uint64_t {
    g_world_score += phase;
    return g_world_score;
}
