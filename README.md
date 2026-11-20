# Handshake Compiler (`HC`) v0.1

A phase-boundary-aware, **pure C11** compiler frontend for HandshakeOS, with
targeted ARM64 (Allwinner H700 / Cortex-A53) and x86_64 inline-assembly hot
paths. HC enforces Herominde's Fair-Play & Phase-Isolation Contract
(`HC-E001`..`HC-E026`) at compile time, lowers annotated `.c` and dedicated
`.hc` sources to deterministic C11, drives `gcc` / `aarch64-linux-gnu-gcc` for
codegen, and runs a built-in pure-C ELF64 zero-lock symbol audit on every
object it emits.

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

See `CREDITS.md` for the squad. Licensed MIT (`LICENSE`).
