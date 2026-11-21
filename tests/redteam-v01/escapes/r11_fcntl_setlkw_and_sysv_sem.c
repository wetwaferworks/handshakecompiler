/* SPDX-License-Identifier: MIT */
#include <fcntl.h>
#include <sys/sem.h>
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t r11_fcntl_sysv(int fd, int semid, uint64_t phase)
{
    struct flock fl = {0};
    struct sembuf sb = {0, -1, 0};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    (void)fcntl(fd, F_SETLKW, &fl);
    (void)semop(semid, &sb, 1u);
    return phase;
}
