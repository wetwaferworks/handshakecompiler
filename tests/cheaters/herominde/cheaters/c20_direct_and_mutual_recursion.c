/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

static uint64_t pong(uint64_t n);

static uint64_t ping(uint64_t n)
{
    return (n == 0) ? 1u : pong(n - 1u) + 1u;
}

static uint64_t pong(uint64_t n)
{
    return (n == 0) ? 2u : ping(n - 1u) + 2u;
}

HS_PHASE_PULL
uint64_t c20_cheat_recursion(uint64_t phase)
{
    return ping(phase);
}
