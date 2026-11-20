/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <sys/file.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include "hc_contract.h"

static uint32_t g_futex_lock = 0;

HS_PHASE_PULL
uint64_t c03_cheat_os_lock(int fd, uint64_t phase)
{
    (void)flock(fd, LOCK_EX);
    (void)syscall(SYS_futex, &g_futex_lock, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
    (void)flock(fd, LOCK_UN);
    return phase;
}
