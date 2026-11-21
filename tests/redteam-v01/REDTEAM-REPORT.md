# Handshake Compiler (HC) v0.1 (`9eb4d70`) — Herominde Layer-2 Red-Team Audit

- **Target:** `build/current/hc` (`hc 0.1.0`, commit `9eb4d70`, read-only audit; `this repository` untouched)
- **ASan target:** `build/asan/hc`
- **Date:** 2026-11-19 (America/New_York)
- **Reproducer runner:** `tests/redteam-v01/run-redteam.sh`

## Executive Summary

- **Layer-1 contract corpus (`tests/cheaters/herominde`):** **46 / 46 PASS** (verified independently).
- **Layer-2 unseen adversarial probes (`29` probes):** **26 ESCAPED** (HC exited `0` and emitted a valid `.o` file), **3 CAUGHT** (`r11`, `r22`, `r26`).
- **Valid edge-case controls (`6` controls):** **2 ACCEPTED**, **4 FALSE POSITIVES / `.hc` lowering corruptions** (`ve01`, `ve04`, `ve05`, `ve06`).
- **Compiler memory-safety probes (ASan `hc`):** **2 REAL ASAN CRASHES** (`stack-buffer-overflow` in `src/verify.c:416`, `heap-buffer-overflow` in `src/lower.c:72`).

---

## P0 — Critical Security-Boundary Bypasses & Compiler Crashes (Fix First for v0.1.1)

### P0-1. Preprocessor / Backend Split-Brain (`#ifndef __HC_COMPILER__` + `-U__HC_COMPILER__` + `-Wp,-include`)
- **Reproducers:**
  - `escapes/r01_ifndef_hc_compiler_splitbrain.c` (**ESCAPED** on both x86_64 and `--target=aarch64-linux-gnu`)
  - `escapes/r29_cli_undef_hc_compiler.c` (exact Layer-1 `c04_atomic_flag_spinlock.c` compiled with `-U__HC_COMPILER__` -> **ESCAPED**, `rc=0`, emitted `.o`)
  - `escapes/r25_cli_wp_include_bypass.c` (`-Wp,-include,r25_injected_spoof.h` -> **ESCAPED**, injected `r25_injected_ctor` spinlock into `.o`)
- **Root cause:**
  1. `src/preproc.c:37` defines `-D__HC_COMPILER__=1` during verification, while `src/backend_cc.c:21-43` compiles the raw source **without** `-D__HC_COMPILER__=1`. Any `#ifndef __HC_COMPILER__` block is completely invisible to `hc_verify()` yet compiled into the final `.o`!
  2. `src/main.c:127-128` passes `-U__HC_COMPILER__` straight to `hc_preprocess()` (erasing all `HS_*` annotations) and forwards arbitrary `-Wp,*`, `-include`, `-imacros`, `-specs=` flags to `backend_cc.c` via `pass[]` without sending them to `hc_preprocess()`.
- **Disassembly proof (live emitted `.o`):**
  - `r01` x86_64: `86 05 ... xchg %al, 0x0(%rip)` + `75 f1 jne` (real `atomic_flag` spinlock in `r01_splitbrain`).
  - `r01` ARM64: atomic spin loop (`b.ne 98`) compiled into `r01_arm64.o`.
  - `r25` x86_64: `r25_injected_ctor` with live `xchg %al` spinlock in emitted `.o`.
- **v0.1.1 Fix:**
  1. Compile the **exact preprocessed `.i` stream** you just verified (stripping only `__attribute__((annotate("hs:...")))` tokens in a deterministic pass, or defining `HS_*` via a dedicated macro that cannot be observed by user `#if`), OR reject any reference to `__HC_COMPILER__` in user source and reject `-U__HC_COMPILER__`/`-D__HC_COMPILER__` on the CLI.
  2. Allowlist passthrough flags to `backend_cc.c` (block `-Wp,*`, `-include`, `-imacros`, `-specs`, `-B`, `-wrapper`, `--param`, `-fplugin*`, or forward every preprocessor flag identically to `hc_preprocess` and verify the resulting TU).

### P0-2. Unannotated Functions & `__attribute__((constructor))` Are Never Audited (`if (!PA) return;`)
- **Reproducers:**
  - `escapes/r02_unannotated_worker_spinlock_rdtsc.c` (**ESCAPED**: unannotated `hs_hs_work_fn` embeds `atomic_flag` spinlock + `rdtsc` + shared global write)
  - `escapes/r03_constructor_hidden_lock.c` (**ESCAPED**: `__attribute__((constructor))` embeds inline-asm `lock xchgl; pause`)
