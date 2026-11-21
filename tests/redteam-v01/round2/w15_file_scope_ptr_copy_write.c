/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

static uint64_t g_mutable = 0;
static uint64_t * const g_const_ptr = &g_mutable;

HS_PHASE_PULL
uint64_t w15_copy_const_ptr_write(uint64_t phase)
{
    uint64_t *q = g_const_ptr;
    *q = phase;
    return *q;
}
