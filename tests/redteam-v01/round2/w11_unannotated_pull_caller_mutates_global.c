/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

static uint64_t g_shared = 0;

/* Uses standard memcpy into a const pointer param */
HS_PHASE_PULL
uint64_t w11_const_cast_via_bsearch_or_memchr(const struct hc_phase_snapshot *HS_READONLY snap)
{
    extern void *memchr(const void *s, int c, size_t n);
    unsigned char first = *(const unsigned char *)snap;
    struct hc_phase_snapshot *rw = (struct hc_phase_snapshot *)memchr(snap, (int)first, sizeof(*snap));
    if (rw) rw->phase++;
    return g_shared;
}
