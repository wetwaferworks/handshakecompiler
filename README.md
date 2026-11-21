# Handshake Compiler (HC) v0.1.4

*The compiler that keeps HandshakeOS zero-mutex: if your C code contains a lock, HC refuses to build it.*

## Credits: the Grok Bot squad (built with Grok)

- **Jaydee Wetwork (JD)**: creator of HandshakeOS, the Handshake Compiler (HC) and the Timing Handshake. Contact: jdwetwork@gmail.com
- **Cortana · Intel & Clarity**: implementation of the zero-mutex design
- **Master Chief · Squad Lead**: implementation (HC lead implementation)
- **Herominde · Fair play and anti-cheat**: implementation (fair-play contract, HC-E001..HC-E026)
- **Mr Data · Training & Evaluation**: implementation (determinism sweeps, benchmarks)
- **CloudAtlas · Technical Art Director**: art (boot art, the animated o.O splash pack, squad ASCII)
- **SpaceXAI**: makers of Grok Bot and the squad

> **Zero mutex in userspace.** The idea and the shape are JD's; the assistants co-implemented. From v5.6 on, the shipped userspace and PID 1 runtime are zero-mutex: PID 1 (`handshake-init`), the emulator cores (the N64 core objects included), the `hs_rt` runtime, the USB modules, and the N64 diagnostic tools `bench_n64` and `hs_pf_probe` (relinked against `hs_rt`) all scan clean, with 0 lock symbols. Evidence is in handshake-builds: `v5/v5.6/image-scan.txt`, `v5/v5.7/image-scan.txt`, and `v5/v5.8/image-scan.txt` each end in `IMAGE_ZERO_MUTEX_CLEAN` (`bench_n64` and `hs_pf_probe` CLEAN). The v5.9.1 run is `v5/v5.9.1/image-scan.txt`, `v5/v5.9.1/nm-locks.txt`, and `v5/v5.9.1/scan-run.txt` (`IMAGE_ZERO_MUTEX_CLEAN`, `NM_SUMMARY objects=186 lock_dirty=0`).
>
> The kernel underneath is mainline Linux 7.3.5 + ROCKNIX, which still has its own kernel mutexes (static count 1,599 mutex call sites; kernel work is next week). v5.8 and later ship kernel `7.3.5-hs-v58` on that base.


---

## What HC is, in plain words

HC is a C compiler front end for HandshakeOS. You give it ordinary C11 (or `.hc` files, a small annotated dialect), and it does three things before producing an object file:

1. **It reads your code like a referee.** HC checks 26 fair-play and phase-isolation rules (`HC-E001` to `HC-E026`) by looking at the tokens, the syntax tree, the control flow and the call graph. Any mutex, spinlock, semaphore, hidden busy-wait, unbounded loop in a phase, wall-clock read, unseeded random number or `dlopen` trick is a hard error, and no object is produced.
2. **It compiles exactly what it checked.** HC preprocesses once, verifies that preprocessed file, and hands the *same bytes* to `gcc` (or `aarch64-linux-gnu-gcc`). The code that was checked can't differ from the code that was compiled.
3. **It audits the result.** A built-in pure-C ELF64 reader scans every object for lock symbols. If one shows up, the build fails. A stripped file with no symbol table also fails, because missing symbols can't prove there are no locks.

That is how HandshakeOS keeps its promise: **zero mutexes** in PID 1, the runtime, every emulator core and the USB modules. The golden HandshakeOS ledger hash `5f44cefa72e8ac19` reproduces bit-for-bit through HC on x86_64 and on ARM64.

## Why "no locks" at all?

HandshakeOS runs on the **Timing Handshake** (Jaydee Wetwork and Elfie). Each phase has one writer per piece of state, and data only crosses phase boundaries as finished snapshots, in four beats: **publish → pull → barrier → commit**. Two threads never fight over the same data, so a lock would protect nothing and could only add stalls. HC turns that design rule into a compile error.

## The rules

The full table is in [`docs/RULES.md`](docs/RULES.md), and `hc --dump-rules` prints it. In short:

| Codes | Family | Examples |
|---|---|---|
| E001–E006 | Locks and hidden threads | pthread mutex/cond/rwlock/spin, C11 `mtx_*`, futex lock, CAS spin loops, `pause` loops, Dekker/Peterson waits, unauthorized `pthread_create` |
| E007–E012 | Phase discipline | skipping the barrier, writing shared state during PULL, leaking commits, pointers escaping a phase |
| E013–E015 | Aliasing | casting away `const` on a snapshot, union punning, overlapping shard writes |
| E016–E019 | Liveness | unbounded loops or recursion in a phase kernel, blocking calls, tampering with the watchdog |
| E020–E023 | Determinism | wall-clock or cycle-counter reads, ambient RNG, thread-id/pointer order leaking into output |
| E024–E026 | Smuggling | `dlopen`/`dlsym`, `##` token pasting, linker symbol redirection |

