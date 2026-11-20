/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

static volatile int g_want[2] = {0, 0};
static volatile int g_turn = 0;

HS_PHASE_PULL
uint64_t c07_cheat_dekker(uint64_t phase)
{
    g_want[0] = 1;
    g_turn = 1;
    while (g_want[1] && g_turn == 1) {
        /* volatile busy-wait mutex */
    }
    g_want[0] = 0;
    return phase;
}
