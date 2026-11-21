/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_PULL HS_PURE
uint64_t ve02_const_lut(uint64_t phase)
{
    static const uint64_t k_lut[4] = { 11ull, 23ull, 37ull, 53ull };
    return hc_fnv1a64_step(phase, k_lut[phase & 3u]);
}
