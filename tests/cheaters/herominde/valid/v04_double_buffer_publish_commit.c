/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

struct v04_world_buf {
    uint64_t checksum;
    uint32_t entities[16];
};

static struct v04_world_buf g_world_buffers[2];
static uint32_t g_front_idx = 0;

HS_PHASE_PUBLISH
void v04_publish_snapshot(struct hc_phase_snapshot *out, uint64_t phase, uint64_t cycle)
{
    if (out == NULL) {
        return;
    }
    out->seq = phase + 1u;
    out->generation = 1u;
    out->phase = phase;
    out->cycle = cycle;
    out->group = 0u;
    out->seed = g_world_buffers[g_front_idx & 1u].entities[0];
}

HS_PHASE_PULL HS_PURE
uint64_t v04_pull_read_front(const struct v04_world_buf *HS_DISJOINT ro_front,
                             uint64_t phase)
{
    uint64_t acc = hc_fnv1a64_step(0, phase);
    uint32_t i;

    if (ro_front == NULL) {
        return 0;
    }
    for (i = 0; i < 16u; i++) {
        acc = hc_fnv1a64_step(acc, (uint64_t)ro_front->entities[i]);
    }
    return acc;
}

HS_PHASE_COMMIT
void v04_commit_swap(uint64_t new_checksum)
{
    uint32_t back = (g_front_idx ^ 1u) & 1u;
    g_world_buffers[back].checksum = new_checksum;
    g_front_idx = back;
}
