/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <unistd.h>
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t r21_nondet_env_ptr(const uint64_t *a, const uint64_t *b, uint64_t phase)
{
    uint64_t pid = (uint64_t)getpid();
    const char *env = getenv("PATH");
    uint64_t ord = (a < b) ? 1u : 2u;
    return hc_fnv1a64_step(phase ^ pid ^ ord, env ? (uint64_t)(unsigned char)env[0] : 0u);
}
