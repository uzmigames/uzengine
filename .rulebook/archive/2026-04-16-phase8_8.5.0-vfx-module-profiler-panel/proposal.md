# Proposal: phase8_8.5.0-vfx-module-profiler-panel

## Why
The debug HUD (7.17.0) shows per-emitter GPU cost. Module-level drill-down is the next layer: which module inside an emitter is the slow one? Niagara breaks timing down per module; UzEngine aggregates everything into sim/render totals.

## What Changes
Extend the timestamp-query infrastructure to bracket each module dispatch with a label. Aggregator produces a per-module breakdown visible in the debug HUD's expanded row.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/editor/vfx_debug_hud.h, timestamp-query harness
- Breaking change: NO
- User benefit: Fast root-cause attribution when an emitter is slow.
- Estimated effort: M

## Source
UE5 FNiagaraPerfReportModule + Stats2 per-module breakdown.