- **Root cause:** `src/verify.c:152`, `224`, `257` early-return when `!phase_any(f)`. Any function without an `HS_PHASE_*` annotation (including callbacks registered via function pointers and ELF `.init_array` constructors) completely skips lock, atomic-spin, inline-asm, cycle-counter, and volatile-wait checks!
- **v0.1.1 Fix:** Enforce TU-wide bans (`HC-E001`..`HC-E006`, `HC-E020`..`HC-E026`, plus `constructor`/`destructor` ban in component profile) across **every** function in the TU regardless of `HS_PHASE_*` annotation, not just `phase_any(f)`.

### P0-3. Inline-Asm Adjacent String Concatenation & Raw `.byte`/`.inst` Machine-Code Bypass
- **Reproducers:**
  - `escapes/r04_asm_adjacent_string_concat_rdtsc.c` (**ESCAPED** on x86_64 `0f 31 rdtsc` AND ARM64 `d53be040 mrs x0, cntvct_el0`)
  - `escapes/r05_asm_adjacent_string_concat_lock.c` (**ESCAPED**: `"lo" "ck " "xc" "hgl %0, %1; pa" "use"`)
  - `escapes/r06_asm_raw_byte_rdtsc_pause.c` (**ESCAPED**: `.byte 0xf3, 0x90; .byte 0x0f, 0x31` -> live `pause` + `rdtsc`)
  - `escapes/r24_hc_unannotated_helper_spinlock.hc` (**ESCAPED** in `.hc`)
- **Root cause:** `src/verify.c:235-239` decodes ONLY the **first** `HCT_STRING` token inside `asm(...)`, ignoring adjacent C string literals (`"" "rd" "tsc"`), and never blocks raw opcode emitter directives (`.byte`, `.2byte`, `.4byte`, `.8byte`, `.short`, `.long`, `.quad`, `.word`, `.inst`, `.hword`, `.ascii`, `.incbin`).
- **v0.1.1 Fix:** Concatenate **all** adjacent `HCT_STRING` tokens inside `asm(...)` (just like `raw_pass:536` already does!), normalize case/whitespace, and **ban all inline `asm` in `--profile=component`** (or strictly allowlist only `hc_rt.h` barriers in `--profile=runtime`) plus reject `.byte`/`.inst`/`.word`/`.long`/`.incbin` unconditionally.

### P0-4. Two Real Memory-Safety Crashes in `hc` Itself (ASan Verified)
- **Crash 1 — Stack Buffer Overflow (`src/verify.c:416`):**
  - Reproducer: `escapes/crash01_deep_callchain_265.c`
  - ASan output: `AddressSanitizer: stack-buffer-overflow ... WRITE of size 8 ... in dfs_cycle src/verify.c:416`
  - Root cause: `check_recursion()` allocates fixed `char *stack[256]`, and `dfs_cycle()` writes `stack[(*sp)++] = f->name` without checking `*sp < 256` (or dynamically sizing to `u->nfuncs`).
- **Crash 2 — Heap Buffer Overflow (`src/lower.c:72`):**
  - Reproducer: `escapes/crash02_unclosed_brace.hc` (37-byte malformed `.hc` with unclosed `{` in `readonly struct Broken { int x;`)
  - ASan output: `AddressSanitizer: heap-buffer-overflow ... READ of size 1 ... in hc_lower_hc src/lower.c:72`
  - Root cause: `copy_block()` (`src/lower.c:24-35`) increments `i++` on the terminating `'\0'` byte before checking `p[i - 1]`, returning `strlen(p) + 1`, so `q += bl` advances one byte past the heap allocation's NUL terminator.

---

## P1 — Substantive Rule-Enforcement Escapes (18 Verified Bypasses)

