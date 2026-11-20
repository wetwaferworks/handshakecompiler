# Handshake Compiler (`HC`) v0.1 — Fair-Play & Phase-Isolation Contract

**Owner:** Herominde (Fair Play & Anti-Cheat)
**Target Runtime:** HandshakeOS Timing Handshake (`/workspace/handshakeos/include/hs_handshake.h`, `/workspace/handshakeos/src/handshake.c`)
**Status:** v0.1 Draft — ready to merge with Master Chief's `/workspace/hc/include/hc_rules.h` & `/workspace/hc/docs/RULES.md`
**License:** MIT

---

## 1. Ground-Truth HandshakeOS 3+1 Invariants

This contract is grounded directly in the verified HandshakeOS C11 implementation (first real-silicon boot on Anbernic RG35XX 2024 / Allwinner H700, deterministic ledger parity hash **`5f44cefa72e8ac19`**):

- **Three pillars only:** Time Serialization, control coupling, event coupling. (Not the Threading Handshake.)
- **Seating:** `3+1` default — one clock thread publishes phase windows; N worker threads pull.
- **Four Gemstone beats per phase (`hs_beat_t`):**
  `HS_BEAT_IDLE (0) -> HS_BEAT_PUBLISH (1) -> HS_BEAT_PULL (2) -> HS_BEAT_BARRIER (3) -> HS_BEAT_COMMIT (4) -> HS_BEAT_IDLE (0)`
- **Missed-window policy:** `WAIT` — the phase extends until all due workers arrive; nothing is silently skipped.
- **Watchdog policy:** `FREEZE` — default ceiling `250 ms` (`HS_HS_DEFAULT_CEILING_NS`). `last_freeze` names the offender; `reset_after_freeze` bumps `generation` so stale in-flight work drops out cleanly.
- **Deterministic commit:** FNV-1a 64-bit (`hs_fnv1a64`, offset `0xcbf29ce484222325ull`, prime `0x100000001b3ull`) folded in **component-id order** (`0 .. n_comps - 1`), plus matured control events folded in **`(tick, seq)` order**. The ledger hash depends strictly on the work and its canonical order — never on thread scheduling, arrival order, wall time, or pointer addresses.

---

## 2. Two Enforcement Profiles (Zero False Positives on Real HandshakeOS)

Inspected directly via `nm -u` on `/workspace/handshakeos/build/host/{handshake.o,gb_emu.o,scoreboard.o}`:

1. **`HC_PROFILE_COMPONENT` (Default — ALL component, emulator, game, and phase-handler TUs):**
   - Full ironclad enforcement of **`HC-E001` through `HC-E026`**.
   - Zero mutexes, zero locks, zero futexes, zero raw thread creation, zero clocks, zero sleeps, zero ambient RNG, zero unbounded loops/recursion, zero cross-phase mutable writes, zero aliasing bypasses, zero dynamic loading or linker smuggling.
   - Real `gb_emu.o` and `scoreboard.o` already satisfy this profile with zero lock/clock/thread/syscall imports.

2. **`HC_PROFILE_RUNTIME` (Explicit opt-in ONLY for audited timing-handshake runtime core, e.g. `src/handshake.c`):**
   - **Still 100% bans:** every mutex, rwlock, condvar, spinlock, semaphore (`HC-E001`, `HC-E002` lock ops), hidden atomic/volatile/asm spinlocks (`HC-E003`..`HC-E005`), ambient RNG (`HC-E022`), wall-clock time (`CLOCK_REALTIME`, `gettimeofday`, `time`), `dlopen`/`dlsym` (`HC-E024`), macro/linker smuggling (`HC-E025`, `HC-E026`).
   - **Narrowly permits ONLY:**
     - `pthread_create` / `pthread_join` for fixed worker-pool lifecycle (`hs_hs_start`/`hs_hs_stop`).
     - `syscall(SYS_futex, ..., FUTEX_WAIT_PRIVATE | FUTEX_WAKE_PRIVATE, ...)` purely for worker/clock sleep-wake signaling on `publish_word`/`barrier_word` (never `FUTEX_LOCK_PI` or lock acquisition).
     - `clock_gettime(CLOCK_MONOTONIC, ...)` strictly inside `hs_now_ns()` for watchdog ceiling and jitter telemetry (must NEVER flow into `ledger_hash`).

---

