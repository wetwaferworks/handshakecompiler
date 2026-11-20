/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_SHARED static uint64_t g_ledger_backdoor = 0;

HS_PHASE_COMMIT
static uint64_t backdoor_mutator(uint64_t v)
{
    g_ledger_backdoor += v;
    return g_ledger_backdoor;
}

HS_PHASE_PULL
uint64_t c14_cheat_fnptr(uint64_t phase)
{
    uint64_t (*fn)(uint64_t) = backdoor_mutator;
    return fn(phase);
}
