// SPDX-License-Identifier: MIT
#include <sys/time.h>
shared uint64_t g_ledger = 0;

phase commit fn_bad_time(uint64_t phase) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    g_ledger ^= phase ^ (uint64_t)tv.tv_usec;
}
