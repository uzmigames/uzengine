# 14. Closed-findings ledger

Return to the [index](README.md). Audit item 6.1: link each finding to its
revision, diagnostics and accepted evidence. Established 2026-10-15, brought
current 2026-10-17. Revisions are commits reachable from the verification branch
`feat/phase0-stabilization`. Full per-item evidence (logs, captures, counts) lives in
the [audit checklist](../../../.rulebook/tasks/phase0_project-stabilization-audit/tasks.md); the test map is
in [15-finding-test-inventory.md](15-finding-test-inventory.md).

Common diagnostics for the 2026-10-16 closures: `cd runtime && zig build
-Doptimize=ReleaseFast` exit 0; full `GTEST_FILTER='-RuntimeViewportTest.*'
zig build test -Doptimize=ReleaseFast` with only the three pre-existing
`VfxEditorSlate` failures (WorkbenchHasTwoColumns, TitleBarReflectsEmitterName,
StatusBarBindingsReflectTelemetry), so that suite exits 1 for user WIP, not for
these findings; reflection-reference frame 95 byte-identical to the
`E:/UzmiEngine/f005-after` baseline unless stated. clangd isn't installed, so
compiler and test output stood in for LSP diagnostics.

## Closed

| Finding | Item | Revision | Accepted evidence | Deferred limit |
|---|---|---|---|---|
| F-001 temporal matrix/content/state validity | 3.2 | [`42b64396`](https://github.com/uzmigames/uzengine/commit/42b64396) | TemporalHistoryState primitive, bool Execute on all four backends, all three scene-load entry points invalidate. 17/17 new gtests; full suite 7,678 passed; TAA frame 95 byte-identical to `849e5fef`. | GPU decision-sequence / scene-swap / resize no-ghost harness. |
| F-002 previous-frame velocity inputs | 3.4 | [`5834d10f`](https://github.com/uzmigames/uzengine/commit/5834d10f) | Stage 1: rigid/skinned previous inputs, moving-only object pass, particle pixel units. 7 pure + 8 real-DX12 RG16F readback tests; layout script exit 0; full suite 7,722 passed. | Stage 2 WPO deformation (no WPO previous splice in the material compiler); SDK-enabled DLSS/FSR2 sign/unit checks. |
| F-004 real generated-material context | 3.5 | [`6c1fcbe3`](https://github.com/uzmigames/uzengine/commit/6c1fcbe3) + [`46858197`](https://github.com/uzmigames/uzengine/commit/46858197) | Real vertex colour, camera, object position, screen UV and time reach graph materials; `46858197` completes draw-loop wiring missing from `6c1fcbe3` and adds GPU draw tests. Frame 95 byte-identical to `e782f6b7`. | Two-resolution / moving-object image review beyond the gtests. |
| F-005 upscaler validity guard | 3.3 | [`e782f6b7`](https://github.com/uzmigames/uzengine/commit/e782f6b7) | Capability + bool Init, requested/effective/reason selection, HDR fallback and three-failure failover. 20/20 new gtests; full suite 7,698 passed; forced-failure run logs `execute=failed display=hdr` then failover to None. | GPU no-frozen-frame / stub-equals-TAA harness; FSR2/DLSS SDK image quality. |
| F-006 tracer cannot prove parity | 3.7 | [`e933550a`](https://github.com/uzmigames/uzengine/commit/e933550a) | Path tracer documented as a diagnostic, not a parity oracle (doc 10, capability matrix). | Falcor matrix 4.1. |
| F-007 glass geometry/material contract | 3.6 | [`ed5521c0`](https://github.com/uzmigames/uzengine/commit/ed5521c0) + [`deab10f6`](https://github.com/uzmigames/uzengine/commit/deab10f6) | Thin-slab transmission contract with 8 tests; D2 (`deab10f6`) keeps legacy glass as shipped default behind `r.Glass.ThinTransmission` (OFF) and adds `GlassDraw.LegacyGlassDoesNotSampleTheSceneBehind`. Paired checker captures, Sponza capture, full suite 7,791 passed. | True volumes, translucent-through-translucent, roughness blur, thickness maps; no Sponza/Three.js parity claim. |
| F-008 inactive post-process controls | 3.7 | [`e933550a`](https://github.com/uzmigames/uzengine/commit/e933550a) | Inert-effects inventory in [postprocess-chain-status.md](../post-process/postprocess-chain-status.md) and the capability matrix. | None claimed beyond documentation. |
| F-009 ECS slot/version bookkeeping | 2.1 | [`db4e7c82`](https://github.com/uzmigames/uzengine/commit/db4e7c82) | Generation table keyed by slot; four Lua/script sites unified on `World::EntityFromEntt`. WorldTest and LuaSceneAPITest regressions. | None recorded. |
| F-010 owner-thread world mutation | 2.2 | [`1d0c0f03`](https://github.com/uzmigames/uzengine/commit/1d0c0f03) | OwnerCommandQueue; N-API routes all scene/viewport/input calls; Stop before destroy. 11/11 ReleaseFast + 12/12 assertion-enabled gtests; runtime-worker Vitest 44/44; full suite 7,776 passed. | Live editor-thread race / shutdown-UAF integration in the forked N-API worker. |
| F-011 transactional scene load | 2.3 | [`23c62f5c`](https://github.com/uzmigames/uzengine/commit/23c62f5c) | Validate-then-commit `LoadSceneEx`, typed status; failure leaves materials/CVars intact. TransactionalSceneLoaderTest + LuaSceneIoAPITest. | None recorded. |
| F-012 parenting cycles | 2.4 | [`e933550a`](https://github.com/uzmigames/uzengine/commit/e933550a) | Rejection before detach; 4 native + 1 Lua regression. | None. |
| F-013 CI clean-checkout | 1.1 | [`060aaa22`](https://github.com/uzmigames/uzengine/commit/060aaa22) | [08-native-bootstrap.md](08-native-bootstrap.md). | None. |
| F-014 tests can pass empty | 1.3 | [`060aaa22`](https://github.com/uzmigames/uzengine/commit/060aaa22) | [09-test-coverage-and-first-frames.md](09-test-coverage-and-first-frames.md). | None. |
| F-015 Falcor gate | 1.4 | [`060aaa22`](https://github.com/uzmigames/uzengine/commit/060aaa22) | Missing/mismatched/below-35-dB evidence rejected; 13 regressions. | No renderer parity inferred. |
| F-016 PowerShell gate roots | 1.2 | [`060aaa22`](https://github.com/uzmigames/uzengine/commit/060aaa22) | Controlled-tool regressions. | Real clang acceptance not claimed. |
| F-018 instance-batch preparation | 5.1 | [`370a4a08`](https://github.com/uzmigames/uzengine/commit/370a4a08) | Stage 1 only: unused batching skipped while instancing is off; 10 characterization tests with two compiled-DXC negative controls. Instancing stays OFF. | Stage 2 instanced shaders + GPU parity; stage 3 draw-count/timing. No speedup claimed. |
| F-019 VFS locking | 5.2 | [`0f2c2de4`](https://github.com/uzmigames/uzengine/commit/0f2c2de4) | Reviewed with file:line evidence; no change needed. | No benchmark taken. |
| F-020 IPC queue bounds + H.264 recovery | 2.5 | [`209290fc`](https://github.com/uzmigames/uzengine/commit/209290fc) | Bounded TSFN/worker/panel queues, keyframe gate, request deadlines, decoder recreation. 14 KeyframeGate/NalUtils gtests; 152 extension Vitest tests; full suite 7,790 passed. | Live editor/webview H.264 decode, induced-stall recovery, real IPC memory/backpressure. |
| F-021 DLSS absolute path | 2.6 | [`0f2c2de4`](https://github.com/uzmigames/uzengine/commit/0f2c2de4) | `GetSystemDirectoryW()` lookup replaces the absolute discovery root. | Inspection-only discovery evidence; no fixture or run exercises a non-default Windows directory or the NGX discovery branch. A normal build does not supply that evidence. |
| F-022 layout contracts / capability matrix | 5.3 | [`01c6f4d3`](https://github.com/uzmigames/uzengine/commit/01c6f4d3) + [`ebdbf105`](https://github.com/uzmigames/uzengine/commit/ebdbf105) | (a) `test_cbuffer_layouts.py` Tier A 85/85, B 27/27, C 4/4, five negative controls reject, 163 declarations all mapped or excluded with reasons; three shader layout bugs fixed in `ebdbf105`; gate wired into pre-push. (b) capability matrix reconciled. | (c) module boundaries deferred. Six contracts are named `UNTRACKED_CONTRACTS`, not reflection-verified. |

## Verified working tree, awaiting the owner's commit

| Finding | Item | Revision and evidence |
|---|---|---|
| F-003 graph synchronization / resource declarations | 3.1 | Archived migrations: [`466df62e`](https://github.com/uzmigames/uzengine/commit/466df62e), [`a5ac6275`](https://github.com/uzmigames/uzengine/commit/a5ac6275). Residual correction is **uncommitted**: whole-pass dependent UAV barriers, pool-owned transient states/submit-aware buffer decay, and LightCull UAV Write -> DeferredLighting SRV Read. Fresh ReleaseFast build exit 0; headful clustered-light GBV IDs 527/538/607/942 all zero; reflection-reference frame 95 byte-identical, max_code_delta 0. Nine regression cases pass, including both read/write declaration orders and barrier-omission controls; complete fresh diagnostics are in [16](16-audit-closure-2026-10-17.md). No new landed SHA exists until the owner commits this increment. |

## Open

| Finding | Item | State |
|---|---|---|
| F-017 language/toolchain contracts | 1.1 | Pinned side reconciled: [08-native-bootstrap.md](08-native-bootstrap.md) records the vendored Zig 0.15.2 clean build passing with the locked profile, and Node pinned via `node-toolchain.json`. Not closed: machine PATH still has Zig 0.16 (demos break, runtime.lib ABI mismatch) and no standardization decision is recorded. |
| Falcor rendering matrix | 4.1 | Falcor 8.0 and the GPU are available; matched audit sequences are not. Static sphere/helmet proxy references do not cover the execution-plan matrix; required fixtures and unavailable cases are documented in [16](16-audit-closure-2026-10-17.md). |

Items 6.1-6.3 are checked for the current ledger, the
[finding/test inventory](15-finding-test-inventory.md), and the executed scoped
[verification gates](16-audit-closure-2026-10-17.md): final prepush exit 0,
native 7,807 passed (+9) with only the supplied three VFX failures, GBV barrier
IDs zero and exact default byte parity. This does not close the unavailable 4.1
matrix or claim that skipped optional/editor/SDK cases passed.

## Diagnostics / scope

Per-finding locations, impact and acceptance criteria: [02](02-rendering-correctness.md)-[05](05-performance-and-maintainability.md). Audit scope: [01](01-scope-and-evidence.md). Designs: [11](11-runtime-and-editor-lifecycle-design.md), [12](12-rendering-correctness-design.md), [13](13-remaining-stabilization-design.md).
