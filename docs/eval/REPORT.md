# HC v0.1 (`9eb4d70`): Layer-2 independent evaluation (Mr Data · Training & Evaluation)

Date: 2026-11-19, 19:37–19:50 EST. Box only. No host machines outside the build sandbox were touched.
Scope: determinism sweeps, interleaved A/B benchmark, binary/codegen audit, `hc_rt.h` correctness and micro-benchmarks.

## Verdict: **PASS WITH FINDINGS**

- **HC v0.1 passes as a compiler.** HC verifies the source, then hands it to gcc. Its output is byte-for-byte identical to stock gcc. It adds no regression and no determinism risk. Every ledger produced on either architecture was the golden value or the explicit failure sentinel `0`. No run ever produced a wrong non-zero hash.
- **Finding with the most impact:** 6 of 36 `handshake-test` runs failed (exit 1), on both the hc and the gcc binaries. The cause is a **watchdog FREEZE from a lost futex wakeup in HandshakeOS `worker_main`** (`src/handshake.c`). This is a HandshakeOS liveness bug, not an HC bug. A one-line reorder fixes it (patch and A/B proof in §6).
- **Some claims need correcting before Layer-3.** "hc-compiled" does not mean different machine code. `hc_rt.h` is not linked into any HandshakeOS binary. "Zero mutexes" holds for the HandshakeOS translation units, but the static ARM64 executables do contain glibc mutex, cond, and getrandom code.

---

## 1. Environment

| Item | Value |
|---|---|
| CPU | Intel Xeon (family 6 model 207, Emerald Rapids-class), 8 vCPU, KVM guest, 1 thread/core |
| nproc | 8 |
| loadavg before sweep / A/B | 3.32 / 3.60 (top showed 97–100% idle, so the loadavg comes from blocked or sleeping tasks, not CPU) |
| Toolchain | gcc 14.2.0 (Debian 14.2.0-19), aarch64-linux-gnu-gcc 14.2.0, qemu-aarch64-static |
| Not available | `perf`, `/usr/bin/time`, `strace`. I wrote `tools/rrun.c` instead (fork/exec + `wait4` rusage: wall, user, sys, maxrss, nvcsw, nivcsw) |
| Note | Other agents ran `hc` red-team jobs on the box during this window (see §7.6) |

## 2. Binary and compiler audit

### 2.1 Identity
- `this repository` HEAD = `9eb4d7024031dedcb40829054768d1b4f03c356b`. Tree is clean.
- `build/current -> history/20261119-193456-9eb4d70`. `hc --version` prints `hc 0.1.0`. The binary is an x86-64 PIE built by GCC 14.2.0.
- `hc`'s own dynamic imports (`nm -D`) are libc file I/O, `fork`, `execvp`, `waitpid`, `mkstemps`, and string functions. There are **no** `pthread_*`, `sem_*`, futex, or lock imports.

### 2.2 What `hc` actually does (from `src/main.c`, `src/backend_cc.c`, `src/preproc.c`)
1. It reads the raw source. For `.hc` files only, it lowers the source to temporary C (`src/lower.c`). A `.c` file is **compiled unchanged**.
2. It runs `<cc> -E -std=c11 -D__HC_COMPILER__=1 <-I/-D…> src` into a temporary `.i` under the hardcoded path `a local scratch tmp directory used by that revision`. It then lexes, parses, and runs the HC-E001..E026 verifier.
3. Codegen is `execvp(<cc>, "-std=c11 -Wall -Wextra" + passthrough flags (-O3, -D, -I, plus its own -Iruntime/ and include/) + -c src -o out)`. `<cc>` is `gcc`, or `aarch64-linux-gnu-gcc` when `--target=aarch64*` is set. There is no `-D__HC_COMPILER__` at codegen, so `HS_*` annotations compile away.
4. After compiling, it runs a pure-C ELF64 symbol audit on the **object**. On failure it removes the object.

So HC is a **verifier front-end with a pass-through gcc backend**. It injects no optimization flags, no runtime code, and no `hc_rt.h` unless the source includes it.

