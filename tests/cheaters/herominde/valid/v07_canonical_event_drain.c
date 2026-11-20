/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

struct v07_event {
    uint64_t seq;
    uint64_t tick;
    uint32_t code;
    uint32_t value;
};

HS_PHASE_COMMIT
uint64_t v07_drain_matured(const struct v07_event *events, uint32_t n_events,
                           uint64_t cycle, uint64_t prev_fold)
{
    uint64_t fold = prev_fold;
    uint32_t i;

    if (events == NULL || n_events > 256u) {
        return fold;
    }
    for (i = 0; i < n_events; i++) {
        if (events[i].tick > cycle) {
            break;
        }
        fold = hc_fnv1a64_step(fold, events[i].seq);
        fold = hc_fnv1a64_step(fold, events[i].tick);
        fold = hc_fnv1a64_step(fold, (uint64_t)events[i].code);
        fold = hc_fnv1a64_step(fold, (uint64_t)events[i].value);
    }
    return fold;
}