## 3. Legal Phase-Transition Matrix

| Current Beat | Permitted Next Beat | Allowed Writer | Allowed Reader | Forbidden Actions |
|---|---|---|---|---|
| `IDLE` | `PUBLISH` | Clock thread (setup/config/registration before start) | Clock thread | Worker mutation, out-of-order commit |
| `PUBLISH` | `PULL` | Clock thread (seqlock `pub_seq` odd->fields->even, `publish_word` wake, affine items) | Workers seqlock-read only after even `pub_seq` | Worker writes to shared/global state; reading while `pub_seq & 1` |
| `PULL` | `BARRIER` | Each worker writes ONLY to its own disjoint shard slot + returns `uint64_t` note | Workers read `const` published snapshot + read-only committed state | Shared mutable writes, pointer escape, `const`-cast, locks, sleeps, clocks, RNG, unbounded loops, recursion |
| `BARRIER` | `COMMIT` (all arrived) or `IDLE` (watchdog `FREEZE`) | Governed runtime `arrivals` fetch-add & watchdog freeze state only | Clock thread polls `arrivals >= expected` or ceiling | Worker writes after arrival; resetting `frozen` or inflating `ceiling_ns`; skipping unfinished work |
| `COMMIT` | `IDLE` | Clock thread ONLY folds component notes in `id` order + matured events in `(tick, seq)` order into `ledger_hash` & bumps `commit_epoch` | Next phase's `PULL` after next `PUBLISH` | Worker execution during commit; folding in arrival/thread/pointer order; folding timestamps or RNG |

---

## 4. Complete Diagnostic Rule Registry (`HC-E001` .. `HC-E026`)

All codes below are **hard compile-time errors** (exit non-zero, refuse to emit object/binary).

### Domain A — Zero-Mutex & Hidden-Lock Enforcement
- **`HC-E001` (`LOCK_POSIX`)**: Any POSIX lock/sync primitive (`pthread_mutex*`, `pthread_rwlock*`, `pthread_cond*`, `pthread_spin*`, `pthread_barrier*`, `sem_*`, `<semaphore.h>`).
- **`HC-E002` (`LOCK_C11_OR_OS`)**: C11 `<threads.h>` locks (`mtx_*`, `cnd_*`, `call_once`), file/kernel locks (`flock`, `lockf`, `fcntl` `F_SETLK`/`F_SETLKW`/`F_OFD_SETLKW`), futex lock ops (`FUTEX_LOCK_PI`/`FUTEX_TRYLOCK_PI`), or any `SYS_futex` in `HC_PROFILE_COMPONENT`.
- **`HC-E003` (`LOCK_ATOMIC_SPIN`)**: Hidden atomic spinlock or CAS retry lock loop (`while (atomic_flag_test_and_set*(...))`, `while (!atomic_compare_exchange*(...))`, `while (atomic_exchange*(...))`, `__sync_lock_test_and_set`, `__atomic_test_and_set` in loop).
- **`HC-E004` (`LOCK_ASM_OR_PAUSE`)**: Inline assembly lock/spin/syscall bypass (`lock cmpxchg`, `lock xchg`, `lock bts`, ARM64 `ldaxr`/`stlxr` or `casal`/`swpal` spinloop, `pause`/`yield`/`wfe` spin, raw `syscall`/`svc` futex).
- **`HC-E005` (`LOCK_VOLATILE_WAIT`)**: Non-atomic `volatile` busy-wait / Dekker / Peterson spinlock (`while (flag) {}`, `while (turn != id) {}`).
- **`HC-E006` (`LOCK_HIDDEN_THREAD`)**: Spawning unauthorized threads/tasks (`pthread_create`, `thrd_create`, `clone`, `fork`, `vfork`, `#pragma omp`) inside `HC_PROFILE_COMPONENT`.

