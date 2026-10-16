# 14. Closed-findings ledger

Return to the [index](README.md). Audit item 6.1: link each finding to its
revision, diagnostics and accepted evidence. This is the running ledger; it is
appended as findings close. Established 2026-10-15. Revisions are git SHAs on
`main`; design docs are in this directory.

## Closed (implemented + evidence)

| Finding | Item | Revision | Resolution + accepted evidence |
|---|---|---|---|
| F-021 DLSS absolute discovery path | 2.6 | `0f2c2de4` | `dlss_upscaler.cpp` `FindNvNgxInDriverStore` no longer hardcodes `C:\Windows\System32\DriverStore`; uses `GetSystemDirectoryW()` at runtime. DLSS is `UZ_DLSS_ENABLED=0` (compiled out), so inspection-verified; the only absolute discovery path is gone. |
| F-019 loose-file VFS locking | 5.2 | `0f2c2de4` | Reviewed and closed with file:line evidence: `vfs.h:27-31` documents the thread-safety contract; `LooseFileVFS::Read` locks `m_mutex`, `PakVFS` locks in-memory/LRU, is lock-free on the immutable `MasterIndex`, and uses per-thread `FILE*` handles for concurrent reads. Locking already narrowed where justified; no speculative change made. |
| F-012 scene parenting permits cycles | 2.4 | `e933550a` | Self-parenting and ancestor cycles are rejected at the shared hierarchy boundary before detachment; 5 tests cover self-parent, direct cycle, deep cycle, valid reparent and detach. |
| F-006 internal tracer cannot prove parity | 3.7 | `e933550a` | PT verification limits published; the path tracer is documented as a diagnostic, not a parity oracle (doc 10; capability matrix). PT limits reconciled with the shipped post chain. |
| F-008 inactive post-process controls exposed | 3.7 | `e933550a` | PT-limits reconciliation + the inert-effects inventory recorded in [post-process/postprocess-chain-status.md](../post-process/postprocess-chain-status.md) and the capability matrix (CAS/FXAA/motion-blur/DOF/lens-flare/LUT/chromatic-aberration inert). |
| F-013 CI clean-checkout contract | 1.1 | `060aaa22` | Reproducible source/build manifest + clean-checkout dependency alignment; native bootstrap evidence in [08-native-bootstrap.md](08-native-bootstrap.md). |
| F-014 tests can pass empty / coverage cannot run | 1.3 | `060aaa22` | Extension lint, required test discovery, fixture-failure semantics and coverage corrected; evidence in [09-test-coverage-and-first-frames.md](09-test-coverage-and-first-frames.md). |
| F-015 Falcor gate approves missing/sub-threshold evidence | 1.4 | `060aaa22` | Required Falcor comparisons now reject missing, mismatched and below-35-dB evidence. |
| F-016 PowerShell gates resolve the wrong root/source scope | 1.2 | `060aaa22` | PowerShell gate roots and source coverage corrected and verified from the user scope. |

## Designed (decision-complete spec, implementation + verification deferred to the engine)

These findings have a turnkey spec (root cause file:line, recommended change,
acceptance criteria, test plan) but are **not implemented**: implementation and
its gtest/GPU-harness verification require building and running the engine.

| Finding | Item | Design doc |
|---|---|---|
| F-009 ECS slot/version bookkeeping | 2.1 | [11-runtime-and-editor-lifecycle-design.md](11-runtime-and-editor-lifecycle-design.md) |
| F-010 owner-thread world mutation | 2.2 | [11](11-runtime-and-editor-lifecycle-design.md) |
| F-011 transactional scene load | 2.3 | [11](11-runtime-and-editor-lifecycle-design.md) |
| F-020 native/IPC queue bounds + H.264 recovery | 2.5 | [11](11-runtime-and-editor-lifecycle-design.md) |
| F-001 temporal matrix/content/state validity | 3.2 | [12-rendering-correctness-design.md](12-rendering-correctness-design.md) |
| F-002 previous-frame velocity inputs | 3.4 | [12](12-rendering-correctness-design.md) |
| F-004 real generated-material context | 3.5 | [12](12-rendering-correctness-design.md) |
| F-007 supported glass geometry/material contract | 3.6 | [12](12-rendering-correctness-design.md) |
| F-005 upscaler validity guard | 3.3 | [13-remaining-stabilization-design.md](13-remaining-stabilization-design.md) |
| F-018 instance-batch preparation + instanced path | 5.1 | [13](13-remaining-stabilization-design.md) |
| F-022 layout contracts / capability matrix / module boundaries | 5.3 | [13](13-remaining-stabilization-design.md); part (b) capability matrix landed 2026-10-15 in [known-issues](../known-issues-and-partial-implementations.md) |

## Open (not yet designed or closed)

| Finding | Item | State |
|---|---|---|
| F-003 graph synchronization / resource declarations | 3.1 | Partly mitigated by the render-graph-state-registry (466df62e); residual UAV-ordering / transient-reuse verification needs the engine (GBV). |
| F-017 language/toolchain contracts disagree | \u2014 | Documented: machine PATH has zig 0.16 but the project pins vendored 0.15.2 (demos break on 0.16; runtime.lib ABI mismatch). Needs a toolchain-standardization decision. |
| Falcor rendering matrix | 4.1 | Needs the engine + GPU + Falcor references. |
| Final tests + run | 6.2, 6.3 | Write + run the per-finding tests; needs build/engine. |

## Diagnostics / scope

Per-finding source locations, impact, confidence and acceptance criteria are in
the thematic files [02](02-rendering-correctness.md)-[05](05-performance-and-maintainability.md).
The initial-audit scope and the checks actually run are in
[01-scope-and-evidence.md](01-scope-and-evidence.md); the stage-0 implementation
evidence is in [07](07-baseline-implementation.md)-[09](09-test-coverage-and-first-frames.md).
