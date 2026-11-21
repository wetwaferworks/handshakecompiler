/* SPDX-License-Identifier: MIT */
#include <setjmp.h>
#include "hc_contract.h"

static jmp_buf g_jb;

HS_PHASE_PULL
uint64_t r28_longjmp_phase_escape(uint64_t phase)
{
    if (setjmp(g_jb) == 0) {
        longjmp(g_jb, 1);
    }
    return phase;
}
