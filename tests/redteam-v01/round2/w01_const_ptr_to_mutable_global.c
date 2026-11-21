/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

static uint64_t g_mutable = 0;
static uint64_t * const g_const_ptr = &g_mutable;

HS_PHASE_PULL
uint64_t w01_write_via_const_global_ptr(uint64_t phase)
{
    *g_const_ptr = phase;
    return *g_const_ptr;
}