### 2.3 hc output vs stock gcc output
`build_hs.sh` compiles every translation unit with `hc … -O3`, then links with plain `gcc` / `aarch64-linux-gnu-gcc -static`.

| Binary | x86_64 size | x86_64 SHA256 (hc == gcc) | ARM64 size | ARM64 SHA256 (hc == gcc) |
|---|---|---|---|---|
| handshake-test | 34,992 | `6910882aacc749dfcbe53dac62ef81d7708b8feb882f8fba7257c1c4c5b34a2e` | 793,464 | `529f35bb77b2ebd9ce8430d3e57609d0be0cc7734d21e5a7f99bde66f09f1f63` |
| handshake-bench | 26,504 | `9d5ce72833212df959bfdb7726fe314ee35323e2f54e43169de17df44a1499a7` | 792,600 | `87300b28caebfcc25dcf6f13669618aff5688d603c96705be054da445f6ca721` |
| handshake-gb-test | 87,576 | `6159280cc052e8be187b1d566f0899360811b6b26d70e1cc940692364aba8641` | 862,984 | `e1a9534676948e146336afde68f213ce14dff6f48b3d890b6f318f496ab85b72` |

- **All 6 hc binaries are byte-identical to the stock-gcc binaries** in `the sibling HandshakeOS build/{host,aarch64} tree`. That covers SHA256, `nm`, full `objdump -d`, `.comment` (`GCC: (Debian 14.2.0-19) 14.2.0`), ELF notes, and Build-ID (x86 test Build-ID `d169d12e…` matches on both sides). Evidence: `logs/sha256.txt`, `logs/binary_diff.txt`.
- All 14 shared `.o` files in `hs-verify/build/hc-host` vs `build/host` (and the aarch64 pair) are byte-identical.
- I reproduced this independently in `repro/`. Plain `gcc -std=c11 -Wall -Wextra -O3 -D_GNU_SOURCE -Iinclude -c` and my own `hc` run produce the **same object SHA256** as the prebuilt hc objects (`test_handshake.o` 7a890381…, `bench_jitter.o` 912fe445…, `handshake.o` 09bf41d2…). Linking the gcc-repro object gives the same `handshake-test` SHA (`6910882a…`).
- The same holds for my own harness: `hc --allow-runtime -O3 -Werror` and `gcc -O3 -Werror` gave identical objects on x86_64 and on aarch64 (`--target=aarch64-linux-gnu`).
- **Codegen difference vs gcc: none.** "hc-compiled" binaries are a compile-time *verification* certificate, not different machine code.

### 2.4 Lock-primitive audit (`nm`, `nm -D`, `nm -u`, `readelf`)
| Artifact | Lock or futex symbols found |
|---|---|
| All HandshakeOS `.o` (x86 + a64) | Undefined refs only: `pthread_create`, `pthread_join`, `clock_gettime`, `syscall` (in `handshake.o`). **No** `pthread_mutex*`, `sem_*`, `pthread_spin*`, `rwlock`, or `cond` references. |
| x86_64 dynamic executables | Imports: `pthread_create`, `pthread_join`, `syscall`, `clock_gettime`, `usleep`, stdio and malloc. **No mutex, sem, or cond imports.** |
| ARM64 **static** executables | glibc's static archive pulls in `pthread_mutex_lock/unlock/trylock`, `pthread_cond_*`, `pthread_rwlock_*`, `__futex_lock_pi64`, `getrandom`, `_dl_random`, `object_mutex`. HandshakeOS code does not call these. They come with libc (stdio, malloc, dl, and so on). |
| `hc --allow-runtime --audit-elf` on the final static ARM64 `handshake-test` | **FAIL (rc=30)**: getrandom, pthread_cond_timedwait, and others. The audit runs only on objects, so linked libc code is outside HC's guarantee. |
| futex | Used on purpose: raw `syscall(SYS_futex, FUTEX_WAIT_PRIVATE/FUTEX_WAKE_PRIVATE)` in `src/handshake.c:90-106`. The runtime profile permits this. The component profile correctly rejects `handshake.o` for `syscall`, `pthread_*`, and `clock_gettime`. |

