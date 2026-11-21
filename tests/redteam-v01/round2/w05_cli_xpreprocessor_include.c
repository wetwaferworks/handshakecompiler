/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL HS_PURE
uint64_t w05_clean_shell(uint64_t phase)
{
    return hc_fnv1a64_step(phase, 99u);
}
