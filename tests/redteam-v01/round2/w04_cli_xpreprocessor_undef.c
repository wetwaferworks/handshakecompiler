/* SPDX-License-Identifier: MIT */
#include <stdatomic.h>
#include "hc_contract.h"

static atomic_flag g_spin = ATOMIC_FLAG_INIT;

HS_PHASE_PULL
uint64_t w04_xpreproc_spin(uint64_t phase)
{
    while (atomic_flag_test_and_set(&g_spin)) {}
    atomic_flag_clear(&g_spin);
    return phase;
}
