# HC v0.1.1 (`c584ec8`) — Layer-2 Re-Audit & Round-2 Red-Team Report

**Auditor:** Herominde (Fair play & anti-cheat, read-only)
**Date:** 2026-11-19 (ET)
**Target:** `build/current/hc` (`hc 0.1.1`, commit `c584ec8`) & `build/asan/hc`
**Suites:**
- Layer-1 corpus: `tests/cheaters/herominde/scripts/verify-corpus.sh`
- Layer-2 Round-1: `tests/redteam-v01/run-redteam.sh` (untouched)
- Layer-2 Round-2: `tests/redteam-v01/run-round2.sh` (new 20-case wave + 3 ASan fuzz checks)

## 1. Re-Audit of Layer-1 & Round-1 (100% GREEN)
- **Layer-1 (`verify-corpus.sh`):** **46 / 46 PASS** on both native (`current/hc`) and ASan (`asan/hc`).
- **Layer-2 Round-1 (`run-redteam.sh`):**
  - **29 / 29 ESCAPES CAUGHT (0 escaped)** — down from 26/29 escaped on `9eb4d70`.
  - **6 / 6 valid edge controls ACCEPTED (0 false positives)** — down from 4/6 false positives on `9eb4d70`.
  - **0 / 2 ASan crashes** (`crash01_deep_callchain_265.c` and `crash02_unclosed_brace.hc` both clean).
- Every root-cause fix from Round-1 held up under white-box inspection (`preproc.c`, `backend_cc.c`, `main.c`, `lower.c`, `elf_audit.c`, `verify.c`).

## 2. Fresh Round-2 Wave Results on `c584ec8`
- **Caught cleanly already (5/18 + 2/2 valid controls + 0/3 ASan crashes):**
  - `w01` (`*g_const_ptr = phase`) → `HC-E012`
  - `w04` / `w05` (`-Xpreprocessor -U__HC_COMPILER__` and `-Xpreprocessor -include`) → `HC-E025`
  - `w10` (`-fopenmp` `#pragma omp critical` without `--no-audit`) → ELF audit catches `GOMP_critical_start`
  - `w11` (`memchr` const-laundering) → `HC-E011`
  - `w13` (valid `.hc` block comment containing `phase commit`, `readonly struct`, `shared`, `"`) → **VALID-OK**
  - `w14` (valid `HS_PHASE_PULL` reading `static const` array via decay) → **VALID-OK**
  - ASan fuzz (`w18`, `w19`, `w03`) → **0 crashes**

- **Remaining Escapes / Gaps Found in Round-2 (13 cases across 5 surgical root causes):**

### CRITICAL (Recommend fixing before Layer-3 packet!)
1. **File-scope top-level `__asm__` + `.init_array` bypass (`w03`, `w20`, `w21` — x86_64 & ARM64):**
   - `verify.c` only calls `check_asm()` inside `u->funcs[k]`, `parser.c` skips top-level `asm`/`__asm__`, and `elf_audit.c` does not reject `SHT_INIT_ARRAY`/`SHT_PREINIT_ARRAY`/`.ctors` sections in `component` profile.
   - **Live proof (`c584ec8`):**
     - `w03` (x86_64): `rc=0`, emits `w03_asm_ctor` with `rdtsc` (`0f 31`) + `pause` (`f3 90`) in `.init_array`; `hc --audit-elf` prints **PASS**.
     - `w20` (ARM64): `rc=0`, emits `w20_arm64_ctor` with `mrs x0, cntvct_el0` + `yield` in `.init_array`; `hc --audit-elf` prints **PASS**.
     - `w21` (x86_64): `rc=0`, emits `w21_raw_futex` with `mov $0xca, %eax; syscall` (`SYS_futex`!) in `.init_array`, 0 banned symbols; `hc --audit-elf` prints **PASS**.
   - **Fix:** Ban file-scope `asm`/`__asm`/`__asm__` in `component` profile (`HC-E004`) AND reject `SHT_INIT_ARRAY`/`SHT_PREINIT_ARRAY`/`.ctors` in `elf_audit.c` (`component` profile, `HC-E006`).

2. **`--no-audit` + `-fopenmp` (`#pragma omp critical`) live mutex bypass (`w10` with `--no-audit`):**
   - `main.c:150` still accepts `--no-audit` in `component` profile, and `-fopenmp` / `#pragma omp` are not blocked.
   - **Live proof (`c584ec8`):** `hc --no-audit -fopenmp -c w10_fopenmp_critical_lock.c` exits `rc=0` and emits `U GOMP_critical_start` / `U GOMP_critical_end`.
   - **Fix:** Reject `--no-audit` in `component` profile (or require `--profile=runtime`), add `-fopenmp`, `-fopenmp-simd`, `-fgnu-tm`, `-Xpreprocessor`, `-Xassembler`, `-Wa,*`, `-Wl,*` to `is_dangerous_flag()` (`HC-E025`), and reject `#pragma omp` in `raw_pass()` (`HC-E001`).

