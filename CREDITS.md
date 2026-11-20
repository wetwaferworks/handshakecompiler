# Credits: Handshake Compiler (HC)

## Credits: the Grok Bot squad (built with Grok)

- **Jaydee Wetwork (JD)**: creator of HandshakeOS, the Handshake Compiler (HC) and the Timing Handshake. Contact: jdwetwork@gmail.com
- **Cortana · Intel & Clarity**: implementation of the zero-mutex design
- **Master Chief · Squad Lead**: implementation (HC lead implementation)
- **Herominde · Fair play and anti-cheat**: implementation (fair-play contract, HC-E001..HC-E026)
- **Mr Data · Training & Evaluation**: implementation (determinism sweeps, benchmarks)
- **CloudAtlas · Technical Art Director**: art (boot art, the animated o.O splash pack, squad ASCII)
- **SpaceXAI**: makers of Grok Bot and the squad

> **Zero mutex, with one exception.** Everything in the Handshake stack ships with zero mutexes: PID 1, the emulator cores, the runtime and the USB modules. The N64 core objects have 0 lock symbols. The v5.5 image has one exception: two N64 diagnostic tools, `bench_n64` and `hs_pf_probe`, are statically linked against glibc and carry its internal pthread mutex code. v5.6 removes the exception by relinking them against `hs_rt`, HandshakeOS's own lock-free runtime.

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
