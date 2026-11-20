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
 *
 * EVTSTREAM DEPENDENCY (R2): the bounded-wait primitive below parks the core
 * with WFE against the ARM64 exclusive monitor. A wakeup arrives either from an
 * explicit hc_event_wake() (SEV) by the writer, OR from the CPU event stream
 * (CNTKCTL_EL1.EVNTEN) which periodically sets the event so a core can never
 * stall indefinitely if a store slipped past without a SEV. On platforms with
 * the event stream disabled, writers MUST pair every published update with
 * hc_event_wake(); the wait is still bounded by max_spins regardless, so it can
 * never become an unbounded busy-wait / spinlock.
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

/*
 * hc_fence_publish — full publish fence (R1). Ordered so that a reader taking
 * hc_fence_pull() observes ALL prior stores, not only stores-before-stores.
 * The store-only variant hc_fence_publish_st() (dmb ishst) is kept for the
 * narrow case where only prior stores must be ordered and no later load needs
 * to be fenced; the default publish path uses the full barrier for safety.
 */
static inline void hc_fence_publish(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dmb ish" ::: "memory");       /* R1: full, not ishst */
#elif defined(__x86_64__)
    __asm__ volatile("" ::: "memory");
#else
    atomic_thread_fence(memory_order_release);
#endif
}

/* Store-only publish fence — documented, narrower ordering (R1). */
static inline void hc_fence_publish_st(void)
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
#else
    /* R3: portable, standard full fence — x86_64 no longer hand-rolls mfence. */
    atomic_thread_fence(memory_order_seq_cst);
#endif
}

/* --------------------------------------------------------------------------
 * Acquire / release word primitives.
 * R4: each has a plain-pointer (asm/volatile) form and an _Atomic form, wired
 * together by a _Generic dispatch macro so callers can pass EITHER a plain /
 * volatile pointer OR an _Atomic-qualified pointer under -Werror.
 * -------------------------------------------------------------------------- */
static inline uint64_t hc_load_acq_u64_plain(const volatile uint64_t *p)
{
#if defined(__aarch64__)
    uint64_t v;
    __asm__ volatile("ldar %0, [%1]" : "=r"(v) : "r"(p) : "memory");
    return v;
#else
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
#endif
}
static inline uint64_t hc_load_acq_u64_atomic(const _Atomic uint64_t *p)
{
    return atomic_load_explicit(p, memory_order_acquire);
}

static inline uint32_t hc_load_acq_u32_plain(const volatile uint32_t *p)
{
#if defined(__aarch64__)
    uint32_t v;
    __asm__ volatile("ldar %w0, [%1]" : "=r"(v) : "r"(p) : "memory");
    return v;
#else
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
#endif
}
static inline uint32_t hc_load_acq_u32_atomic(const _Atomic uint32_t *p)
{
    return atomic_load_explicit(p, memory_order_acquire);
}

static inline void hc_store_rel_u64_plain(volatile uint64_t *p, uint64_t v)
{
#if defined(__aarch64__)
    __asm__ volatile("stlr %0, [%1]" :: "r"(v), "r"(p) : "memory");
#else
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
#endif
}
static inline void hc_store_rel_u64_atomic(_Atomic uint64_t *p, uint64_t v)
{
    atomic_store_explicit(p, v, memory_order_release);
}

static inline void hc_store_rel_u32_plain(volatile uint32_t *p, uint32_t v)
{
#if defined(__aarch64__)
    __asm__ volatile("stlr %w0, [%1]" :: "r"(v), "r"(p) : "memory");
#else
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
#endif
}
static inline void hc_store_rel_u32_atomic(_Atomic uint32_t *p, uint32_t v)
{
    atomic_store_explicit(p, v, memory_order_release);
}

/* _Generic dispatch: works for _Atomic, plain, const, volatile pointers. */
#define hc_load_acq_u64(p) _Generic((p), \
    _Atomic uint64_t *:        hc_load_acq_u64_atomic, \
    const _Atomic uint64_t *:  hc_load_acq_u64_atomic, \
    volatile uint64_t *:       hc_load_acq_u64_plain,  \
    const volatile uint64_t *: hc_load_acq_u64_plain,  \
    uint64_t *:                hc_load_acq_u64_plain,  \
    const uint64_t *:          hc_load_acq_u64_plain)(p)

#define hc_load_acq_u32(p) _Generic((p), \
    _Atomic uint32_t *:        hc_load_acq_u32_atomic, \
    const _Atomic uint32_t *:  hc_load_acq_u32_atomic, \
    volatile uint32_t *:       hc_load_acq_u32_plain,  \
    const volatile uint32_t *: hc_load_acq_u32_plain,  \
    uint32_t *:                hc_load_acq_u32_plain,  \
    const uint32_t *:          hc_load_acq_u32_plain)(p)

#define hc_store_rel_u64(p, v) _Generic((p), \
    _Atomic uint64_t *:  hc_store_rel_u64_atomic, \
    volatile uint64_t *: hc_store_rel_u64_plain,  \
    uint64_t *:          hc_store_rel_u64_plain)((p), (v))

