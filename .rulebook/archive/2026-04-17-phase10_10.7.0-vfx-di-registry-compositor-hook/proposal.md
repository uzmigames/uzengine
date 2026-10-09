# Proposal: phase10_10.7.0-vfx-di-registry-compositor-hook

## Why
Phase 9.15 (advection DI) and 9.16 (scene SDF DI) both need a way for their HLSL snippet + CB fields to land inside the composed update CS. The `ModuleCompositor` today only emits module shader code from registered `IVFXModule` types — it has no Data Interface registry, so DIs can't participate in the emitted shader.

## What Changes
Add `VfxDIRegistry` to the module compositor: data interfaces register by type name + provide a `(cbFieldsHLSL, helperFunctionsHLSL, dispatchSiteCall)` triplet. When an emitter asset references a DI, the compositor injects all three fragments into the composed update CS. Both 9.15 and 9.16 use this via `VfxAdvectionEmitHLSLSnippet` / `VfxSceneSDFEmitHLSLSnippet`.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/modules/module_compositor.*
- Breaking change: NO
- User benefit: Data interfaces become first-class citizens in shader composition.
- Estimated effort: M

## Source
UE5 UNiagaraDataInterface + FNiagaraScriptCompiler.