### Domain B — Phase Transition & Boundary Isolation
- **`HC-E007` (`PHASE_TRANSITION`)**: Illegal beat transition (skipping `BARRIER` to call `COMMIT`, invoking `COMMIT` from `PULL`, or calling `hs_hs_run_phase` from inside a worker callback).
- **`HC-E008` (`PHASE_WRITE_PULL`)**: Writing to `HC_SHARED_STATE` / file-scope / `static` mutable state from a `HC_PHASE_PULL` worker function.
- **`HC-E009` (`PHASE_UNPUBLISHED`)**: Reading unpublished staging fields or directly mutating `struct hs_hs` internal slots (`comps[]`, `pub_*`, `arrivals`, `expected`, `ledger_hash`) from component code.
- **`HC-E010` (`PHASE_COMMIT_LEAK`)**: Mutating commit-only state or posting control events (`hs_hs_post_event`) from a `PULL` worker or `BARRIER` context instead of the clock thread.
- **`HC-E011` (`PHASE_CALL_LEAK`)**: Transitive call-graph violation — a `PULL` function calling a helper (directly or via function pointer) that writes shared mutable state or calls a `COMMIT`/`PUBLISH`-only function.
- **`HC-E012` (`PHASE_PTR_ESCAPE`)**: Escaping a pointer to phase-scoped, stack, snapshot, or shard-private memory into a global, static, or cross-shard pointer.

### Domain C — Aliasing & Immutability Integrity
- **`HC-E013` (`ALIAS_CONST_CAST`)**: Casting away `const` on a published snapshot or `HC_READONLY_SNAP` pointer to mutate read-only phase data.
- **`HC-E014` (`ALIAS_UNION_PUN`)**: Union type-punning (`union { const T *ro; T *rw; }`) or `uintptr_t` integer round-trip / pointer arithmetic used to obtain a writable alias to read-only or foreign-shard memory.
- **`HC-E015` (`ALIAS_SHARD_OVERLAP`)**: Cross-shard write or missing `restrict` disjointness — writing into another shard/component slot (`slots[other_id]`) or sharing an unguarded mutable buffer across workers.

### Domain D — Liveness, Starvation & Watchdog Ceiling
- **`HC-E016` (`LIVENESS_LOOP`)**: Statically unbounded loop (`for (;;)`, `while (1)`, unbounded `while`, or backward `goto` loop) inside a `PULL`/`COMMIT` function.
- **`HC-E017` (`LIVENESS_RECURSION`)**: Direct or mutual recursion (`f -> f` or `f -> g -> f`) reachable from any phase callback.
- **`HC-E018` (`LIVENESS_BLOCKING`)**: Blocking sleep, yield loop, or blocking I/O syscall inside a phase callback (`sleep`, `usleep`, `nanosleep`, `clock_nanosleep`, `select`, `poll`, `epoll_wait`, blocking `read`/`recv`/`accept`/`connect`, `waitpid`, `sched_yield` loop).
- **`HC-E019` (`WATCHDOG_TAMPER`)**: Bypassing or mutating the watchdog contract (clearing `hs->frozen`, raising `ceiling_ns` during a phase, ignoring `generation` mismatch, or dropping overdue work instead of honoring `WAIT`).

### Domain E — Determinism & Ledger Parity
- **`HC-E020` (`DET_WALL_CLOCK`)**: Reading wall-clock or monotonic time (`time`, `gettimeofday`, `clock_gettime`, `timespec_get`, `clock`, `hs_now_ns`) inside `HC_PROFILE_COMPONENT` payload logic.
- **`HC-E021` (`DET_CYCLE_COUNTER`)**: Reading hardware cycle/timestamp counters (`rdtsc`, `rdtscp`, `__rdtsc`, `__builtin_ia32_rdtsc`, ARM64 `cntvct_el0`/`pmccntr_el0`).
- **`HC-E022` (`DET_AMBIENT_RNG`)**: Unseeded or ambient entropy (`rand`, `srand`, `random`, `drand48`, `getrandom`, `arc4random`, `/dev/urandom`, `/dev/random`, x86 `rdrand`/`rdseed`, ARM64 `rndr`).
- **`HC-E023` (`DET_ORDER_LEAK`)**: Nondeterministic ordering in outputs or ledger folding — hashing/branching on pointer addresses (`(uintptr_t)ptr`), `pthread_self()`/`gettid()`/`sched_getcpu()`, or arrival-order atomic append (`atomic_fetch_add`) folded into the ledger instead of component-id / `(tick, seq)` order.

