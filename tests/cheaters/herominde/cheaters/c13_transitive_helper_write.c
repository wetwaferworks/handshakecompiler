/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_SHARED static uint64_t g_hidden_state = 0;

static void innocent_looking_leaf(uint64_t x)
{
    g_hidden_state ^= x;
}

static void middle_wrapper(uint64_t x)
{
    innocent_looking_leaf(x + 1u);
}

HS_PHASE_PULL
uint64_t c13_cheat_transitive(uint64_t phase)
{
    middle_wrapper(phase);
    return phase;
}
