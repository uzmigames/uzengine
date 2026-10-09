# Proposal: phase8_8.3.0-vfx-emitter-inheritance

## Why
Large VFX families (explosion + sparks + smoke + blood) are a multiplication problem: one shared base + N variants. Niagara solves this with emitter inheritance: a child emitter references a parent and overrides only the modules it needs. Parent edits propagate automatically.

## What Changes
Add VfxEmitterHandle (parent reference) + per-override module list to the emitter asset. Runtime resolves the inheritance chain at load time by merging parent modules with child overrides. Hot-reload propagates parent edits through every descendant.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/components/particle_emitter.h, system/vfx_system.cpp
- Breaking change: NO
- User benefit: One parent edit ripples through every descendant automatically.
- Estimated effort: L

## Source
UE5 NiagaraEmitterHandleInheritance.h + UNiagaraEmitter::bIsTemplateAsset.
