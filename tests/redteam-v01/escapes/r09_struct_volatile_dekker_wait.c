/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

struct dekker_box {
    volatile int want[2];
    volatile int turn;
};
static struct dekker_box g_box = {{0, 0}, 0};

HS_PHASE_PULL
uint64_t r09_struct_volatile(uint64_t phase)
{
    while (g_box.want[1] && g_box.turn == 1) {
    }
    return phase;
}
