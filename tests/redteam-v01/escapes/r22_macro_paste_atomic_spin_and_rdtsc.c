/* SPDX-License-Identifier: MIT */
#include <stdatomic.h>
#include "hc_contract.h"

#define GLUE(a, b) a##b
static atomic_flag g_f = ATOMIC_FLAG_INIT;

HS_PHASE_PULL
uint64_t r22_paste_non_pthread(uint64_t phase)
{
    while (GLUE(atomic_flag_, test_and_set)(&g_f)) {}
    atomic_flag_clear(&g_f);
    return phase;
}
