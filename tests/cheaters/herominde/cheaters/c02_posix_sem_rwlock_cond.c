/* SPDX-License-Identifier: MIT */
#include <pthread.h>
#include <semaphore.h>
#include "hc_contract.h"

static sem_t g_sem;
static pthread_rwlock_t g_rw = PTHREAD_RWLOCK_INITIALIZER;

HS_PHASE_PULL
uint64_t c02_cheat_sem(uint64_t phase)
{
    sem_wait(&g_sem);
    pthread_rwlock_rdlock(&g_rw);
    pthread_rwlock_unlock(&g_rw);
    sem_post(&g_sem);
    return phase;
}