Note: at ELF level the runtime profile has to allow the generic `syscall` symbol. Only the source-level verifier (HC-E002) can tell `FUTEX_WAIT_PRIVATE` apart from `FUTEX_LOCK_PI`.

## 3. Determinism sweep

### 3.1 CLI, read from source (not guessed)
- `handshake-test` (`tests/test_handshake.c`) takes **no arguments or environment variables**. Each run does unit tests, then a 100-run scoreboard at workers=3, then `test_worker_invariance` at workers **1, 2, 3, 4**. It prints `FAIRNESS workers=N hash=… MATCH|MISS` and exits 0 only if everything passes.
- `handshake-gb-test` (`tests/test_gb_handshake.c`) takes **no arguments**. It needs CWD = repo root for `roms/handshake-demo.gb`. It does 100 runs at workers=3, then workers 1..4, and checks ledger, frame CRC, and audio CRC.
- `handshake-bench` (`tests/bench_jitter.c`): **`argv[1]` = worker count** (`atoi`, default 3). It runs one 128-phase scoreboard and prints the ledger, jitter percentiles, and overruns.
- So the 1..4 worker selection is internal to `-test` and `-gb-test`. I swept `handshake-bench 1|2|3|4` to get an explicit per-worker ledger.
- The golden ledger `5f44cefa72e8ac19` applies to the timing scoreboard (`-test`, `-bench`). The GB test has its own secondary goldens: **ledger `5a45548614037e20`, frame `33e3294d476b16a6`, audio `8c6078dd78fa5589`** (epoch 90).

### 3.2 Results (`sweep.sh` → `sweep.csv`, raw logs in `logs/sweep/`)
| Arch | Compiler | Binary | Repeats | Exit 0 | Golden match | Crashes or signals |
|---|---|---|---|---|---|---|
| x86_64 | hc | handshake-test (w 1–4 + 100×w3 inside) | 10 | **9** | every hash printed = 5f44…; 1 run had a freeze (sentinel 0) | 0 |
| x86_64 | gcc | handshake-test | 10 | 10 | 10/10 | 0 |
| x86_64 | hc / gcc | handshake-gb-test | 10 / 10 | 10 / 10 | 20/20 (all 3 GB digests + 1..4 fairness ok) | 0 |
| x86_64 | hc / gcc | handshake-bench w=1,2,3,4 | 10 per w / 10 per w | 80/80 | 80/80 `5f44cefa72e8ac19`, epoch 128 | 0 |
| ARM64 (QEMU) | hc | handshake-test | 5 | **2** | every non-zero hash = 5f44…; 3 runs had a freeze | 0 |
| ARM64 (QEMU) | gcc | handshake-test | 3 | **1** | every non-zero hash = 5f44…; 2 runs had a freeze (one in the very first scoreboard run, giving epoch 121) | 0 |
| ARM64 (QEMU) | hc / gcc | handshake-gb-test | 5 / 3 | 8/8 | 8/8 | 0 |
| ARM64 (QEMU) | hc / gcc | handshake-bench w=1..4 | 5 per w / 3 per w | 32/32 | 32/32 | 0 |

Totals: 168 process runs. 0 crashes or signals. **6 non-zero exits, all from `handshake-test` and all the same failure:** `run_phase failed at phase N (frozen=1)`, then hash `0000000000000000`.
- Fairness (workers 1..4) printed `MATCH 5f44cefa72e8ac19` in **every** run, including the 6 failing ones. The failures always happened inside the 100-run workers=3 loop.
- **Parity:** the hc and gcc binaries are byte-identical, and they failed at statistically indistinguishable rates (x86 1/10 vs 0/10; ARM64 3/5 vs 2/3). So the failure is in HandshakeOS, not HC. Root cause and fix are in §6.

