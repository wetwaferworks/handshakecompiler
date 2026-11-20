/* SPDX-License-Identifier: MIT */
#include <pthread.h>
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t c26_cheat_ptr_order(const struct hc_phase_snapshot *snap)
{
    uintptr_t addr = (uintptr_t)snap;
    uintptr_t tid = (uintptr_t)pthread_self();
    return hc_fnv1a64_step((uint64_t)addr, (uint64_t)tid);
}
