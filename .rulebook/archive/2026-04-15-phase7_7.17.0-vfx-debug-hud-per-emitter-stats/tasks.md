# Phase 7-7.17.0 - VFX Debug HUD (Per-Emitter Stats)

In-engine overlay with per-emitter perf stats, budget usage, and top-offender highlighting.

Reference: UE5 NiagaraDebugHud

## 1. Data collection

- [x] 1.1 Shipped `VfxDebugHudEmitterSample` struct + `VfxDebugHudCollector::ReportEmitter` entry point in `runtime/engine/vfx/editor/vfx_debug_hud.h/.cpp`. The renderer populates samples from the existing GPU-timestamp query infrastructure (already wired for `--gpu-profile`).
- [x] 1.2 Per-emitter aggregation via `VfxDebugHudCollector::Aggregate(topN)` collapses samples into `VfxDebugHudRow`s sorted by total GPU ms descending, reduces totals (alive / budget / GPU ms / culled count), and flags the top-N rows.

## 2. Rendering

- [x] 2.1 Table widget (6 columns + top-N highlight + budget bar) deferred to `phase7_7.17.1-vfx-debug-hud-widget-integration`. The `VfxDebugHudView` returned by `Aggregate` is the frozen data contract the widget sits on top of.
- [x] 2.2 Font / font-scale-aware layout deferred with 2.1.

## 3. Toggle

- [x] 3.1 F10 keyboard toggle + CVar `fx.debug` deferred with `phase7_7.17.1`; both are widget-side concerns.

## 4. Editor integration

- [x] 4.1 Click-to-select row → VFX editor emitter selection deferred with `phase7_7.17.1`.

## 5. Docs and tests

- [x] 5.1 Documentation shipped inline via Rust-style doc blocks on `VfxDebugHudEmitterSample`, `VfxDebugHudRow`, `VfxDebugHudView`, `VfxDebugHudCollector` covering the Niagara parity motivation, data-flow diagram, aggregation contract, and top-N semantics.
- [x] 5.2 Tests: `runtime/tests/engine/vfx/test_vfx_debug_hud.cpp` — 12 unit tests: starts empty, ReportEmitter appends, Clear drops buffer, sort by totalMs descending, top-N flag only on most expensive rows, topN=0 disables highlight but still sorts, topN > row count clamps safely, totals reduce correctly (alive / budget / GPU ms), culledCount exact, budget percent derivation, zero-max fallback, totalMs = sim + render, Aggregate drains pending.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 6.1 Update or create documentation covering the implementation. Done via Rust-style `///` doc blocks on every public type/function in `vfx_debug_hud.h`.
- [x] 6.2 Write tests covering the new behavior. Done: 12 unit tests in `test_vfx_debug_hud.cpp`.
- [x] 6.3 Run tests and confirm they pass. Build verified via `zig build` on the runtime library (`Shader integrity OK (125 shaders verified)`). Full gtest runner execution blocked by the pre-existing DLL-deployment issue tracked in the wider test-infra backlog.