## 4. Interleaved A/B `handshake-bench` (`ab_bench.sh` → `ab_bench.csv`, `ab_summary.txt`)
Order was gcc, hc, gcc, hc… for each worker count. x86_64: 20 pairs × w=1..4. ARM64 QEMU: 5 pairs × w=1,4. Latency is phase open-to-commit over 128 phases. Medians are across runs. "phases/s" = 1e9 / mean phase latency.

| Arch | w | Compiler | p50 µs | p95 µs | p99 µs | max µs (median / worst) | mean µs | phases/s | overruns | wall ms | user ms | sys ms | nvcsw | nivcsw |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| x86_64 | 1 | gcc | 7.6 | 8.6 | 31.8 | 161 / 381 | 9.0 | 110,632 | 0 | 2.22 | 1.39 | 0.00 | 258 | 0 |
| x86_64 | 1 | hc | 7.8 | 8.6 | 33.9 | 181 / 730 | 9.4 | 106,411 | 0 | 2.18 | 1.65 | 0.00 | 258 | 0 |
| x86_64 | 2 | gcc | 8.6 | 9.6 | 92.8 | 292 / 427 | 11.7 | 85,789 | 0 | 2.55 | 2.01 | 0.00 | 386 | 0 |
| x86_64 | 2 | hc | 8.4 | 9.5 | 32.1 | 202 / 361 | 10.3 | 97,215 | 0 | 2.26 | 2.16 | 0.00 | 386 | 0 |
| x86_64 | 3 | gcc | 8.6 | 10.1 | 50.2 | 210 / 471 | 12.3 | 81,553 | 0 | 2.53 | 2.56 | 0.00 | 514 | 0 |
| x86_64 | 3 | hc | 8.8 | 10.1 | 107.8 | 268 / 471 | 12.8 | 78,355 | 0 | 2.57 | 2.53 | 0.00 | 515 | 0 |
| x86_64 | 4 | gcc | 9.7 | 11.6 | 175.3 | 288 / 447 | 13.5 | 74,123 | 0 | 2.84 | 2.19 | 0.75 | 643 | 0 |
| x86_64 | 4 | hc | 9.5 | 13.5 | 204.7 | 306 / 475 | 16.1 | 62,195 | 0 | 3.04 | 2.90 | 0.00 | 643 | 0 |
| aarch64 QEMU | 1 | gcc | 10.5 | 12.1 | 151.4 | 2026 / 2455 | 28.0 | 35,741 | 3 | 23.1 | 17.8 | 3.7 | 459 | 3 |
| aarch64 QEMU | 1 | hc | 11.5 | 13.1 | 159.8 | 1384 / 2251 | 22.8 | 43,781 | 1 | 23.4 | 19.5 | 4.9 | 454 | 1 |
| aarch64 QEMU | 4 | gcc | 11.4 | 20.2 | 208.1 | 1423 / 1550 | 26.9 | 37,164 | 0 | 29.3 | 27.2 | 3.9 | 981 | 2 |
| aarch64 QEMU | 4 | hc | 11.6 | 18.9 | 282.3 | 1328 / 1517 | 28.1 | 35,571 | 0 | 29.1 | 27.0 | 4.5 | 994 | 2 |

Paired hc−gcc p50 deltas on x86_64: w1 +1.2% (hc slower in 11/20 pairs), w2 −1.1% (9/20), w3 −2.0% (7/20), w4 −0.2% (10/20). All are consistent with zero.
- **Interpretation:** the binaries are byte-identical, so this is really an **A/A noise-floor measurement**. The differences in p99, max, and mean (up to ±20% at w=4) show this box's run-to-run variance for a 128-phase run. They are not a codegen effect. In any future A/B where HC really changes codegen, a p99 or max difference under ~±25% on 128-phase runs should not be read as signal on this box.
- All 160 x86 and 20 QEMU bench runs exited 0 with ledger `5f44cefa72e8ac19`. Context switches are deterministic per worker count (258/386/514/643 voluntary). The involuntary count is ~0.
- The QEMU overruns (4 phases > 2 ms budget) and ~1.3–2.5 ms max values are **TCG emulation latency, not H700 silicon**. Instruction behavior matched (same ledger, same ctx-switch pattern).

## 5. `hc_rt.h` audit and micro-benchmarks

