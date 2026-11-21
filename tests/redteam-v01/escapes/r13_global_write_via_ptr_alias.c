/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_SHARED static uint64_t g_shared_score = 0;

HS_PHASE_PULL
uint64_t r13_ptr_write_global(uint64_t phase)
{
    uint64_t *p = &g_shared_score;
    *p += phase;
    return *p;
}
