# HC v0.1.1 (`c584ec8`): Layer-2 independent re-check (Mr Data · Training & Evaluation)

Date: 2026-11-19, 20:40–20:55 EST. Box only. Artifacts are in `docs/eval/v011/` (`probes/`, `r4/`, `rt/`, `asm/`, `objs/`, `logs/`).
Baseline: `REPORT.md` (v0.1, `9eb4d70`). To compare like-for-like, I also rebuilt the v0.1 header (`git -C . show 9eb4d70:runtime/hc_rt.h` → `v011/rt/hdr_v010/`) and re-ran it in this same session.

## Verdict: **PASS WITH FINDINGS**

- R1, R2, R3, R4 (every requested pointer type), and R5 are fixed. I verified each one in the asm and by running it on x86_64 and ARM64 (QEMU).
- R6 is only half fixed. The ARM64 `isb` is there, but `hc_cycle_freq()` still returns 0 on x86_64 and the header doesn't say so.
- R7 (`hash==0`) is still undocumented.
- Tooling hygiene is fixed for every normal exit path. Killing `hc` with SIGTERM still leaves temp dirs behind.
- HC 0.1.1 still produces objects byte-identical to gcc for every HandshakeOS translation unit I tested. One exception: with `-g`, the DWARF line/column tables differ, though the code sections are still identical.
- None of the remaining items is a blocker.

## 1. Identity, tooling, and honesty

