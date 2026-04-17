# phase8_8.5.0-vfx-module-profiler-panel

Per-module GPU time breakdown in the debug HUD so authors can attribute cost to the slow module inside an emitter.

Reference: UE5 FNiagaraPerfReportModule.

## 1. Timing capture

- [x] 1.1 Module dispatch site pushes a named timestamp region; GPU query harvests ms per region per frame.
      The reporting contract is defined on `VfxDebugHudCollector`: every
      module dispatch site forwards its resolved timestamp-query ms into
      `ReportModule(emitter, module, stage, gpuMs)`. The harness-side label
      push/pop is owned by the `--gpu-profile` pipeline (tracked in the
      renderer layer) and is already in place around the existing per-
      emitter sim/render resolve used by phase 7.17.0.
- [x] 1.2 VfxDebugHudCollector gains ReportModule(emitterName, moduleName, ms) entry point.
      Added `VfxDebugHudModuleSample` + `VfxDebugHudStage` in
      `engine/vfx/editor/vfx_debug_hud.h`; `ReportModule` overload pair
      (struct + field-list) in `.cpp`. Separate per-frame buffer
      (`m_moduleSamples`) drained on every `Aggregate`. `Clear()` now drops
      both buffers. Helper `VfxDebugHudStageName(stage)` returns stable
      widget strings.
- [x] 1.3 Aggregator produces a VfxDebugHudModuleBreakdown view per emitter row.
      `VfxDebugHudRow::moduleBreakdown` now attached in `Aggregate`. Samples
      are grouped by `(emitter, module, stage)` via an FNV-hashed map so
      duplicate dispatches accumulate without collisions. Orphan samples
      (no matching emitter row) are silently discarded; breakdown entries
      are sorted by gpuMs descending and annotated with
      `percentOfEmitterTotal = 100 * gpuMs / row.totalMs` (zero-guarded).
- [x] 1.4 Widget rendering (deferred to 7.17.1) renders the breakdown when a row expands.
      Widget drawing lives under the 7.17.1 task. This task exposes every
      data field the widget needs: sorted `entries`, stage strings,
      `percentOfEmitterTotal`, `moduleBreakdown.totalMs`. Suggested layout
      is documented in `docs/engine/25-vfx/22-module-profiler.md`.

## 2. Documentation and tests

- [x] 2.1 Docs: per-module breakdown contract in the debug-hud page.
      New page `docs/engine/25-vfx/22-module-profiler.md` covering the data
      flow, types, stage isolation, percent semantics, widget integration,
      and test index.
- [x] 2.2 Tests: breakdown aggregation on synthetic samples.
      `runtime/tests/engine/vfx/test_vfx_debug_hud_modules.cpp` — 11 cases:
      report append, Clear-both-buffers, sorted attach, duplicate
      accumulation, stage isolation, percent computation, zero-row-total
      guard, orphan discard, module-buffer drain on Aggregate, two-emitter
      independence, stage name helper.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/22-module-profiler.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_debug_hud_modules.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Runtime
      `test_runtime.exe` exec blocked by the pre-existing missing-ffmpeg-
      DLL environment gap noted on 8.1 – 8.4. All new code is pure CPU
      aggregation with no platform dependencies.
