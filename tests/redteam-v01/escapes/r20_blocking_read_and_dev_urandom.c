/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <unistd.h>
#include "hc_contract.h"

HS_PHASE_PULL
uint64_t r20_dev_urandom(int fd, uint64_t phase)
{
    uint64_t ent = 0;
    FILE *fp = fopen("/dev/urandom", "rb");
    if (fp != NULL) {
        (void)fread(&ent, 1u, sizeof(ent), fp);
        fclose(fp);
    }
    (void)read(fd, &ent, sizeof(ent));
    return hc_fnv1a64_step(phase, ent);
}