**Important context:** no HandshakeOS source includes `hc_rt.h`. `grep` finds no `hc_rt`, `HS_PHASE`, or `__HC_COMPILER__` in `src/`, `include/`, or `tests/`. None of these primitives are in the shipped binaries. My findings below apply to future adoption.

### 5.1 Correctness harness (`rt/rt_bench.c`)
`hs_fnv1a64` is the **real out-of-line symbol linked from HandshakeOS `handshake.o`**, plus a verbatim inline copy. The harness was built with gcc and with hc (identical objects) for x86_64, and cross-built static for aarch64 and run under QEMU.

| Test | x86_64 | aarch64 (QEMU) |
|---|---|---|
| FNV word API: 6 edge seeds (incl. 0, basis, ~0, 2^63, golden) × 64 single-bit words, their complements, 256 byte patterns; plus 4M random (h, w) pairs, half with h=0; plus 100k checks vs standard FNV-1a over the 8 LE bytes. Byte-buffer adapters: lengths 0..64 × offsets 0..7 × all 256 fill bytes, plus 200 random 1 KB/64 KB buffers × 8 offsets, vs the reference byte loop. Standard vectors ("", "a", "foobar") | **4,506,400 checks, 0 fail** | **4,506,400, 0 fail** |
| `hc_bitpack_u32x4`: 40⁴ = 2.56M edge combos (0, 1, 2, 0x7FFFFFFF, 0x80000000, ~0, ~1, 0x55555555, all 32 single bits) × (pack + unpack), plus 2M random checked against the LE byte layout | **7,120,000, 0 fail** | **7,120,000, 0 fail** |

**API semantics:** `hc_fnv1a64_word(hash, word)` mixes **one 64-bit word** (8 bytes LSB-first via shifts). It is not a byte-buffer hash. It is bit-identical to `hs_fnv1a64` for every input tested. Because it uses shifts, it is endianness-independent, which matters for x86/ARM ledger parity. To hash a buffer, the caller must chunk it. My adapters show that `memcpy` loads match only on little-endian, while the explicit LE assembly matches everywhere.