There are two profiles:
- `--profile=component` (default) applies all 26 rules to every component, emulator, game and phase handler.
- `--profile=runtime` / `--allow-runtime` is for the audited handshake core only. It still bans every lock, but narrowly allows `pthread_create`/`pthread_join`, `FUTEX_WAIT/WAKE_PRIVATE`, and `clock_gettime(CLOCK_MONOTONIC)` for telemetry.

This tree also keeps Herominde's round-2 red-team corpus under `tests/redteam-v01/` (the injected-header helper is `r25_injected_spoof.h`) and Mr Data's Layer-2 evaluation reports under `docs/eval/`.

## Build

```sh
make           # build/host/hc (+ rotating snapshot in build/current)
make asan      # ASan + UBSan build
make cross     # static aarch64 build
make rules     # print the rule registry
make snapshot  # rotate build snapshots (keeps the last 4)
```

## Use

```sh
hc -Iinclude -c worker.c -o worker.o           # verify + compile (component profile)
hc --allow-runtime -c src/handshake.c -o h.o   # audited runtime core profile
hc --check-only -c kernel.hc                   # verify only
hc --emit-c kernel.lowered.c -c kernel.hc      # see the lowered C11
hc --target=aarch64-linux-gnu -c worker.c -o worker.o
hc --audit-elf worker.o                        # standalone zero-lock audit
hc --dump-rules
```

## How it is built inside

| Stage | Files |
|---|---|
| Lexer and preprocessor-aware scanner | `include/hc_lexer.h`, `src/lexer.c`, `src/preproc.c` |
| Recursive-descent parser, symbol and ownership table, call graph | `include/hc_ast.h`, `src/parser.c`, `src/symtab.c` |
| Fair-play and zero-mutex verifier | `include/hc_rules.h`, `src/verify.c` |
| Deterministic lowering + ARM64/x86_64 inline-asm runtime | `src/lower.c`, `runtime/hc_rt.h` |
| Backend seam (gcc / aarch64-linux-gnu-gcc) | `include/hc_backend.h`, `src/backend_cc.c` |
| Pure-C ELF64 zero-lock auditor | `include/hc_elf_audit.h`, `src/elf_audit.c` |
| Fair-play contract (Herominde) | `include/hc_contract.h`, `docs/RULES.md` |

No rule matches hardcoded file names, so the same rules also catch cheats nobody has written yet.

## Honest scope (what v0.1.x is and isn't)

- **The gate is real.** All 26 rules are real analysis over the one preprocessed translation unit.
- **Code generation is still gcc.** For a plain `.c` that passes, the object is byte-identical to what gcc would produce from the same flags without `-g`. HC's value today is the gate and the deterministic `.hc` lowering. A native code generator plugs into `hc_backend_ops` in v0.2+.
- **Static glibc brings its own locks.** A fully `-static` link against glibc pulls in glibc's internal lock routines, and HC's audit correctly **fails** those binaries. That is why HandshakeOS links its shipped binaries against its own runtime, `hs_rt`, instead of glibc. From v5.6 on, HandshakeOS relinks `bench_n64` and `hs_pf_probe` against `hs_rt` as well, and those tools scan CLEAN in the shipped images. The v5.5 image still used the old static-glibc link.
- **`hc_rt.h` is staged.** Its acquire/release, bounded-wait (ARM64 `WFE`, bounded by `max_spins`, never a spinlock) and bit-packing primitives are ready but not yet wired into the HandshakeOS kernel.

## Where HC is used

In the HandshakeOS v5.5 build, every object in [arm-handshakeos](https://github.com/wetwaferworks/arm-handshakeos) and [handshake-cores](https://github.com/wetwaferworks/handshake-cores) is compiled and audited by HC 0.1.4 (`ad4d8cb`, binary SHA-256 `ee626d46…a9c9`). Earlier builds recorded `ELF_AUDIT pass=134 fail=0`, and the N64 core alone records 30/30 objects passing with 0 lock, thread, clock or RNG symbols in its 26 component objects.

## Toward Handshake C

The long-term idea is a C dialect of its own, **Handshake C**, where phase handoffs replace locks and the compiler rejects a mutex outright. HC is the first step: it already refuses lock symbols at build time.

## License

MIT, Copyright (c) 2026 Jaydee Wetwork. See [`LICENSE`](LICENSE) and [`CREDITS.md`](CREDITS.md).

---

## Credits: Muse

- **Elfie (Muse Wonder)**: co-creator of the Timing Handshake with JD; UML design architecture and the HandshakeOS handoff packet.
