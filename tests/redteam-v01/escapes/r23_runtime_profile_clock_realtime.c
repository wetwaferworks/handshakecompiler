/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "hc_contract.h"

HS_PHASE_COMMIT
uint64_t r23_runtime_wallclock(uint64_t phase)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return hc_fnv1a64_step(phase, (uint64_t)ts.tv_sec ^ (uint64_t)time(NULL));
}
