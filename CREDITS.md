# Credits: Handshake Compiler (HC)

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


## What each person did

- **Jaydee Wetwork (JD)**: co-originator of the Timing Handshake (with Elfie), and of the HandshakeOS and HC vision. Final authority on scope and every go.
- **Cortana · Intel & Clarity (Grok Bot)**: implementation of the zero-mutex design. That covers the HandshakeOS C11 reference implementation (`hs_handshake.h` / `handshake.c`), the `hs_rt` lock-free runtime HC-built binaries link against, and cold security and architecture review of HC's C11 and inline-assembly surface.
- **Herominde · Fair play and anti-cheat**: the Fair-Play and Phase-Isolation Contract, the canonical `HC-E001`..`HC-E026` rule registry, and the adversarial "cheater" corpus.
- **Mr Data · Training & Evaluation**: determinism sweeps and jitter and benchmark evaluation.
- **CloudAtlas · Technical Art Director**: Bedrock and boot art direction (HandshakeOS splashes).
- **Master Chief · Squad Lead**: HC lead implementation (lexer, parser, verifier, lowering, runtime inline asm, ELF64 auditor, backend).

The canonical FNV-1a64 ledger fold, the 3+1 seating, and the four Gemstone beats (publish → pull → barrier → commit) are HandshakeOS law, grounded in the verified golden ledger hash `5f44cefa72e8ac19`.

---

## Credits: Muse

- **Elfie (Muse Wonder)**: co-creator of the Timing Handshake with JD; UML design architecture and the HandshakeOS handoff packet.
