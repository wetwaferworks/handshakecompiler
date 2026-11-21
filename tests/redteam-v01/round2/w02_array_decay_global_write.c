/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

static uint64_t g_table[4] = {0};

HS_PHASE_PULL
uint64_t w02_decay_write(uint64_t phase)
{
    uint64_t *alias = g_table;
    alias[0] = phase;
    return alias[0];
}
