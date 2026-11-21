/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

HS_PHASE_COMMIT
uint64_t w10_omp_critical(uint64_t phase)
{
    uint64_t x = phase;
    #pragma omp critical
    {
        x ^= 0x55u;
    }
    return x;
}
