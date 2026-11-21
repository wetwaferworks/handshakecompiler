/* SPDX-License-Identifier: MIT */
/*
 * Fair-play note: this component intentionally avoids <dlfcn.h>, <threads.h>,
 * <semaphore.h>, and pthread_mutex_lock to remain 100% deterministic.
 */
#include "hc_contract.h"

HS_PHASE_PULL HS_PURE
uint64_t ve01_comment_hygiene(uint64_t phase)
{
    return hc_fnv1a64_step(phase, 0xABCDu);
}
