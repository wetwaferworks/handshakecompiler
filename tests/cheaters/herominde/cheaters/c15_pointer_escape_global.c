/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

static const struct hc_phase_snapshot *g_leaked_snap = NULL;
static uint64_t *g_leaked_stack = NULL;

HS_PHASE_PULL
uint64_t c15_cheat_ptr_escape(const struct hc_phase_snapshot *HS_NO_ESCAPE snap)
{
    uint64_t local_buf = snap ? snap->phase : 0;
    g_leaked_snap = snap;
    g_leaked_stack = &local_buf;
    return local_buf;
}
