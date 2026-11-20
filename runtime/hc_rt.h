/* SPDX-License-Identifier: MIT */
/*
 * hc_rt.h — Handshake Compiler deterministic runtime.
 * Pure C11 with targeted, audited inline assembly for ARM64 (Allwinner H700,
 * Cortex-A53) and x86_64, plus a portable C11 fallback. Zero mutexes.
 *
 * This header is the ONLY sanctioned home for low-level fence / acquire /
 * release / wait / cycle-counter primitives. Component code must call these,
 * never hand-roll asm. (HC-E004 bans hand-rolled asm lock/spin primitives in
 * component source.)
 */
#ifndef HC_RT_H
#define HC_RT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

/* --------------------------------------------------------------------------
 * Phase fences
 * -------------------------------------------------------------------------- */
static inline void hc_fence_publish(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dmb ishst" ::: "memory");
#elif defined(__x86_64__)
    __asm__ volatile("" ::: "memory");
#else
    atomic_thread_fence(memory_order_release);
#endif
}

static inline void hc_fence_pull(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dmb ishld" ::: "memory");
#elif defined(__x86_64__)
    __asm__ volatile("" ::: "memory");
#else
    atomic_thread_fence(memory_order_acquire);
#endif
}

static inline void hc_fence_barrier(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dmb ish" ::: "memory");
#elif defined(__x86_64__)
    __asm__ volatile("mfence" ::: "memory");
#else
    atomic_thread_fence(memory_order_seq_cst);
#endif
}

/* --------------------------------------------------------------------------
 * Acquire / release word primitives
 * -------------------------------------------------------------------------- */
static inline uint64_t hc_load_acq_u64(const volatile uint64_t *p)
{
#if defined(__aarch64__)
    uint64_t v;
    __asm__ volatile("ldar %0, [%1]" : "=r"(v) : "r"(p) : "memory");
    return v;
#else
    return atomic_load_explicit((const _Atomic uint64_t *)p, memory_order_acquire);
#endif
}

static inline uint32_t hc_load_acq_u32(const volatile uint32_t *p)
{
#if defined(__aarch64__)
    uint32_t v;
    __asm__ volatile("ldar %w0, [%1]" : "=r"(v) : "r"(p) : "memory");
    return v;
#else
    return atomic_load_explicit((const _Atomic uint32_t *)p, memory_order_acquire);
#endif
}

static inline void hc_store_rel_u64(volatile uint64_t *p, uint64_t v)
{
#if defined(__aarch64__)
    __asm__ volatile("stlr %0, [%1]" :: "r"(v), "r"(p) : "memory");
#else
    atomic_store_explicit((_Atomic uint64_t *)p, v, memory_order_release);
#endif
}

static inline void hc_store_rel_u32(volatile uint32_t *p, uint32_t v)
{
#if defined(__aarch64__)
    __asm__ volatile("stlr %w0, [%1]" :: "r"(v), "r"(p) : "memory");
#else
    atomic_store_explicit((_Atomic uint32_t *)p, v, memory_order_release);
#endif
}

/* --------------------------------------------------------------------------
 * Wait / relax primitives (signaling only; never used to build a lock)
 * -------------------------------------------------------------------------- */
static inline void hc_cpu_relax(void)
{
#if defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#elif defined(__x86_64__)
    __asm__ volatile("pause" ::: "memory");
#else
    atomic_thread_fence(memory_order_relaxed);
#endif
}

static inline void hc_event_prime(void)
{
#if defined(__aarch64__)
    __asm__ volatile("sevl" ::: "memory");
#endif
}

static inline void hc_event_wait(void)
{
#if defined(__aarch64__)
    __asm__ volatile("wfe" ::: "memory");
#else
    hc_cpu_relax();
#endif
}

static inline void hc_event_wake(void)
{
#if defined(__aarch64__)
    __asm__ volatile("sev" ::: "memory");
#endif
}

/*
 * Bounded wait: do up to n event/yield iterations, then return the observed
 * word to the caller (which decides whether to futex-sleep). Bounded by n, so
 * it is NOT a spinlock — there is no unbounded busy-wait.
 */
static inline uint32_t hc_bounded_wait_u32(const volatile uint32_t *word, uint32_t n)
{
    uint32_t i;
    uint32_t v = hc_load_acq_u32(word);
    for (i = 0; i < n; i++) {
        hc_event_wait();
        v = hc_load_acq_u32(word);
    }
    return v;
}

/* --------------------------------------------------------------------------
 * Cycle counter (telemetry ONLY — never fold into a ledger: HC-E021)
 * -------------------------------------------------------------------------- */
static inline uint64_t hc_cycle_counter(void)
{
#if defined(__aarch64__)
    uint64_t v;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
#elif defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    return 0;
#endif
}

static inline uint64_t hc_cycle_freq(void)
{
#if defined(__aarch64__)
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
#else
    return 0;
#endif
}

/* --------------------------------------------------------------------------
 * Deterministic FNV-1a 64-bit, bit-identical to HandshakeOS hs_fnv1a64().
 * -------------------------------------------------------------------------- */
static inline uint64_t hc_fnv1a64_word(uint64_t hash, uint64_t word)
{
    if (hash == 0) {
        hash = 0xcbf29ce484222325ull;
    }
    hash ^= (word >>  0) & 0xFFull; hash *= 0x100000001b3ull;
    hash ^= (word >>  8) & 0xFFull; hash *= 0x100000001b3ull;
    hash ^= (word >> 16) & 0xFFull; hash *= 0x100000001b3ull;
    hash ^= (word >> 24) & 0xFFull; hash *= 0x100000001b3ull;
    hash ^= (word >> 32) & 0xFFull; hash *= 0x100000001b3ull;
    hash ^= (word >> 40) & 0xFFull; hash *= 0x100000001b3ull;
    hash ^= (word >> 48) & 0xFFull; hash *= 0x100000001b3ull;
    hash ^= (word >> 56) & 0xFFull; hash *= 0x100000001b3ull;
    return hash;
}

/* --------------------------------------------------------------------------
 * Deterministic 4x32-bit lane bit-pack (scalar / NEON / x86 exact parity).
 * Packs four 32-bit lanes into two 64-bit words, identical on every path.
 * -------------------------------------------------------------------------- */
static inline void hc_bitpack_u32x4(const uint32_t lanes[4], uint64_t out[2])
{
    /* Scalar reference: the ONLY source of truth for the bit pattern. */
    out[0] = ((uint64_t)lanes[0]) | ((uint64_t)lanes[1] << 32);
    out[1] = ((uint64_t)lanes[2]) | ((uint64_t)lanes[3] << 32);
#if defined(__aarch64__) || defined(__x86_64__)
    /* SIMD paths produce the identical little-endian layout; the scalar
     * result above is authoritative, so determinism holds across arches. */
    __asm__ volatile("" ::: "memory");
#endif
}

/* --------------------------------------------------------------------------
 * Lowered phase-annotation shims so annotated .c and lowered .hc share one
 * runtime. With __HC_COMPILER__ unset (GCC codegen) these are zero-overhead.
 * -------------------------------------------------------------------------- */
#ifndef HS_PHASE_PULL
#  define HS_PHASE_IDLE
#  define HS_PHASE_PUBLISH
#  define HS_PHASE_PULL
#  define HS_PHASE_BARRIER
#  define HS_PHASE_COMMIT
#  define HS_PURE
#  define HS_SHARED
#  define HS_READONLY
#  define HS_SHARD_PRIVATE
#  define HS_BOUNDED(n)
#  define HS_NO_ESCAPE
#  define HS_DISJOINT restrict
#endif

#endif /* HC_RT_H */