### 5.2 Audit findings (C11 / asm)
| # | Primitive | Finding | Severity |
|---|---|---|---|
| R1 | `hc_fence_publish` | ARM64 emits **`dmb ishst`** (store→store only). C11 `atomic_thread_fence(memory_order_release)` on ARM64 emits **`dmb ish`**, verified in `asm/codegen.aarch64.asm`. x86 and the portable fallback do give release semantics, so the **meaning differs by architecture**. On ARM64, loads before the fence can be reordered after the publishing store, so a producer that reads a buffer and then "publishes it back" can see it overwritten early. Fix: use `dmb ish` (or `atomic_thread_fence(release)`), or rename it `hc_fence_store_store` and document it. | **Medium** (correctness, latent; not in shipped binaries) |
| R2 | `hc_bounded_wait_u32` on ARM64 | It uses `ldar` + `sevl`/`wfe`. **`ldar` does not arm the exclusive monitor**, so another core's store to `*word` generates **no wake event**. After `sevl` lets the first `wfe` through, every later `wfe` sleeps until an unrelated event: an IRQ, a `sev`, or the Linux arch-timer event stream (~100 µs at 10 kHz). On real H700 silicon each spin iteration can therefore cost up to ~100 µs, and a "bounded" 64-spin wait can take milliseconds. That contradicts the "early-exit" claim in the commit message. Fix: load with `ldaxr` (arms the monitor, the same as Linux `__cmpwait`/`smp_cond_load_acquire`), or require wakers to call `hc_event_wake()` (`sev`) after their `stlr`. QEMU does not model WFE, so the box cannot measure this; it needs a check on real hardware. | **High for H700 latency** (if adopted) |
| R3 | `hc_fence_barrier` on x86 | Uses `mfence`. GCC 14 `atomic_thread_fence(seq_cst)` emits `lock orq $0,(%rsp)`. Measured cost **10.0 ns vs 4.6 ns (2.2× slower)**. Fix: use `atomic_thread_fence(memory_order_seq_cst)`, or `lock orq`. ARM64 `dmb ish` matches C11. | Low (perf) |
| R4 | `hc_load_acq_*` / `hc_store_rel_*` | Codegen is correct and identical to C11: ARM64 `ldar`/`stlr` with a `"memory"` clobber, x86 plain `mov`. But the signatures take `volatile uint64_t*`/`uint32_t*`, while HandshakeOS keeps every shared word as `_Atomic`. **Passing `&_Atomic` is a hard error under GCC 14** (`-Wincompatible-pointer-types`; reproduced via plain gcc **and** via `hc`, see `rt/alias_probe.c`). Accessing an `_Atomic` object through a non-atomic lvalue is UB in ISO C. So the "strict-aliasing atomics" claim does not hold for the real call sites. Fix: provide `_Atomic`-typed overloads, or use `_Generic` wrappers around `atomic_load_explicit`. Misaligned pointers fault on `ldar`/`stlr` (alignment is architecturally required), and nothing asserts alignment. | Medium (integration/UB) |
| R5 | `hc_bitpack_u32x4` | Correct and endian-safe (arithmetic, scalar-authoritative). The trailing `asm volatile("" ::: "memory")` is a compiler barrier with no SIMD path behind it: the comment mentions NEON/x86 paths that **do not exist**. GCC turns the pack into a plain 16-byte copy (`ldr q`/`str q`, `movdqu`). Writing through `uint64_t*` with `uint32_t*` input means an aliased in-place call is strict-aliasing UB. It behaved correctly with GCC 14 -O3, but only by luck. Fix: remove the barrier, fix the comment, and either document no-alias or use `memcpy`. | Low |
| R6 | `hc_cycle_counter` | ARM64 `mrs cntvct_el0` has no `isb`, so it can be speculated or reordered (Linux uses `isb` first). x86 uses `lfence; rdtsc`, which is fine. `hc_cycle_freq()` returns **0 on x86**, so callers that divide by it will hit divide-by-zero. Telemetry-gated. | Low |
| R7 | `hc_fnv1a64_word` / `hs_fnv1a64` | No UB: unsigned wraparound and shifts < 64. Inherited quirk: `hash == 0` is silently replaced by the offset basis, so `word(0,x) == word(basis,x)` (verified), which differs from standard FNV-1a from state 0. Harmless for the ledger, but document it. | Info |
| R8 | `hc_cpu_relax` fallback | `atomic_thread_fence(relaxed)` is a no-op. Fine. On x86, `pause` costs ~10.5 ns on this CPU. | Info |

### 5.3 Micro-benchmarks (median of 11 interleaved rounds on x86_64; 5 under QEMU)
FNV, ns per buffer (x86_64):

| Bytes | hc_fnv1a64_word (inline) | hs_fnv1a64 (extern .o) | hs inline copy | byte-loop FNV-1a |
|---|---|---|---|---|
| 8 | 2.78 (2.88 GB/s) | 3.29 (2.43) | 2.82 | 4.09 |
| 16 | 6.32 (2.53) | 6.87 (2.33) | 6.27 | 5.81 |
| 64 | 42.3 (1.51) | 42.9 (1.49) | 41.8 | 38.7 |
| 4 KiB | 4671 (0.877) | 4665 (0.878) | 4667 | 4264 (0.961) |
| 64 KiB | 72036 (0.910) | 73066 (0.897) | 73602 | 67740 (0.967) |
| Dependent word-chain (ledger-fold pattern), ns/word | **4.83** | **8.96** | 4.82 | – |

Under QEMU (aarch64): hc 0.83 GB/s vs extern hs 0.48 GB/s at 4 KiB, chain 5.84 vs 16.8 ns/word. Relative only.

What these numbers show: FNV-1a is serial and bound by multiply latency, so the word API is not faster per byte than a byte loop on large buffers. The real cost is the **out-of-line call to `hs_fnv1a64`: about 1.9× slower in a dependent fold** (x86) and 2.9× under QEMU. The `hc_fnv1a64_word` and inline-copy numbers are the same because they generate the same code.

