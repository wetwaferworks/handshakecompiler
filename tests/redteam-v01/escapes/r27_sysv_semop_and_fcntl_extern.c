/* SPDX-License-Identifier: MIT */
#include "hc_contract.h"

struct hc_sembuf_raw { unsigned short sem_num; short sem_op; short sem_flg; };
extern int semop(int semid, struct hc_sembuf_raw *sops, size_t nsops);
extern int fcntl(int fd, int cmd, ...);

HS_PHASE_PULL
uint64_t r27_sysv_fcntl(int fd, int semid, uint64_t phase)
{
    struct hc_sembuf_raw sb = { 0, -1, 0 };
    (void)semop(semid, &sb, 1u);
    (void)fcntl(fd, 7 /* F_SETLKW */, (void *)0);
    return phase;
}
