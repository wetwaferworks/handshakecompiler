# Handshake Compiler (`HC`) v0.1.1

A phase-boundary-aware, **pure C11** compiler frontend for HandshakeOS, with
targeted ARM64 (Allwinner H700 / Cortex-A53) and x86_64 inline-assembly hot
paths. HC enforces Herominde's Fair-Play & Phase-Isolation Contract
(`HC-E001`..`HC-E026`) at compile time, lowers annotated `.c` and dedicated
`.hc` sources to deterministic C11, drives `gcc` / `aarch64-linux-gnu-gcc` for
codegen, and runs a built-in pure-C ELF64 zero-lock symbol audit on every
object it emits. The backend compiles the EXACT verified preprocessed
translation unit (`-x cpp-output` on the annotation-stripped `.i`), so the
bytes HC analyzes and the bytes gcc codegens are provably the same — there is
no preprocessor/backend split-brain.

Zero mutexes. Zero locks. Zero ambient nondeterminism. The golden HandshakeOS
ledger hash `5f44cefa72e8ac19` reproduces bit-for-bit through HC on x86_64 and
ARM64.

## Build

```sh
make           # build build/host/hc (+ rotating build snapshot, build/current)
make asan      # ASan+UBSan build (build/asan/hc)
make cross     # static aarch64 build (build/aarch64/hc)
make rules     # print the HC-E001..HC-E026 rule registry
make snapshot  # rotate build snapshots (keep last 4), update build/current
```

## Use

```sh
hc -Iinclude -c worker.c -o worker.o          # verify + compile (component profile)
hc --allow-runtime -c src/handshake.c -o h.o  # audited runtime core profile
hc --check-only -c kernel.hc                  # verify only, no codegen
hc --emit-c kernel.lowered.c -c kernel.hc     # inspect lowered C11
hc --target=aarch64-linux-gnu -c worker.c -o worker.o
hc --audit-elf worker.o                        # standalone ELF64 zero-lock audit
hc --dump-rules                                # the declarative rule registry
```

Two profiles: `--profile=component` (default, strict `HC-E001`..`HC-E026`) and
`--profile=runtime` / `--allow-runtime` (audited timing-handshake core: still
bans every lock/spinlock/RNG/wall-clock/dlopen/smuggle, narrowly permits
`pthread_create`/`pthread_join`, `FUTEX_WAIT/WAKE_PRIVATE`, and
`clock_gettime(CLOCK_MONOTONIC)` in `hs_now_ns` telemetry).

## Architecture

| Stage | Files |
|---|---|
| Dual-pass lexer / preprocessor-aware scanner | `include/hc_lexer.h`, `src/lexer.c`, `src/preproc.c` |
| Recursive-descent parser, symbol/ownership table, call graph | `include/hc_ast.h`, `src/parser.c`, `src/symtab.c` |
| Declarative fair-play & zero-mutex verifier | `include/hc_rules.h`, `src/verify.c` |
| Deterministic lowering + ARM64/x86_64 inline-asm runtime | `src/lower.c`, `runtime/hc_rt.h` |
| Backend seam (gcc / aarch64-linux-gnu-gcc) | `include/hc_backend.h`, `src/backend_cc.c` |
| Pure-C ELF64 zero-lock symbol auditor | `include/hc_elf_audit.h`, `src/elf_audit.c` |
| Fair-play contract (Herominde) | `include/hc_contract.h`, `docs/RULES.md` |

Every rule is genuine lexical / AST / CFG / call-graph analysis — no hardcoded
filenames — so unseen red-team cheaters are caught by the same rules.


## What v0.1.x actually is (honest scope)

HC v0.1.x is a **compile-time verification + `.hc` lowering gate layered over
`gcc`**, not yet a standalone native code generator (the `hc_backend_ops` seam
in `include/hc_backend.h` is where a native emitter lands in v0.2+). Concretely:

- **Fair-play / phase-isolation enforcement is real and complete.** Every
  `HC-E001`..`HC-E026` rule is genuine lexical / AST / CFG / call-graph analysis
  over the one authoritative preprocessed TU (`gcc -E -D__HC_COMPILER__=1` with
  the user's exact macro/opt/std flags mirrored in). There are no hardcoded
  filenames, so unseen cheaters are caught by the same rules.
- **Codegen is gcc.** For a plain annotated `.c` that passes verification, the
  emitted object is **byte-identical to what `gcc` would produce** from the same
  flags — the HS_* annotations strip to nothing (or `restrict`) and HC does not
  (yet) rewrite instructions. The value HC adds at this stage is the *gate* and
  the deterministic `.hc` lowering, not a different instruction stream.
- **"Zero mutexes" is a property of the compiled translation units** HC emits
  and audits (0 lock/spin/sem/cond symbols across all HandshakeOS objects).
  Note that a fully static ARM64 link (`-static`) pulls in glibc's `libc.a`,
  which itself contains lock symbols; that is a property of the C library, not
  of HC-compiled code. HC audits the objects it compiles, and fails closed on a
  stripped/static binary it cannot prove (no symbol table).

## `hc_rt.h` runtime note (EVTSTREAM dependency)

`hc_bounded_wait_u32()` parks a core with ARM64 `WFE` against the exclusive
monitor (armed by a read-only `LDAXR`, never `STXR`, so no lock is built). A
wakeup comes from an explicit `hc_event_wake()` (`SEV`) by the writer **or** from
the CPU **event stream** (`CNTKCTL_EL1.EVNTEN`), which periodically sets the
event so a core can never stall past a store that missed its `SEV`. On parts
with the event stream disabled, writers must pair every published update with
`hc_event_wake()`. Either way the wait is bounded by `max_spins`, so it can
never degrade into an unbounded busy-wait / spinlock. See the header's top
comment for the full contract.

See `CREDITS.md` for the squad. Licensed MIT (`LICENSE`).
