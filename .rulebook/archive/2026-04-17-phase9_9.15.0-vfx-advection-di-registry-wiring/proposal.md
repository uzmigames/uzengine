# Proposal: phase9_9.15.0-vfx-advection-di-registry-wiring

## Why
Phase 8.21.0 shipped `VfxAdvectionFieldConfig` + CPU helpers + HLSL snippet. The DI needs a registry entry (the module compositor must recognise it) and CB-field declaration so the snippet compiles.

## What Changes
Register the advection DI in the module-compositor registry: authors opt in by attaching an `advection_field` DI asset to an emitter. The compositor emits the four CB fields (`advectionFieldSrvIdx`, `advectionWorldMin`, `advectionWorldMax`, `advectionTightness`) into the composed update CS and appends the `VfxAdvectionEmitHLSLSnippet()` body.

## Impact
- Affected specs: docs/engine/25-vfx/38-advection-field.md
- Affected code: runtime/engine/vfx/modules/module_compositor.cpp + VFX asset loader
- Breaking change: NO
- User benefit: Emitters can reference advection-field DI assets.
- Estimated effort: M

## Source
UE5 UNiagaraDataInterface registry pattern.
