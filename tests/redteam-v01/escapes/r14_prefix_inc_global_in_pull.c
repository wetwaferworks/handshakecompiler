/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_SHARED static uint64_t g_counter = 0;

HS_PHASE_PULL
uint64_t r14_prefix_inc(uint64_t phase)
{
    ++g_counter;
    return phase ^ g_counter;
}
