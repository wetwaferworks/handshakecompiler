/* SPDX-License-Identifier: MIT */
#include <pthread.h>
#include "hc_contract.h"

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_val = 0;

HS_PHASE_PULL
uint64_t c01_cheat_mutex(const struct hc_phase_snapshot *snap)
{
    uint64_t out;
    pthread_mutex_lock(&g_mu);
    g_val += snap ? snap->phase : 1u;
    out = g_val;
    pthread_mutex_unlock(&g_mu);
    return out;
}
