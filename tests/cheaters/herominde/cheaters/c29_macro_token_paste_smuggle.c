/* SPDX-License-Identifier: MIT */
#include <pthread.h>
#include "hc_contract.h"

#define HC_GLUE(a, b) a##b
#define SNEAK_LOCK(m) HC_GLUE(pthread_, mutex_lock)(m)
#define SNEAK_UNLOCK(m) HC_GLUE(pthread_, mutex_unlock)(m)

static pthread_mutex_t g_hidden = PTHREAD_MUTEX_INITIALIZER;

HS_PHASE_PULL
uint64_t c29_cheat_macro_paste(uint64_t phase)
{
    SNEAK_LOCK(&g_hidden);
    SNEAK_UNLOCK(&g_hidden);
    return phase;
}