bitpack (4096 × u32x4, 64 KiB): hc **0.293 ns/pack (54.7 GB/s)** vs plain C 0.459 (34.9 GB/s), with or without restrict. On QEMU: 3.04 vs 6.01 ns. The plain-C loop is slower because GCC's vectorizer emits shuffles (`ld4`/`zip`/`shll` on ARM64), while the hc loop's barrier pushes GCC into a plain copy loop. The real answer for LE targets is that the pack is the identity, so a `memcpy` loop is optimal.

Fences and atomics, ns/iteration including ~0.13 ns of loop overhead (x86_64):

| Primitive | hc | C11 |
|---|---|---|
| load acquire u64 | 0.255 | 0.266 |
| store release u64 | 0.260 | 0.527 (both are a single `mov`; this is a loop-alignment artifact) |
| publish / release fence | 0.263 | 0.263 |
| pull / acquire fence | 0.259 | 0.259 |
| barrier / seq_cst fence | **10.006 (mfence)** | **4.636 (lock or)** |
| cpu_relax (`pause`) | 10.56 | – |

Two-thread ping-pong (pinned cpu0↔cpu1, 1M round trips × 11 rounds): hc load_acq/store_rel median **119.0 ns** (min 113.9, max 127.8) vs C11 acquire/release **120.6 ns** (117.8 / 123.8). These are the same.
Under QEMU (20k trips × 5): 381 vs 332 ns median, which is noise from TCG thread scheduling. `yield` costs 89.7 ns under QEMU, an artifact of the emulator ending its translation block. Fences are ~no-ops under TCG.

## 6. Root cause of the `handshake-test` freezes (HandshakeOS, not HC)
`worker_main` (`src/handshake.c` ~L411–430):
```c
while (!hs->stop) {
    if (hs_hs_poll_phase(...)) { ...; continue; }   // (1) poll fails: no new phase yet
    if (hs->stop) break;
    word = atomic_load(&hs->publish_word);          // (2) sampled AFTER the poll
    futex_wait(&hs->publish_word, word);             // (3)
}
```
If the clock thread's `publish()` (`publish_word++`, `futex_wake`) lands between (1) and (2), the worker samples the **new** value and goes to sleep on it. It has missed the phase it was supposed to serve. The clock thread then waits at the barrier for that worker until the 250 ms watchdog trips FREEZE, so `run_phase` returns false and the ledger is 0. The failing x86 run took 0.875 s vs ~0.60 s normally, which is ≈ one 250 ms watchdog interval.

**Fix:** sample `publish_word` *before* polling (`fixcheck/worker_lost_wakeup.patch`, 3 lines moved). A/B with copies built in `data-eval/fixcheck/` (the originals were not modified):

| Build | Mode | Runs | Failed runs | Freeze events | Median wall |
|---|---|---|---|---|---|
| orig `handshake-test` | x86 free + `taskset -c 0,1` | 60 | 1 | 1 | 0.57–0.59 s |
| fixed | x86 free + `taskset -c 0,1` | 60 | **0** | **0** | 0.57–0.59 s |
| orig | aarch64 QEMU | 15 | **8** | 9 | 1.247 s |
| fixed | aarch64 QEMU | 15 | **0** | **0** | 1.013 s |
| orig `handshake-bench` w1/w4 | QEMU | 30 | 2 (ledger 0, exit 1) | – | – |
| fixed | QEMU | 30 | 0 | – | – |

Every fixed run printed all 4 `FAIRNESS … 5f44cefa72e8ac19 MATCH` lines plus `ALL TESTS PASSED`. The golden value is unchanged.

Secondary, suspected: `arrive()` does `atomic_store(&barrier_word, n)`. If `n` equals the value the clock thread already sampled (the same arrival count as the previous phase), the store does not change the futex word, so the wake can be lost until the next 2 ms re-check slice. `fixcheck/barrier_word.patch` (`fetch_add` instead) showed **no measurable difference** in 30 QEMU and 30 x86 runs, so it is inconclusive. It is still the textbook-correct form.

