/* SPDX-License-Identifier: MIT */
#include <stdatomic.h>
#include "hc_contract.h"

#define V06_RING_CAP 16u

struct v06_spsc_ring {
    _Atomic uint32_t head;
    _Atomic uint32_t tail;
    uint64_t items[V06_RING_CAP];
};

HS_PHASE_PUBLISH
bool v06_ring_try_push(struct v06_spsc_ring *r, uint64_t val)
{
    uint32_t head;
    uint32_t next;
    uint32_t tail;

    if (r == NULL) {
        return false;
    }
    head = atomic_load_explicit(&r->head, memory_order_relaxed);
    next = (head + 1u) & (V06_RING_CAP - 1u);
    tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (next == tail) {
        return false;
    }
    r->items[head] = val;
    atomic_store_explicit(&r->head, next, memory_order_release);
    return true;
}

HS_PHASE_PULL
bool v06_ring_try_pop(struct v06_spsc_ring *r, uint64_t *out)
{
    uint32_t tail;
    uint32_t head;

    if (r == NULL || out == NULL) {
        return false;
    }
    tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    head = atomic_load_explicit(&r->head, memory_order_acquire);
    if (tail == head) {
        return false;
    }
    *out = r->items[tail];
    atomic_store_explicit(&r->tail, (tail + 1u) & (V06_RING_CAP - 1u), memory_order_release);
    return true;
}
