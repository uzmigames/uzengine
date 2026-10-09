# VFX module profiler panel

Per-module GPU-time breakdown in the debug HUD so authors can attribute cost
to the slow module inside an emitter. Extends the per-emitter stats (phase
7.17.0) with a drill-down row the widget (phase 7.17.1) reveals on expand.

Reference: UE5 `FNiagaraPerfReportModule` + Stats2 per-module breakdown.

## Data flow

1. GPU timestamp-query harness wraps each module dispatch (spawn CS, update
   CS, render draw) with a labelled region.
2. On resolve the renderer calls
   `VfxDebugHudCollector::ReportModule(emitterName, moduleName, stage, gpuMs)`
   once per resolved region — multiple dispatches of the same module in one
   frame accumulate during `Aggregate`.
3. `Aggregate` groups samples by `(emitter, module, stage)`, attaches the
   sorted breakdown to the matching emitter row, computes per-entry percent
   against the row's `totalMs`, and drains both per-frame buffers.
4. Orphan samples whose emitter did not call `ReportEmitter` (e.g. culled
   emitter that still pushed module labels) are silently discarded.

## Types

```cpp
enum class VfxDebugHudStage : uint8_t { Spawn, Update, Render };

struct VfxDebugHudModuleSample {
    std::string       emitterName;
    std::string       moduleName;
    VfxDebugHudStage  stage;
    float             gpuMs;
};

struct VfxDebugHudModuleEntry {
    std::string       moduleName;
    VfxDebugHudStage  stage;
    float             gpuMs;
    float             percentOfEmitterTotal;
};

struct VfxDebugHudModuleBreakdown {
    std::vector<VfxDebugHudModuleEntry> entries; // sorted desc by gpuMs
    float totalMs;
};
```

`VfxDebugHudRow::moduleBreakdown` holds the attached breakdown; empty when
no module samples landed on the row.

## Stage isolation

Modules may share a name across stages (e.g. `SetAttribute` in Spawn vs
Update). Aggregation keys on the full triple `(emitter, module, stage)` so
cross-stage counts do not collide. `VfxDebugHudStageName(stage)` returns a
stable string for widget display.

## Percent calculation

`percentOfEmitterTotal = 100 * gpuMs / row.totalMs`. The denominator is the
row's total (spawn + update + render) rather than the breakdown subtotal so
authors see "this module is 40% of this emitter" rather than "this module
is 40% of the measured modules"; a gap between `breakdown.totalMs` and
`row.totalMs` indicates un-instrumented dispatches. Zero-row-total emitters
(purely culled) produce zero percents instead of dividing by zero.

## Widget integration (phase 7.17.1)

The widget renders the breakdown when the row is expanded. Suggested layout:

```
▼ fire_explosion        alive 12345  sim 1.20ms  rend 0.60ms  total 1.80ms
    ColorOverLife       Update   1.20ms   66.7%
    Billboard           Render   0.60ms   33.3%
    Gravity             Update   0.40ms   22.2%
```

## Tests

`runtime/tests/engine/vfx/test_vfx_debug_hud_modules.cpp` covers: report
append, `Clear()` drops both buffers, sorted attach, duplicate accumulation,
stage isolation, percent-of-emitter total, zero-row-total percent guard,
orphan discard, module-buffer drain on Aggregate, two-emitter independence,
stage-name helper.
