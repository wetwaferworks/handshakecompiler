// SPDX-License-Identifier: MIT
#include <pthread.h>
shared pthread_mutex_t g_mu;

phase pull fn_bad_lock(uint64_t phase) -> uint64_t {
    pthread_mutex_lock(&g_mu);
    pthread_mutex_unlock(&g_mu);
    return phase;
}
