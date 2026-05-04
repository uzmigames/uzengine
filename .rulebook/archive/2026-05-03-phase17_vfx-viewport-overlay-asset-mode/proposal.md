# Proposal: phase17_vfx-viewport-overlay-asset-mode

## Why

`runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp:251` explicitly
HIDES the entire Niagara-parity editor overlay (parameter inspector,
splitter, color picker, module stack picker, color-over-life gradient
editor, all of `runtime/engine/vfx/editor/`) when the demo runs in
asset mode (`--vfx-asset <path>`):

```cpp
if (auto* overlay = GetVfxEditorOverlay()) {
    overlay->SetVisible(false);  // overlay is preset-centric; off in asset mode
}
```

The comment is misleading. The Niagara-parity editor (phase 11) is
explicitly designed to work for ANY single emitter, not "preset-centric".
The phase 11 widgets (`VfxParameterInspectorWidget`,
`VfxEditorPaneWidget`, etc. in `runtime/engine/vfx/editor/`) bind to a
live `ParticleEmitter*` via direct pointer, regardless of where that
emitter came from.

User-visible consequence: when launching with `--vfx-asset
vfx/presets/magic_orb.uzvfx` (or any other asset path), the demo shows
ONLY the 3D viewport with no UI. The user has no way to inspect or
tune the emitter's parameters live.

A local patch (uncommitted, applied during the 2026-05-03 debugging
session) inverts the conditional to `SetVisible(true)` and additionally
applies the same `SetNiagaraMode(true) + SetLayoutMode(SplitHorizontal)`
calls that the default-mode branch uses. This change has been visually
verified to render the editor overlay correctly in asset mode. The
task formalises that fix.

## What Changes

1. `runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp` - in the
   asset-mode branch of `OnStart` (lines ~250-258), replace
   `overlay->SetVisible(false)` with the same overlay configuration
   sequence used in the default-mode branch:
   - `overlay->SetVisible(true)`
   - `overlay->SetNiagaraMode(true)`
   - `overlay->SetLayoutMode(VfxEditorLayoutMode::SplitHorizontal)`
   - Skip `overlay->SetPresetNames(...)` (preset cycling stays
     disabled in asset mode per `m_currentPreset = -1`).
   - Skip `overlay->onLoadPreset` callback (no preset to load).
   - Wire `overlay->onRestart`, `overlay->onMenuCommand`, and the
     emitter manipulation callbacks (`onAddEmitterRequested`,
     `onEmitterDuplicated`, etc.) the same way default mode does -
     these manipulate the ECS entity list, which works in both modes.

2. `runtime/demos/vfx_viewport_app/vfx_viewport_app.h` - update the
   docstring on `m_emitterAssetPath` to remove any "overlay disabled
   in asset mode" wording.

3. `docs/guides/vfx-engine-integration.md` - if it mentions overlay
   being preset-only, correct it. Add a screenshot of the editor
   running in asset mode if helpful.

## Impact

- Affected specs: `vfx-viewport-app-overlay` (NEW spec - the contract
  is "overlay is always visible by default; user can toggle it via the
  editor key but the asset mode does NOT auto-hide it").
- Affected code: `runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp`
  (one branch, ~10 lines), `vfx_viewport_app.h` (docstring),
  `docs/guides/vfx-engine-integration.md` (optional doc update).
- Breaking change: NO behavioural change to scripts that toggle the
  overlay key. The default visibility flips from hidden to visible in
  asset mode only.
- User benefit: the editor (parameter inspector, splitter, color
  picker, module stack picker) is now accessible when loading any
  `.uzvfx` asset. Live tuning of asset-loaded emitters becomes possible
  for the first time.

## Dependencies

- Independent of: `phase17_vfx-viewport-default-scene-no-uzvfxsys`
  (default-scene fix is orthogonal).
- Independent of: `phase17_vfx-viewport-smoke-color-fix`.
- Prerequisite for: `phase17_vfx-viewport-22-preset-validation` (the
  validation script needs the overlay visible to read the preset
  status text).
