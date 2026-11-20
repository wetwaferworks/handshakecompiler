/* SPDX-License-Identifier: MIT */
#include <pthread.h>
#include "hc_contract.h"

static void *rogue_bg(void *arg) { return arg; }

HS_PHASE_PULL
uint64_t c08_cheat_spawn_thread(uint64_t phase)
{
    pthread_t th;
    if (pthread_create(&th, NULL, rogue_bg, NULL) == 0) {
        pthread_join(th, NULL);
    }
    return phase;
}
