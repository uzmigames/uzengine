# Proposal: phase17_vfx-viewport-default-scene-no-uzvfxsys

## Why

The default scene of `runtime/demos/vfx_viewport_app` (no `--vfx-asset`
CLI argument) hard-codes a dependency on `vfx/systems/fireball.uzvfxsys`
- a compound VFX system asset that DOES NOT EXIST in the repository.
Result on launch:

```
[VFX] [error] VfxSystem: failed to read file 'vfx/systems/fireball.uzvfxsys'
[VFX] [error] SyncVfxEmitters: failed to load VfxSystem asset 'vfx/systems/fireball.uzvfxsys' - skipping
```

The error spams every frame. The compound emitter never spawns. The
"FireballProj" preset (preset index 0) is broken because it depends on
the same compound. The standalone emitter for the OTHER 21 presets DOES
work - preset cycling via Tab / Left/Right rewrites the standalone
emitter's parameters via the in-code `ApplyPreset_*` functions in
`runtime/engine/vfx/components/emitter_presets.h`. But the user
experience is degraded: error log spam + a broken preset slot.

The asset path `m_systemAssetPath = "vfx/systems/fireball.uzvfxsys"`
was authored when the team intended to ship a multi-emitter compound
("flame head + sparks + smoke"), but the `.uzvfxsys` was never created.
The directory `sample/content/vfx/systems/` does not exist either.

## What Changes

### Option A (preferred): kill the dependency

Drop the `VfxSystemComponent` setup entirely in default mode. The 22
in-code presets (`ApplyPreset_Fire`, `ApplyPreset_Smoke`, etc.) already
provide all the visual variety; the compound was redundant. Replace
`ApplyPreset_FireballProj` (preset index 0) with a compatible
single-emitter implementation OR demote it to alias another preset
(e.g., `ApplyPreset_Fireball`).

Concretely:

1. `runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp`
   - Remove the `Fireball Compound` block (lines 287-305 in the current
     file) that creates `m_vfxEntity` with `VfxSystemComponent`.
   - Remove the `m_vfxEntity` member if it is no longer referenced
     anywhere else (audit `vfx_viewport_app.cpp` + `.h` for remaining
     uses).
   - Remove `m_systemAssetPath` member + the `--vfxsys` CLI override
     code path. Document the removal in the `.h`.
   - Replace the `kPresetFns[0]` entry (currently
     `ApplyPreset_FireballProj`) with a working preset (probably
     `ApplyPreset_Fireball` - the standalone large-fireball one - and
     update `kPresetNames[0]` accordingly).

2. `runtime/engine/vfx/components/emitter_presets.h` - if
   `ApplyPreset_FireballProj` only exists as a stub for the missing
   compound, remove it.

3. `runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp` startup log:
   change `"VFXViewportApp: fireball compound created via
   VfxSystemComponent"` to a log line describing the actual standalone
   emitter setup.

### Option B (alternative, only if A is rejected)

Author the missing `sample/content/vfx/systems/fireball.uzvfxsys`
asset as a real `.uzvfxsys` JSON document referencing 3 child
emitters (head + sparks + smoke). This keeps the compound demo path
alive but adds a content-pipeline cost (need to inspect the
`.uzvfxsys` schema and write a hand-authored asset).

The team has historically preferred Option A (kill the dead path) over
Option B (complete the dead path) - see commit history for the legacy
`vfx_viewport` removal patterns.

## Impact

- Affected specs: `vfx-viewport-app-default-scene` (NEW spec - pinning
  the contract that no asset file is required).
- Affected code: `runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp`,
  `runtime/demos/vfx_viewport_app/vfx_viewport_app.h`, possibly
  `runtime/engine/vfx/components/emitter_presets.h`.
- Breaking change: NO. CLI surface unchanged for `--vfx-asset` mode.
  The `--vfxsys` flag becomes a no-op (or warning) - document in the
  CLI help text.
- User benefit: launching `vfx_viewport_app.exe` with no arguments
  produces a working scene with no error spam. Tab cycling works on
  all 22 presets. The editor overlay (Niagara-parity, phase 11) is
  fully functional.

## Dependencies

- Prerequisite for: `phase17_vfx-viewport-22-preset-validation` (cannot
  validate all 22 presets while preset 0 is broken).
- Independent of: `phase17_vfx-viewport-overlay-asset-mode` (overlay
  visibility fix is orthogonal).
- Independent of: `phase17_vfx-viewport-smoke-color-fix` (smoke color
  is a per-preset rendering issue, unrelated to the compound).