### Domain F — Smuggling, Obfuscation & Linker Evasion
- **`HC-E024` (`SMUGGLE_DLOPEN`)**: Dynamic symbol smuggling (`<dlfcn.h>`, `dlopen`, `dlsym`, `dlvsym`), including split string literals (`"pthread_" "mutex_lock"`), hex/octal escapes (`"\x70thread_mutex_lock"`), or stack-constructed symbol strings.
- **`HC-E025` (`SMUGGLE_MACRO`)**: Preprocessor token-pasting (`##`) smuggling banned calls/types, macro-disguised locks, `#pragma` suppression tricks, or forged runtime-privilege annotations inside component code.
- **`HC-E026` (`SMUGGLE_LINKER`)**: Linker/assembler symbol evasion (`__attribute__((alias(...)))`, `__asm__("pthread_mutex_lock")` symbol redirection, `.symver`, weak overrides of HandshakeOS symbols, or precompiled objects importing banned symbols caught by ELF64/`nm` audit).

---

## 5. Multi-Pass Architecture Mapping (Sync with Master Chief's HC v0.1)

| HC Pass | Primary Rules Caught | Notes |
|---|---|---|
| **Pass 1: Raw source / preprocessor-directive scan** | `HC-E025`, include hygiene (`<semaphore.h>`, `<threads.h>`, `<dlfcn.h>`), forged annotations | Runs before macro expansion so obfuscation intent is visible |
| **Pass 2: Preprocessed token + AST scan + string-literal decoder** | `HC-E001`..`HC-E006`, `HC-E008`..`HC-E010`, `HC-E012`..`HC-E016`, `HC-E018`..`HC-E024` | Decodes adjacent string concat, `\x`/`\ooo` escapes, and macro-expanded `##` tokens |
| **Pass 3: Call-graph & phase-propagation pass** | `HC-E007`, `HC-E011`, `HC-E017` (direct + mutual recursion), indirect fn-ptr propagation | Propagates `PULL`/`COMMIT` taint and shared-write / side-effect summaries across call edges |
| **Pass 4: Pure-C ELF64 symbol-table audit (`nm` equivalent)** | `HC-E001`, `HC-E002`, `HC-E006`, `HC-E018`, `HC-E020`, `HC-E022`, `HC-E024`, `HC-E026` | Zero-trust final gate on every emitted `.o` and linked ELF binary before shipping |

---

## 6. Layer-1 Automated Verification Gates

1. **100% Cheater Rejection:** Every file in `cheaters/` MUST be rejected by `hc` with non-zero exit AND emit the expected `HC-E###` code in `manifest.tsv`.
2. **100% Valid Acceptance:** Every file in `valid/` MUST compile cleanly with `hc` (`-std=c11 -Wall -Wextra -Werror`) AND pass `nm-zero-lock-audit.sh --profile=component`.
3. **Real HandshakeOS Parity:** Compiling `/workspace/handshakeos/` with `hc` (`--profile=runtime` on `handshake.c`, default component profile on `scoreboard.c`/`gb_emu.c`) MUST pass ELF64 zero-lock audit AND reproduce the golden ledger hash **`5f44cefa72e8ac19`** bit-for-bit.

---

## 7. Dual Input Syntax (Option A Frontend — JD / Cortana Scope Update)

HC v1 enforces the exact same `HC-E001`..`HC-E026` phase-IR contract across **both** input surfaces before lowering to `gcc` / `aarch64-linux-gnu-gcc` + targeted inline asm:

1. **C11 `HS_*` Annotations (`.c` files, direct HandshakeOS compatibility):**
   - `HS_PHASE_IDLE`, `HS_PHASE_PUBLISH`, `HS_PHASE_PULL`, `HS_PHASE_BARRIER`, `HS_PHASE_COMMIT`, `HS_PURE`, `HS_SHARED`, `HS_READONLY`, `HS_SHARD_PRIVATE`, `HS_BOUNDED(n)`, `HS_NO_ESCAPE`, `HS_DISJOINT` (defined in `include/hc_contract.h`, with `HC_*` backwards-compatible aliases).
   - Valid controls: `valid/v01`..`v08` (8 files). Adversarial cheaters: `cheaters/c01`..`c30` (30 files covering all 26 rules).
2. **Dedicated `.hc` Phase-Keyword Syntax (`.hc` files):**
   - `phase publish`, `phase pull`, `phase barrier`, `phase commit`, `pure`, `shared`, `readonly`, `shard_private`, `bounded(N)`.
   - Valid controls: `valid_hc/vh01`..`vh02` (2 files). Adversarial cheaters: `cheaters_hc/ch01`..`ch06` (6 files).