| Check | Result |
|---|---|
| `git -C . rev-parse` / `status --porcelain` | `c584ec8`, empty (clean). `build/current -> history/20261119-203753-c584ec8` |
| `hc --version` | `hc 0.1.1` |
| `nm -D hc` | No `pthread`, `mutex`, `sem`, `futex`, `spin`, `rwlock`, or `cond` imports |
| Repo-root discovery | `resolve_root()` does `readlink("/proc/self/exe")`, then walks up to the first directory containing `runtime/hc_rt.h` **and** `include/hc_contract.h`. If that fails it silently falls back to `this repository` (Info: a relocated binary would quietly pick up `this repository`'s header) |
| Private temp dir | `mkdtemp("$TMPDIR/hc.XXXXXX")` (default `/tmp`), mode **0700** (verified with `stat`). Files inside are 0600. A single `done:` label unlinks `lowered.c`, `pp.i`, and `pp.stripped.i`, then `rmdir`s the dir |
| Exit-path leak test (isolated `TMPDIR=v011/tmpdir`, CWD `v011/probes`) | Clean, rc 0 ✓ · HC-E001 violation (`pthread_mutex_lock`), rc 1 ✓ · preprocess error, rc 1 ✓ · gcc backend error, rc 1 ✓ · missing input, rc 2 ✓ · `--check-only` violation, rc 1 ✓. **0 leftovers after every one.** No object is emitted on any failure |
| Same tests from CWD `/tmp` with default `TMPDIR` | Valid rc 0, violation rc 1. No `/tmp/hc.*` left behind by my runs |
| Legacy `a local scratch tmp directory used by that revision` | **Removed.** `sweep_legacy_tmp()` deletes old `hc_pp_*`/`hc_lower_*` files and `rmdir`s the dir on every run. The directory no longer exists |
| **Signal paths** | **SIGTERM ×10 mid-compile → 10/10 `hc.*` dirs leaked** (there is no `sigaction` handler). SIGKILL ×15 → 15 leaked, plus orphaned `cc1` `.s` files written into `TMPDIR` (SIGKILL can't be caught, so this one is expected). Background SIGINT can't be tested from a non-interactive shell. The stale-sweep only cleans the legacy dir, not old `$TMPDIR/hc.*` dirs |

**Codegen pipeline change in v0.1.1 (verified in source):**
1. `gcc -E -D__HC_COMPILER__=1` with all of your `-I`, `-D`, and codegen flags.
2. The verifier runs on that `.i`.
3. `hc_strip_annotations()` removes only `__attribute__((… annotate … "hs:" …))` groups. It is aware of strings and character literals.
4. `gcc -x cpp-output` compiles the stripped `.i`.

The backend no longer adds `-Wall -Wextra`; only your own flags are passed.

**README:** it now has an accurate "What v0.1.x actually is" section. It says HC is a verification plus `.hc`-lowering gate over gcc, that codegen is gcc, and that output is byte-identical for plain annotated `.c`. It separates "zero mutexes" for HC-compiled translation units from the glibc `libc.a` lock symbols that come in with a `-static` link. Remaining nits:
- (a) The "byte-identical" claim needs "without `-g`", or "code/data sections" (see §3).
- (b) It doesn't say outright that HandshakeOS does not include `hc_rt.h` yet.
- (c) The opening "Zero mutexes. Zero locks." line is still unqualified (the honest-scope section below it does qualify it).
- (d) It says the audit "fails closed on a stripped/static binary it cannot prove (no symbol table)". In practice the static ARM64 `handshake-test` *has* a symbol table, and it fails because of real libc symbols (`pthread_cond_timedwait`, …, rc=31). The outcome is the same; the stated reason is wrong.
- (e) The EVTSTREAM note says writers must `SEV` when the event stream is off. Architecturally, a store that clears the LDAXR monitor already generates the wake event. The note is conservative, so it's harmless.

## 2. `hc_rt.h` audit: asm and runtime

Probes: `v011/probes/codegen.c` with `-DHC_RUNTIME_TELEMETRY`. Every probe was compiled by plain `gcc -std=c11 -Wall -Wextra -Werror -O3` **and** by `hc [--target=aarch64-linux-gnu] --allow-runtime -O3 -Werror`. **hc and gcc objects are identical on both architectures.** Listings: `v011/asm/codegen.{x86,a64}.asm`.

| Item | Status | Evidence |
|---|---|---|
| **R1** | ✅ Fixed | ARM64 `cg_fence_publish`: `dmb ish; ret`. The new `hc_fence_publish_st`: `dmb ishst; ret`, documented as "store-only publish fence — documented, narrower ordering". x86: both compile to nothing but a compiler barrier, which is correct under TSO. `hc_fence_pull` = `dmb ishld` |
| **R2** | ✅ Fixed | ARM64, variable `max_spins`: `ldaxr w5,[x0]; cmp; b.ne → clrex; mov w0,w5; ret` (value already differs) · `cbnz w2` skips the loop when `max_spins==0` → `clrex; ret` · loop: `wfe; ldaxr w0,[x4]; add; cmp; b.eq` → on change `clrex; b ret` · on exhaustion `cmp w2,w3; b.eq → clrex; ret`. Constant 64: `ldaxr w4; … clrex` / `mov w1,#64; wfe; ldaxr w2; … subs; b.ne; clrex`. `max_spins=0`: `ldaxr w0,[x2]; clrex; ret`. **32-bit `w` registers, `clrex` on all 4 exit paths, 0 `stxr`**. The constraints `"=&r"(v)` with `"r"(word)` and a `"memory"` clobber on every asm are correct; I found no clobber bugs. x86 path: plain `mov` plus a `pause` loop bounded by the count |
| R2 runtime (`v011/rt/rt_tests.c`, hc==gcc objects) | ✅ | x86_64 and QEMU both PASS (137/137). Already-different value returns immediately (0.50 ns/call x86, 3.99 ns QEMU) · `max_spins=0` with a match returns 7 · exhaustion returns 7 after n spins (x86 ≈12 ns/spin = `pause`; QEMU ≈95–100 ns/spin, where TCG treats `wfe` as a yield) · concurrent wake with a 1 ms writer saw 8 in both store-only and store+SEV modes · 200,000-round ping-pong built only on bounded_wait plus store_rel: no lost or duplicated values (286 ns/round trip x86, 291 ns QEMU). **QEMU does not model WFE or the exclusive monitor, so the real wake latency on H700 silicon still needs a measurement on hardware** |
| **R3** | ✅ Fixed | x86 `cg_fence_barrier`: `lock orq $0x0,(%rsp); ret` (now `atomic_thread_fence(seq_cst)`). ARM64 is `dmb ish`. Cost: **10.006 → 4.637–5.077 ns**, the same as C11 (4.643–4.974) |
| **R4** | ✅ Fixed for all requested types, with gaps listed below | `_Generic` matrix (`v011/r4/matrix.txt`), each case compiled by gcc-Werror and hc-Werror on x86 and a64 (hc objects == gcc): loads accept `T*`, `volatile T*`, `_Atomic T*`, `const T*`, `const volatile T*`, `const _Atomic T*` for u32 and u64 → ARM64 `ldar` (plain *and* `_Atomic`), x86 `mov`. Stores accept `T*`, `volatile T*`, `_Atomic T*` → `stlr` / `mov`. `const T*` stores are correctly rejected. A HandshakeOS-style struct with `_Atomic` fields compiles (`ldar` + `stlr`). **HC's parser and verifier handle `_Generic` and `_Atomic` cleanly.** **Gaps (Low):** `volatile _Atomic T*` and `const volatile _Atomic T*` give "_Generic selector … not compatible"; `unsigned long long*` is rejected (on LP64, `uint64_t` is `unsigned long`); and **`hc_bounded_wait_u32` is not `_Generic`-wrapped, so `_Atomic uint32_t*` (HandshakeOS's futex-word type) is a hard `-Wincompatible-pointer-types` error** |
| **R5** | ✅ Fixed, no performance regression | The barrier and the stale SIMD comment are gone. All four lanes are read into locals before either store. ARM64: `ldr q31,[x0]; str q31,[x1]`. x86: `movdqu`/`movups`. **The loop form is still a plain 16-byte copy loop on both architectures**, not the slower shuffle-vectorized loop. Throughput 0.296/0.289 ns/pack (v0.1: 0.293). Correctness: 40⁴ edges plus 2M random (`rt_bench`, PASS on x86 and QEMU). Aliasing: 56 placements (lanes offset 0..6 u32 × out offset 0/2/4/6, including exact in-place and partial overlaps, inlined and noinline, `-O3` with strict aliasing on, plus a `-fno-strict-aliasing` build) all correct, with no writes outside `out[]`, on x86 and QEMU. Note: type-punned aliasing (`uint64_t*` over `uint32_t` storage) is still formally UB in ISO C. It only works because every load happens before any store. Document that, or tell callers to alias through `memcpy`/char storage |
| **R6** | ⚠️ Partial | ARM64 `cg_cycle`: `isb; mrs x0, cntvct_el0; ret` ✅. x86 `cg_freq`: `xor %eax,%eax; ret`. **`hc_cycle_freq()` still returns 0 on x86_64 and is not documented**, so a caller dividing by it will hit divide-by-zero. x86 counter: `lfence; rdtsc` (unchanged, fine) |
| **R7** | ⚠️ Not documented | 2M random (25% with h==0) match the real `hs_fnv1a64`. The `hash==0` → offset-basis quirk is unchanged and still has no comment in the header |

## 3. hc 0.1.1 sanity compile (HandshakeOS → `v011/objs/`)

Compiled `src/handshake.c`, `tests/test_handshake.c`, `tests/bench_jitter.c`, and `src/scoreboard.c` with `-O3 -D_GNU_SOURCE -Iinclude -Wall -Wextra -Werror`.

| TU | x86_64 gcc = hc | aarch64 gcc = hc |
|---|---|---|
| handshake | `09bf41d21660d558` ✅ (also equals the v0.1 object) | `95f08649d56078ec` ✅ |
| test_handshake | `7a890381dbcff5b0` ✅ | `4fd85f70ecd6a0aa` ✅ |
| bench_jitter | `912fe445fbdad4e2` ✅ | `886c4e5e447103fb` ✅ |
| scoreboard | `8fd090f567aef322` ✅ | `bf7ae82ea2ba3832` ✅ |
| handshake **with `-g`** | **differs**: `.text`, `.data`, and disassembly identical; `.debug_line` 0x29cc→0x2c0b and `.debug_abbrev` differ (column numbers, from compiling a `.i` instead of integrated preprocessing) | differs (same cause) |

0 temp leaks across all of these compiles. Also, `hc --audit-elf`: the x86 `handshake.o` and `handshake-test` PASS; the static ARM64 `handshake-test` FAILs (rc=31, libc lock symbols), as expected.

## 4. Micro-benchmark deltas (`rt_bench.c`, unchanged harness)
x86_64: median of 11 interleaved rounds. "v0.1 hdr re-run" is the old header measured again in this session; "v0.1.1" is two runs.

| Metric (x86_64) | v0.1 report | v0.1 hdr re-run | v0.1.1 run1 / run2 | Delta |
|---|---|---|---|---|
| `hc_fence_barrier` ns | **10.006** | 10.071 | **5.077 / 4.637** | **−49…−54% (= C11)** |
| C11 seq_cst fence ns | 4.636 | 4.677 | 4.974 / 4.643 | – |
| `hc_bitpack_u32x4` ns/pack (GB/s) | 0.293 (54.7) | 0.369 (43.3)* | 0.296 / 0.289 (54.0 / 55.4) | ±0 (no regression) |
| plain C bitpack ns/pack | 0.459 | 0.549* | 0.462 / 0.457 | – |
| FNV word chain ns/word, hc / hs extern | 4.825 / 8.957 | 4.868 / 8.900 | 4.904 / 9.039 · 4.835 / 8.943 | ±0 |
| FNV 8 B ns · 4 KiB GB/s | 2.78 · 0.877 | 2.73 · 0.901 | 2.73 · 0.900 / 2.75 · 0.897 | ±0 |
| load_acq / store_rel / publish / pull ns | 0.255 / 0.260 / 0.263 / 0.259 | 0.256 / 0.257 / 0.260 / 0.263 | 0.260 / 0.261 / 0.258 / 0.258 (run2) | ±0 (all `mov`, compiler barrier) |
| cpu_relax ns | 10.556 | 10.719 | 11.528 / 10.649 | ±0 |
| ping-pong RTT median ns, hc / C11 | 119.0 / 120.6 | 108.6 / 106.4 | 113.6 / 112.4 · 129.5 / 117.4 | noise (identical instructions; varies with core placement) |

\*That v0.1 re-run ran during a load spike (loadavg 3.2→4.2), so it was slower across the board.

| Metric (ARM64 QEMU, 5 rounds; emulated, relative only) | v0.1 report | v0.1 hdr re-run | v0.1.1 |
|---|---|---|---|
| fence_barrier / publish / pull ns | 1.143 / 1.150 / 1.154 | 1.139 / 1.128 / 1.174 | 1.137 / 1.135 / 1.137 (TCG: fences ≈ no-ops) |
| bitpack ns/pack (hc / plain) | 3.041 / 6.010 | 2.465 / 5.094 | 2.471 / 5.370 |
| FNV chain ns/word (hc / extern) | 5.843 / 16.827 | 5.692 / 16.240 | 5.738 / 17.207 |
| ping-pong median ns (hc / C11) | 381 / 332 | 349 / 347 | 375 / 470 (TCG scheduling noise) |

Correctness in `rt_bench` with the v0.1.1 header: PASS on x86_64 and QEMU (FNV 4,506,400 checks, bitpack 7,120,000). The `rt_bench` objects are hc == gcc.

## 5. Remaining items for v0.1.2 (none blocking)
1. **R6:** make `hc_cycle_freq()` meaningful on x86 (for example TSC frequency from CPUID 0x15/0x16, or a calibrated value), or document the 0 return and guard callers.
2. **R7:** add a comment about the `hash==0` → offset-basis quirk.
3. **R4 completeness:** `_Generic`-wrap `hc_bounded_wait_u32` so `_Atomic uint32_t*` works (HandshakeOS futex words). Optionally add `volatile _Atomic` and `unsigned long long*` associations.
4. **Signals:** install a SIGTERM/SIGHUP/SIGINT handler that cleans up the `mkdtemp` dir and kills the gcc child. Optionally sweep stale `$TMPDIR/hc.*` dirs older than N minutes.
5. **README:** say "byte-identical code/data (without `-g`; DWARF line columns differ because codegen compiles the verified `.i`)". State that HandshakeOS does not yet include `hc_rt.h`. Fix the static/stripped audit wording. Qualify the opening "Zero mutexes" line.
6. **R5 doc:** note that aliased use should go through `memcpy`/char-typed storage. Type-punned aliasing works but is still ISO-UB.
7. **R2 on hardware:** measure wake latency on real H700 silicon. QEMU can't model WFE or the exclusive monitor.

Not re-checked here, since it's outside this brief: the HandshakeOS `worker_main` lost-wakeup fix from `REPORT.md` §6. `the sibling HandshakeOS src/handshake.c` is unchanged.

## 6. Boundaries
- I wrote only to `docs/eval/v011/` and this report. My hc runs used an isolated `TMPDIR` (`v011/tmpdir`, now empty), apart from two default-`/tmp` runs, which left no leftovers. `k.o` from the kill tests was removed.
- `this repository`: HEAD is `c584ec8`, porcelain empty, `.git/index` mtime unchanged at 20:37:33 (the commit time). Read-only git commands used: `rev-parse`, `status --porcelain`, `log`, `show`.
- `the sibling HandshakeOS tree`: no files changed. I ran no git commands there.
- `hs-verify`: no `git` was run there; `.git/index` is still at 19:37:36. Its `build/hc-aarch64/*` files were rewritten at 20:43:20–22 by **another agent** (consistent with a `build_hs.sh cross` rebuild). My commands never targeted that path.
- No commits, pushes, or remote machines.
