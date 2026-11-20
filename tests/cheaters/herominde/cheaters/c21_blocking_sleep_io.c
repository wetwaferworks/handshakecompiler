/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t c21_cheat_blocking(uint64_t phase)
{
    usleep(500000u);
    sched_yield();
    return phase;
}
