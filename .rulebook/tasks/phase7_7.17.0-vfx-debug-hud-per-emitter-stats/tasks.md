# Phase 7-7.17.0 - VFX Debug HUD (Per-Emitter Stats)

In-engine overlay with per-emitter perf stats, budget usage, and top-offender highlighting.

Reference: UE5 NiagaraDebugHud

## 1. Data collection

- [ ] 1.1 Tap existing GPU timestamp queries (from --gpu-profile) for per-emitter CS + render timings.
- [ ] 1.2 Aggregate per-frame into VfxDebugHudStats (alive, max, csMs, renderMs, lodLevel, budgetPct).

## 2. Rendering

- [ ] 2.1 HUD layout: 6-column table view, font scale-aware.
- [ ] 2.2 Top-N highlight (top 3 emitters by total cost).
- [ ] 2.3 Budget bar: total alive particles vs VFXBudget::maxParticles.

## 3. Toggle

- [ ] 3.1 F10 keyboard toggle (configurable).
- [ ] 3.2 CVar fx.debug at runtime.

## 4. Editor integration

- [ ] 4.1 Click-to-select: clicking an emitter row selects it in the VFX editor for parameter editing.

## 5. Docs and tests

- [ ] 5.1 Documentation on HUD columns, toggle, click-to-select workflow.
- [ ] 5.2 Smoke test: HUD renders without GPU validation errors.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 6.1 Update or create documentation covering the implementation
- [ ] 6.2 Write tests covering the new behavior
- [ ] 6.3 Run tests and confirm they pass