| ID | Reproducer | Rule | Root Cause (`file:line`) | Concrete v0.1.1 Fix |
|---|---|---|---|---|
| P1-1 | `r07_for_loop_cas_spinlock.c` | E003 | `verify.c:279-288`: `for(...)` only checks empty condition (`;;`), never scans condition/body for CAS/exchange/volatile. | Run CAS/exchange/volatile/atomic-load spin detection across `while`, `do-while`, `for` conditions **and** loop bodies. |
| P1-2 | `r08_atomic_load_busywait_lock.c` | E003/E016 | `verify.c:269-275`: `while(atomic_load_explicit(&g, ...))` is neither `volatile` nor `CAS`. | Ban `atomic_load*`/`__atomic_load*` in any loop condition or unbounded `while` in component profile. |
| P1-3 | `r09_struct_volatile_dekker_wait.c` | E005 | `verify.c:446`: `collect_globals` only records `depth == 0` `volatile` vars; misses `volatile` struct members and `volatile T *` locals. | Track `volatile` struct field names & local `volatile` declarations, or ban `volatile` reads in loop conditions altogether. |
| P1-4 | `r10_unbounded_while2_and_for_noinc.c` | E016 | `verify.c:264,286`: only checks `while(1)`/`while(true)` and `for(;;)`. Misses `while(2)`, `while(!0)`, `for(i=0; i<1; )`. | Require statically bounded `for` with recognizable induction variable + step + constant/param upper bound (or explicit `HS_BOUNDED(N)`), and reject arbitrary `while`/`do-while` in phase kernels. |
| P1-5 | `r12_static_local_mutable_in_pull.c` | E008 | `parser.c:208`: only records `depth == 0` globals; misses mutable `static` locals inside `PULL`/`BARRIER` functions. | Flag any non-`const` `static` declaration inside `strictf(f)` functions as `HC-E008` (keep `static const` allowed — see `ve02`). |
| P1-6 | `r13_global_write_via_ptr_alias.c` | E008 | `verify.c:320-336`: only detects direct `g_var = ...`; misses `uint64_t *p = &g_var; *p = ...`. | Flag taking address (`&g_var`) of any non-`const` file-scope variable inside `strictf(f)` unless assigned to a `const T *`. |
| P1-7 | `r14_prefix_inc_global_in_pull.c` | E008 | `verify.c:330`: `check_strict_mem` omitted prefix `++`/`--` (present in `writes_global:138`, missing in `check_strict_mem:330`). | Add the `pv == "++" || pv == "--"` check to `check_strict_mem()`. |
| P1-8 | `r15_const_cast_via_uintptr_roundtrip.c` | E013/E014 | `verify.c:357`: only checks adjacent `(T *)ro_param`; misses `(uintptr_t)snap` -> `(T *)raw`. | Flag casting any `ro_param` to an integer type (`uintptr_t`/`intptr_t`/`size_t`/`long`) or `void *` in `strictf(f)`. |
| P1-9 | `r16_typedef_anon_union_const_launder.c` | E014 | `verify.c:458,365`: only detects named `union NAME { ... }` and `union NAME` token pairs; misses `typedef union { ... } launder_t;`. | Record typedef aliases of any union containing both `const *` and non-`const *` (or pointer + integer) and flag uses of the typedef name. |
| P1-10 | `r17_cross_shard_write_add_mod.c` | E015 | `verify.c:346-355`: E015 is hardcoded to `^` in array index; misses `(my_shard + 1) & 3` and constant `[0]`. | Require shard-slot writes to use the exact `my_shard` param (or `HS_SHARD_PRIVATE` single-slot pointer) rather than keying on `^` (also fixes false positive `ve04`!). |
| P1-11 | `r18_struct_fnptr_table_to_commit.c` | E007/E011 | `verify.c:401-408`: only checks bare fn refs inside `PULL` body; misses file-scope function-pointer tables containing `COMMIT`/global-writing functions. | Scan file-scope initializers for `COMMIT`/mutator function addresses, or ban indirect calls through function pointers inside `PULL`/`BARRIER` unless target set is verified pure. |
| P1-12 | `r19_indirect_fnptr_recursion.c` | E017 | `parser.c:75-79`: `callees` only captures direct `ident(` calls; misses `self_fp(n-1)`. | Add address-taken function refs inside function bodies to the call graph (and ban indirect calls inside phase kernels for liveness). |
| P1-13 | `r20_blocking_read_and_dev_urandom.c` | E018/E022 | `verify.c:184,198` & `elf_audit.c:42-56`: neither bans `read`, `write`, `recv`, `accept`, `connect`, `fopen`, `open`, `openat`, or `"/dev/urandom"`/`"/dev/random"` in component profile. | Add blocking POSIX I/O (`read`, `pread`, `recv*`, `accept*`, `connect`, `open*`, `fopen*`, `ioctl`) to `HC-E018` + `elf_audit.c` `banned_component`, and flag `"/dev/urandom"`/`"/dev/random"` as `HC-E022`. |
| P1-14 | `r21_ptr_address_order_and_getenv_pid.c` | E023 | `verify.c:177` & `elf_audit.c`: neither bans `getpid`, `getppid`, `getenv`, `secure_getenv`, or relational pointer ordering (`a < b`) in component profile. | Add `getpid`, `getppid`, `getenv`, `secure_getenv` to `HC-E023` + `elf_audit.c` `banned_component`. |
| P1-15 | `r23_runtime_profile_clock_realtime.c` | E020 | `verify.c:190` & `elf_audit.c:48-49`: `--profile=runtime` allows `time()` and `clock_gettime(CLOCK_REALTIME, ...)` unchecked. | Move `time`/`ftime`/`timespec_get` to `banned_always`, and in `--profile=runtime` reject `CLOCK_REALTIME*` / allow only `CLOCK_MONOTONIC*` in `clock_gettime()`. |
| P1-16 | `r27_sysv_semop_and_fcntl_extern.c` | E001/E002 | `verify.c:160-163` & `elf_audit.c:29-31`: `semop`/`semtimedop`/`semget`/`semctl` don't start with `sem_`, and `fcntl` (`F_SETLKW`/`F_SETLK`) isn't in `verify.c` or `elf_audit.c` (`r11` only tripped by coincidence on `struct flock`). | Add `semop`, `semtimedop`, `semget`, `semctl` (`E001`) and `fcntl` (`E002` in component, or flag `F_SETLK`/`F_SETLKW`/`F_OFD_SETLK*` always) to both `verify.c` and `elf_audit.c`. |
| P1-17 | `r28_setjmp_longjmp_escape.c` | E016/E018 | `setjmp`, `_setjmp`, `sigsetjmp`, `longjmp`, `siglongjmp` are unchecked in both `verify.c` and `elf_audit.c`, allowing non-local jumps out of phase boundaries. | Ban `setjmp`/`_setjmp`/`sigsetjmp`/`longjmp`/`siglongjmp` in both `verify.c` and `elf_audit.c` (`banned_component` or `banned_always`). |
| P1-18 | `r24_hc_unannotated_helper_spinlock.hc` | E008/E021 | Combines P0-3 + P1-5 inside `.hc` syntax. | Resolved automatically by P0-3 + P1-5 fixes. |