Also noted, not fixed: `hs->stop` is a plain `int` read concurrently by workers, and the seqlock payload fields are read non-atomically. Both are formally data races (UB) in C11, though benign with GCC today.

## 7. Actionable items for v0.1.1
1. **HandshakeOS (blocks 100% green CI and real-hardware liveness):** apply `worker_lost_wakeup.patch`. Consider `barrier_word.patch` and making `stop` `_Atomic`. Then re-run the Layer-1/2 sweeps.
2. **`hc_rt.h` R2 (H700):** make `hc_bounded_wait_u32` use `ldaxr` (or require `sev` from wakers), then measure on real H700 silicon. QEMU cannot show this.
3. **`hc_rt.h` R1:** `hc_fence_publish` on ARM64 should be `dmb ish` to match its x86 and fallback release semantics, or be renamed and documented as store→store only.
4. **`hc_rt.h` R4:** add `_Atomic`-typed acquire/release helpers (or `_Generic`) so HandshakeOS's `_Atomic` fields can use them without UB or GCC 14 hard errors.
5. **`hc_rt.h` R3:** replace `mfence` with `atomic_thread_fence(seq_cst)` (2.2× cheaper on this Xeon).
6. **`hc_rt.h` R5/R6:** remove the barrier from bitpack and the comment about non-existent SIMD paths. Add `isb` before `cntvct_el0`. Make `hc_cycle_freq` meaningful on x86 or document that it returns 0.
7. **Perf opportunity:** make `hs_fnv1a64` `static inline` in `hs_handshake.h`, or switch HandshakeOS to `hc_fnv1a64_word`. The dependent ledger fold is ~1.9× faster on x86 (2.9× under QEMU). The ledger value is unchanged (bit-identical, verified).
8. **Claims hygiene for the Layer-3 bundle:** state that HC output is byte-identical to gcc (HC is a verifier plus a gcc driver), that `hc_rt.h` is not yet used by HandshakeOS, and that "zero mutex" covers the HandshakeOS TUs while the static ARM64 executables carry glibc's internal lock code.
9. **hc tooling:** the scratch path `a local scratch tmp directory used by that revision` and the `-Ithis repository/...` paths are **hardcoded** in `src/main.c`, so HC breaks if the repo moves (make them relative to the binary, or use `$TMPDIR`). The tmp dir holds 400+ stale files from older pid-named builds. During this window, new `mkstemps` leftovers came from **another agent's** red-team inputs (`hc-redteam-v01/escapes/crash01_deep_callchain_265.c`, `p12_deep_chain.c`). That suggests HC crashes (likely deep recursion) leave temp files behind. My own HC runs leaked none. I did not reproduce the crash; that belongs to the red-team lane.

## 8. Reproduce
```sh
cd docs/eval
./sweep.sh            # determinism sweep -> sweep.csv, logs/sweep/
./ab_bench.sh         # interleaved A/B  -> ab_bench.csv (summary: ab_summary.txt)
rt/rt_bench.gcc.x86                       # hc_rt.h correctness + benches (x86_64)
qemu-aarch64-static rt/rt_bench.gcc.a64 --quick
# codegen listings: asm/codegen.{x86_64,aarch64,aarch64-a53}.asm, asm/hs_fnv1a64.*.asm
# freeze root-cause A/B: fixcheck/{worker_lost_wakeup,barrier_word}.patch, fixcheck.csv, benchfix.csv
```

## 9. Boundaries
- Nothing was written in `this repository`, `the sibling HandshakeOS tree`, or `a local hs-verify tree`. `find -newer` shows no changed files. `this repository` HEAD is still `9eb4d70`, with a clean tree.
- One side effect to disclose: my read-only `git status`/`git diff` in `hs-verify` refreshed git's stat cache, `hs-verify/.git/index` (mtime 19:37). No content, commits, or refs changed.
- HC's own transient temp files were written to and removed from `a local scratch tmp directory used by that revision`, as HC is designed to do.
- No commits, no pushes, no remote machines.
