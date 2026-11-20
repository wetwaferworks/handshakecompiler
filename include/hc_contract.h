/* SPDX-License-Identifier: MIT */
/*
 * Handshake Compiler (HC) v0.1 — Fair-Play & Phase-Isolation Contract Header
 * Author: Herominde (Fair Play & Anti-Cheat), aligned with HandshakeOS hs_handshake.h
 *
 * Canonical C11 HS_* phase/ownership annotations (Option A frontend) with HC_* aliases.
 * Compiles cleanly under standard gcc/clang (-std=c11 -Wall -Wextra -Werror) with
 * zero runtime overhead while exposing explicit phase, ownership, and bound contracts
 * to HC's phase-IR verifier and dual-pass static analyzer.
 */
#ifndef HC_CONTRACT_H
#define HC_CONTRACT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* --------------------------------------------------------------------------
 * Canonical C11 HS_* phase & ownership annotations
 * -------------------------------------------------------------------------- */
#if defined(__HC_COMPILER__)
  #define HS_PHASE_IDLE         __attribute__((annotate("hs:phase:idle")))
  #define HS_PHASE_PUBLISH      __attribute__((annotate("hs:phase:publish")))
  #define HS_PHASE_PULL         __attribute__((annotate("hs:phase:pull")))
  #define HS_PHASE_BARRIER      __attribute__((annotate("hs:phase:barrier")))
  #define HS_PHASE_COMMIT       __attribute__((annotate("hs:phase:commit")))
  #define HS_PURE               __attribute__((annotate("hs:pure:deterministic")))
  #define HS_SHARED             __attribute__((annotate("hs:mem:shared")))
  #define HS_READONLY           __attribute__((annotate("hs:mem:readonly")))
  #define HS_SHARD_PRIVATE      __attribute__((annotate("hs:mem:shard_private")))
  #define HS_BOUNDED(n)         __attribute__((annotate("hs:loop:bound:" #n)))
  #define HS_NO_ESCAPE          __attribute__((annotate("hs:ptr:no_escape")))
  #define HS_DISJOINT           restrict
#else
  #define HS_PHASE_IDLE
  #define HS_PHASE_PUBLISH
  #define HS_PHASE_PULL
  #define HS_PHASE_BARRIER
  #define HS_PHASE_COMMIT
  #define HS_PURE
  #define HS_SHARED
  #define HS_READONLY
  #define HS_SHARD_PRIVATE
  #define HS_BOUNDED(n)
  #define HS_NO_ESCAPE
  #define HS_DISJOINT           restrict
#endif

/* Backwards-compatible HC_* aliases */
#define HC_PHASE_IDLE         HS_PHASE_IDLE
#define HC_PHASE_PUBLISH      HS_PHASE_PUBLISH
#define HC_PHASE_PULL         HS_PHASE_PULL
#define HC_PHASE_BARRIER      HS_PHASE_BARRIER
#define HC_PHASE_COMMIT       HS_PHASE_COMMIT
#define HC_PURE_DETERMINISTIC HS_PURE
#define HC_SHARED_STATE       HS_SHARED
#define HC_READONLY_SNAP      HS_READONLY
#define HC_SHARD_PRIVATE      HS_SHARD_PRIVATE
#define HC_BOUNDED_LOOP(n)    HS_BOUNDED(n)
#define HC_NO_ESCAPE          HS_NO_ESCAPE
#define HC_DISJOINT           HS_DISJOINT

/*
 * Canonical HandshakeOS 3+1 beat ids (mirroring hs_beat_t in hs_handshake.h).
 * Legal cycle: IDLE(0) -> PUBLISH(1) -> PULL(2) -> BARRIER(3) -> COMMIT(4) -> IDLE(0)
 */
enum hc_beat_id {
    HC_BEAT_IDLE    = 0,
    HC_BEAT_PUBLISH = 1,
    HC_BEAT_PULL    = 2,
    HC_BEAT_BARRIER = 3,
    HC_BEAT_COMMIT  = 4
};

/*
 * Stable diagnostic rule codes (HC-E001 .. HC-E026).
 */
enum hc_rule_code {
    HC_OK                       = 0,
    HC_E001_LOCK_POSIX          = 1,
    HC_E002_LOCK_C11_OR_OS      = 2,
    HC_E003_LOCK_ATOMIC_SPIN    = 3,
    HC_E004_LOCK_ASM_OR_PAUSE   = 4,
    HC_E005_LOCK_VOLATILE_WAIT  = 5,
    HC_E006_LOCK_HIDDEN_THREAD  = 6,
    HC_E007_PHASE_TRANSITION    = 7,
    HC_E008_PHASE_WRITE_PULL    = 8,
    HC_E009_PHASE_UNPUBLISHED   = 9,
    HC_E010_PHASE_COMMIT_LEAK   = 10,
    HC_E011_PHASE_CALL_LEAK     = 11,
    HC_E012_PHASE_PTR_ESCAPE    = 12,
    HC_E013_ALIAS_CONST_CAST    = 13,
    HC_E014_ALIAS_UNION_PUN     = 14,
    HC_E015_ALIAS_SHARD_OVERLAP = 15,
    HC_E016_LIVENESS_LOOP       = 16,
    HC_E017_LIVENESS_RECURSION  = 17,
    HC_E018_LIVENESS_BLOCKING   = 18,
    HC_E019_WATCHDOG_TAMPER     = 19,
    HC_E020_DET_WALL_CLOCK      = 20,
    HC_E021_DET_CYCLE_COUNTER   = 21,
    HC_E022_DET_AMBIENT_RNG     = 22,
    HC_E023_DET_ORDER_LEAK      = 23,
    HC_E024_SMUGGLE_DLOPEN      = 24,
    HC_E025_SMUGGLE_MACRO       = 25,
    HC_E026_SMUGGLE_LINKER      = 26
};

/* Canonical FNV-1a 64-bit step matching hs_fnv1a64() in HandshakeOS handshake.c */
static inline uint64_t hc_fnv1a64_step(uint64_t hash, uint64_t word)
{
    unsigned i;
    if (hash == 0) {
        hash = 0xcbf29ce484222325ull;
    }
    for (i = 0; i < 8u; i++) {
        hash ^= (word >> (i * 8u)) & 0xFFull;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

struct hc_phase_snapshot {
    uint64_t seq;
    uint64_t generation;
    uint64_t phase;
    uint64_t cycle;
    uint32_t group;
    uint32_t seed;
};

struct hc_shard_result {
    uint32_t component_id;
    uint32_t shard_id;
    uint64_t note;
    uint64_t work_units;
};

#endif /* HC_CONTRACT_H */