### Notes on the 3 Caught Probes (Minor Improvements)
- `r11_fcntl_setlkw_and_sysv_sem.c`: Caught **only** because `struct flock` matched `ieq(s, "flock")` (`r27` proves `fcntl` + `semop` escape once `struct flock` isn't named).
- `r22_macro_paste_atomic_spin_and_rdtsc.c`: Caught as `HC-E003` (good!), though `E025` didn't fire because `raw_pass:493` only checks `pthread_*` prefixes for `##` smuggling.
- `r26_cli_no_audit_asm_alias.c`: Caught by `raw_pass` `E024` split-string detector — note that `--no-audit` itself is still a silent footgun CLI flag (`main.c:109`) that should either be removed or require explicit `--unsafe-skip-audit` + stderr warning.

---

## False Positives & `.hc` Lowering Bugs (4 / 6 Valid Edge Controls Failed!)

1. **`ve01_doc_comment_mentions_dlfcn_and_mutex.c` (FALSE POSITIVE — `HC-E002`/`HC-E024`/`HC-E001`)**
   - **Cause:** `raw_pass()` (`src/verify.c:482-485`) runs `strstr(raw, "<threads.h>")` / `"<dlfcn.h>"` / `"<semaphore.h>"` / `"<omp.h>"` over raw unstripped source, firing on ordinary comments!
   - **Fix:** Check `#include` directives after stripping C comments (or scan preprocessed line markers `# 1 "/usr/include/.../dlfcn.h"` from `ppbuf`!).
2. **`ve04_valid_xor_in_bounded_array_index.c` (FALSE POSITIVE — `HC-E015`)**
   - **Cause:** `check_strict_mem()` (`src/verify.c:346-355`) flags **any** array write whose index contains `^`, even to a **local stack array** (`uint64_t local_scratch[8]; local_scratch[i ^ 1u] = ...;`)! That breaks legitimate bit-reversal FFT, checksum, and Morton/Z-order local buffers inside `PULL`.
   - **Fix:** Only apply `HC-E015` to pointer/array parameters (especially `HS_SHARD_PRIVATE` or `struct hc_shard_result *` params), never local stack arrays.
3. **`ve05_hc_comment_with_phase_and_shared_words.hc` & `ve06_hc_string_with_shared_word.hc` (`.hc` LOWERING CORRUPTION + MACRO WARNINGS)**
   - **Cause 1 (`src/lower.c:52-139`):** `hc_lower_hc()` does not skip `//` line comments, `/* ... */` block comments, or `"..."` string literals!
     - In `ve05`, `// Design note: in phase pull workers...` got rewritten to `// Design note: in HS_PHASE_PULL uint64_t workers...` and **ate the real `phase pull pure fn_clean_commented(...) -> uint64_t` signature**, emitting uncompilable C!
     - In `ve06`, `"shared phase pull summary"` was corrupted to `"HS_SHARED  HS_PHASE_PULL void summary"` and emitted a stray `{` at EOF!
   - **Cause 2 (`src/lower.c:50`):** Emitting `#include "hc_rt.h"` BEFORE `#include "hc_contract.h"` causes **10 `-Wmacro-redefined` warnings on every `.hc` file** under `__HC_COMPILER__` (`hc_rt.h:235-245` defines empty `HS_*` macros, then `hc_contract.h:22-32` redefines them).
   - **Fix:** Add comment/string/char-literal pass-through state in `hc_lower_hc()`, and include `"hc_contract.h"` before `"hc_rt.h"` (or guard `hc_rt.h:235` with `#ifndef HS_PHASE_IDLE`).