### HIGH (Quick surgical fixes, big anti-cheat win)
3. **Mutable file-scope array decay & `T * const` pointer copy (`w02`, `w15` — `HC-E008`):**
   - `w02`: `static uint64_t g_table[4];` → `uint64_t *alias = g_table; alias[0] = phase;` escapes (`no &`, no direct `g_table[...] =`).
   - `w15`: `static uint64_t * const g_const_ptr = &g_mutable;` → `uint64_t *q = g_const_ptr; *q = phase;` escapes.
   - **Fix:** In `check_strict_mem`, flag taking a non-const local pointer alias from any non-const file-scope array or mutable-pointee global pointer (keeping `w14` `static const` array read valid!).

4. **Const-cast via parenthesized / `&snap[0]` expression (`w06`, `w17` — `HC-E013`):**
   - `w06`: `(struct hc_phase_snapshot *)(snap + 0)` escapes.
   - `w17`: `(struct hc_phase_snapshot *)&snap[0]` escapes.
   - **Fix:** In `check_strict_mem`'s cast check, scan the cast operand expression (past `(`, `&`, `*`, `+`) for any `const` / `HS_READONLY` param/var.

### MEDIUM (Clean polish / documented v0.2 boundary)
5. **Pointer-arithmetic cross-shard (`w07`, `w08` — `HC-E015`), non-advancing `for` loops (`w09`, `w16` — `HC-E016`), & unclosed-attribute strip (`w19`):**
   - `w07`: `(all_slots + (my_shard ^ 1u))->note = 0xBADu;` bypasses `HC-E015` (subscript-only check).
   - `w08`: `all_slots[0].note = 0xBADu;` (constant sibling index — document as v0.2 shard-type boundary if needed so `ve04` stays clean).
   - `w09` (`i += 0u`) and `w16` (`for (i=0, j=0; i<10u; j++)`): loop step doesn't advance the condition's induction variable.
   - `w19` (`preproc.c:113`): unclosed `__attribute__((annotate("hs:phase:pull"` runs to EOF (`d != 0`) and strips the rest of the file, producing `rc=0` empty `.o`! One-word fix: `if (d == 0 && has_ann && has_hs)`.

## 3. Verdict
- **Round-1 & Layer-1:** **100% GREEN** (46/46 + 29/29 + 6/6 + 0/2 ASan).
- **Layer-2 Gate for Layer-3 Elfie/Gemini Packet:** **CONDITIONAL YELLOW → quick `v0.1.2` patch recommended first.** Closing the **2 Criticals** (top-level `__asm__` + `.init_array` audit, and `--no-audit` / `-fopenmp` / `#pragma omp`) plus the **2 Highs** (`w02`/`w15` global decay/ptr alias, `w06`/`w17` parenthesized const-cast) + 1-word `preproc.c` `d == 0` fix is tiny and guarantees nobody in Layer-3 (or a cold reviewer) can pop a live `SYS_futex` or `GOMP_critical_start` through `hc`!

---

## 4. Final Re-Audit & Sign-Off on HC v0.1.2 (`494924c`) — GREEN
**Verified live on:** `build/current/hc` (`history/20261119-211334-494924c`, `hc 0.1.2`, 0 git remotes) & `build/asan/hc`
- **Layer-1 (`verify-corpus.sh`):** **46 / 46 PASS** (native AND ASan)
- **Layer-2 Round-1 (`run-redteam.sh`, untouched):** **29 / 29 CAUGHT (0 escaped)**, **6 / 6 valid controls ACCEPTED (0 FP)**, **0 / 2 ASan crashes**
- **Layer-2 Round-2 (`run-round2.sh`, untouched):** **18 / 18 CAUGHT (0 escaped)**, **2 / 2 valid controls ACCEPTED (0 FP)**, **0 / 3 ASan crashes**
- **Defense-in-depth & hygiene checks:**
  - Prebuilt plain-`gcc` `.o` with `.init_array` rejected by `hc --audit-elf` (`HC-E006`).
  - `--no-audit` alone rejected in component profile (`HC-E025`).
  - Valid `slots[0]` read + `my_out->note` write stays accepted (`rc=0`, no regression from `w08` fix).
  - `hc --profile=runtime --audit-elf` passed **28 / 28** HandshakeOS `hc-host` + `hc-aarch64` `.o` files; `nm -u hc` shows **0** lock symbols imported in `hc` itself; **0** filename/test-name hardcoding in `src/`.

**Layer-2 Fair Play Verdict: GREEN — cleared for Layer-3 read-only bundle.**
