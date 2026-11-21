/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t r12_static_local(uint64_t phase)
{
    static uint64_t s_hidden_shared = 0;
    s_hidden_shared += phase;
    return s_hidden_shared;
}
