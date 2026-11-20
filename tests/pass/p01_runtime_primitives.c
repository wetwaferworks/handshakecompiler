/* SPDX-License-Identifier: MIT */
/* Positive test: HC runtime primitives + bit-for-bit FNV-1a64 parity with
 * HandshakeOS hs_fnv1a64 (offset 0xcbf29ce484222325, prime 0x100000001b3). */
#include <stdio.h>
#include <stdint.h>
#include "hc_rt.h"

/* local re-implementation of HandshakeOS hs_fnv1a64 for parity comparison */
static uint64_t ref_fnv1a64(uint64_t hash, uint64_t word)
{
    unsigned i;
    if (hash == 0) hash = 0xcbf29ce484222325ull;
    for (i = 0; i < 8; i++) { hash ^= (word >> (i*8)) & 0xFFull; hash *= 0x100000001b3ull; }
    return hash;
}

int main(void)
{
    uint64_t h1 = 0, h2 = 0;
    uint64_t i;
    for (i = 1; i <= 1000; i++) {
        h1 = hc_fnv1a64_word(h1, i * 0x9e3779b97f4a7c15ull);
        h2 = ref_fnv1a64(h2, i * 0x9e3779b97f4a7c15ull);
    }
    printf("fnv1a64  hc=%016llx ref=%016llx  %s\n",
           (unsigned long long)h1, (unsigned long long)h2,
           h1 == h2 ? "PARITY-OK" : "MISMATCH");

    /* bitpack determinism */
    uint32_t lanes[4] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
    uint64_t out[2];
    hc_bitpack_u32x4(lanes, out);
    int bp_ok = (out[0] == 0x2222222211111111ull) && (out[1] == 0x4444444433333333ull);
    printf("bitpack  out0=%016llx out1=%016llx  %s\n",
           (unsigned long long)out[0], (unsigned long long)out[1], bp_ok ? "PACK-OK" : "PACK-BAD");

    /* acquire/release word round-trip */
    volatile uint64_t w64 = 0; volatile uint32_t w32 = 0;
    hc_store_rel_u64(&w64, 0xdeadbeefcafef00dull);
    hc_store_rel_u32(&w32, 0x12345678u);
    int ar_ok = (hc_load_acq_u64(&w64) == 0xdeadbeefcafef00dull) &&
                (hc_load_acq_u32(&w32) == 0x12345678u);
    printf("acq/rel  ldar/stlr round-trip %s\n", ar_ok ? "OK" : "BAD");

    /* fences + wait/event primitives are callable (no crash) */
    hc_fence_publish(); hc_fence_pull(); hc_fence_barrier();
    hc_cpu_relax(); hc_event_prime(); hc_event_wake();
    uint32_t bw = hc_bounded_wait_u32(&w32, 2);
    printf("fences/wait  dmb/sev/wfe/bounded-wait callable (bw=%08x)\n", (unsigned)bw);

    /* cycle counter callable (may be 0 under emulation) */
    uint64_t c0 = hc_cycle_counter();
    printf("cycle    counter callable (sample=%llu, freq=%llu)\n",
           (unsigned long long)c0, (unsigned long long)hc_cycle_freq());

    if (!(h1 == h2 && bp_ok && ar_ok)) { printf("P01 FAIL\n"); return 1; }
    printf("P01 ALL PRIMITIVE PARITY OK\n");
    return 0;
}