#define hc_store_rel_u32(p, v) _Generic((p), \
    _Atomic uint32_t *:  hc_store_rel_u32_atomic, \
    volatile uint32_t *: hc_store_rel_u32_plain,  \
    uint32_t *:          hc_store_rel_u32_plain)((p), (v))

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
 * Bounded wait (R2): wait for *word to differ from `expected`, up to
 * `max_spins` event iterations, then return the observed word to the caller
 * (which decides whether to futex-sleep). It is NOT a spinlock — the iteration
 * count is bounded, so there is never an unbounded busy-wait.
 *
 * On ARM64 the value is loaded with LDAXR (read-only, exclusive — NO STXR, so
 * nothing is written and no lock is built). LDAXR arms the local exclusive
 * monitor; a store to `word` (or the CPU event stream, or hc_event_wake())
 * clears it, which releases the subsequent WFE. The value is re-checked BEFORE
 * each wait and the loop breaks immediately once it differs, so a wakeup that
 * already landed is never missed. CLREX clears the monitor on every exit path.
 * See the EVTSTREAM DEPENDENCY note at the top of this header.
 */
static inline uint32_t hc_bounded_wait_u32(const volatile uint32_t *word,
                                           uint32_t expected, uint32_t max_spins)
{
#if defined(__aarch64__)
    uint32_t i, v;
    __asm__ volatile("ldaxr %w0, [%1]" : "=&r"(v) : "r"(word) : "memory");
    if (v != expected) {
        __asm__ volatile("clrex" ::: "memory");
        return v;
    }
    for (i = 0; i < max_spins; i++) {
        __asm__ volatile("wfe" ::: "memory");
        __asm__ volatile("ldaxr %w0, [%1]" : "=&r"(v) : "r"(word) : "memory");
        if (v != expected) {
            break;
        }
    }
    __asm__ volatile("clrex" ::: "memory");
    return v;
#else
    uint32_t i;
    uint32_t v = hc_load_acq_u32(word);
    hc_event_prime();
    for (i = 0; i < max_spins; i++) {
        if (v != expected) {
            break;
        }
        hc_event_wait();
        v = hc_load_acq_u32(word);
    }
    return v;
#endif
}

/* --------------------------------------------------------------------------
 * Cycle counter (telemetry ONLY — never fold into a ledger: HC-E021).
 * Gated behind HC_RUNTIME_TELEMETRY so component code cannot reach it; HC also
 * rejects direct hc_cycle_counter/hc_cycle_freq calls under HC-E021 in the
 * component profile.
 * -------------------------------------------------------------------------- */
#if defined(HC_RUNTIME_TELEMETRY)
static inline uint64_t hc_cycle_counter(void)
{
#if defined(__aarch64__)
    uint64_t v;
    /* R6: ISB before the read so the counter sample is not speculated early. */
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v));
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
#endif /* HC_RUNTIME_TELEMETRY */

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
 * Deterministic 4x32-bit lane bit-pack (R5). Reads ALL four lanes into locals
 * before writing either output word, so it is correct even if `out` aliases
 * `lanes`. 100% bit-exact little-endian; no spurious memory barrier.
 * -------------------------------------------------------------------------- */
static inline void hc_bitpack_u32x4(const uint32_t lanes[4], uint64_t out[2])
{
    uint32_t l0 = lanes[0];
    uint32_t l1 = lanes[1];
    uint32_t l2 = lanes[2];
    uint32_t l3 = lanes[3];
    out[0] = ((uint64_t)l0) | ((uint64_t)l1 << 32);
    out[1] = ((uint64_t)l2) | ((uint64_t)l3 << 32);
}

/* --------------------------------------------------------------------------
 * Lowered phase-annotation shims. These MIRROR hc_contract.h exactly so that a
 * translation unit including only hc_rt.h still exposes phase/ownership
 * annotations to HC's analyzer (under __HC_COMPILER__), and so that including
 * BOTH headers yields byte-identical definitions (no -Wmacro-redefined). With
 * __HC_COMPILER__ unset (plain GCC codegen) these are zero-overhead.
 * -------------------------------------------------------------------------- */
#ifndef HS_PHASE_PULL
#  if defined(__HC_COMPILER__)
#    define HS_PHASE_IDLE         __attribute__((annotate("hs:phase:idle")))
#    define HS_PHASE_PUBLISH      __attribute__((annotate("hs:phase:publish")))
#    define HS_PHASE_PULL         __attribute__((annotate("hs:phase:pull")))
#    define HS_PHASE_BARRIER      __attribute__((annotate("hs:phase:barrier")))
#    define HS_PHASE_COMMIT       __attribute__((annotate("hs:phase:commit")))
#    define HS_PURE               __attribute__((annotate("hs:pure:deterministic")))
#    define HS_SHARED             __attribute__((annotate("hs:mem:shared")))
#    define HS_READONLY           __attribute__((annotate("hs:mem:readonly")))
#    define HS_SHARD_PRIVATE      __attribute__((annotate("hs:mem:shard_private")))
#    define HS_BOUNDED(n)         __attribute__((annotate("hs:loop:bound:" #n)))
#    define HS_NO_ESCAPE          __attribute__((annotate("hs:ptr:no_escape")))
#    define HS_DISJOINT           restrict
#  else
#    define HS_PHASE_IDLE
#    define HS_PHASE_PUBLISH
#    define HS_PHASE_PULL
#    define HS_PHASE_BARRIER
#    define HS_PHASE_COMMIT
#    define HS_PURE
#    define HS_SHARED
#    define HS_READONLY
#    define HS_SHARD_PRIVATE
#    define HS_BOUNDED(n)
#    define HS_NO_ESCAPE
#    define HS_DISJOINT           restrict
#  endif
#endif

#endif /* HC_RT_H */
