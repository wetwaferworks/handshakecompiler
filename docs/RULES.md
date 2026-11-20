# HC Fair-Play & Phase-Isolation Rules (`HC-E001`..`HC-E026`)

Canonical registry owned by Herominde (see `include/hc_contract.h` and
`tests/cheaters/herominde/HC-FAIR-PLAY-CONTRACT.md`). Printable at runtime
with `hc --dump-rules`. Each code is a hard compile-time error (non-zero
exit, no object emitted).

| Code | Name | What it rejects |
|------|------|-----------------|
| HC-E001 | LOCK_POSIX | POSIX mutex/rwlock/cond/spin/barrier/semaphore primitive |
| HC-E002 | LOCK_C11_OR_OS | C11 <threads.h> lock, flock/lockf, or SYS_futex/FUTEX_LOCK in component |
| HC-E003 | LOCK_ATOMIC_SPIN | hidden atomic_flag / CAS-retry / exchange spinlock |
| HC-E004 | LOCK_ASM_OR_PAUSE | inline-asm lock/spin/pause bypass |
| HC-E005 | LOCK_VOLATILE_WAIT | volatile Dekker/Peterson busy-wait |
| HC-E006 | LOCK_HIDDEN_THREAD | unauthorized thread/process creation in component |
| HC-E007 | PHASE_TRANSITION | illegal beat transition (PULL invoking COMMIT, skipping BARRIER) |
| HC-E008 | PHASE_WRITE_PULL | write to shared/file-scope mutable state inside PULL/BARRIER |
| HC-E009 | PHASE_UNPUBLISHED | reading/mutating unpublished staging or hs_hs internals |
| HC-E010 | PHASE_COMMIT_LEAK | posting commit/control events from PULL/BARRIER |
| HC-E011 | PHASE_CALL_LEAK | transitive call-graph leak to shared write / COMMIT mutator |
| HC-E012 | PHASE_PTR_ESCAPE | phase-scoped/stack/shard pointer escaping into global |
| HC-E013 | ALIAS_CONST_CAST | casting away const on a readonly snapshot |
| HC-E014 | ALIAS_UNION_PUN | union / uintptr_t punning bypassing const immutability |
| HC-E015 | ALIAS_SHARD_OVERLAP | cross-shard write overlap |
| HC-E016 | LIVENESS_LOOP | statically unbounded loop / backward goto in phase kernel |
| HC-E017 | LIVENESS_RECURSION | direct/mutual recursion reachable from a phase kernel |
| HC-E018 | LIVENESS_BLOCKING | blocking sleep/yield/IO inside a phase kernel |
| HC-E019 | WATCHDOG_TAMPER | tampering with watchdog ceiling_ns/frozen |
| HC-E020 | DET_WALL_CLOCK | wall-clock/monotonic time read in component payload |
| HC-E021 | DET_CYCLE_COUNTER | raw hardware cycle counter in deterministic code |
| HC-E022 | DET_AMBIENT_RNG | ambient entropy / unseeded RNG |
| HC-E023 | DET_ORDER_LEAK | pointer/tid/arrival-order leak into output/ledger |
| HC-E024 | SMUGGLE_DLOPEN | dynamic-loader symbol smuggling (dlopen/dlsym/split-hex) |
| HC-E025 | SMUGGLE_MACRO | preprocessor ## token-pasting smuggle / #pragma suppression |
| HC-E026 | SMUGGLE_LINKER | linker/asm symbol redirection to a banned symbol |

## Profiles
- `--profile=component` (default): all 26 rules, for every component, emulator, game, and phase-handler TU.
- `--profile=runtime` / `--allow-runtime`: audited timing-handshake core only (`src/handshake.c`). Still bans every lock/spinlock/RNG/wall-clock/dlopen/smuggle; narrowly permits `pthread_create`/`pthread_join`, `FUTEX_WAIT/WAKE_PRIVATE`, and `clock_gettime(CLOCK_MONOTONIC)` in `hs_now_ns()` telemetry.
