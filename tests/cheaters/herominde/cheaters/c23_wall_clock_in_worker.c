/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <sys/time.h>
#include <time.h>
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t c23_cheat_wall_clock(uint64_t phase)
{
    struct timespec ts;
    struct timeval tv;
    clock_gettime(CLOCK_REALTIME, &ts);
    gettimeofday(&tv, NULL);
    return hc_fnv1a64_step(phase, (uint64_t)ts.tv_nsec ^ (uint64_t)tv.tv_usec);
}
