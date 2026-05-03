## 1. Implementation

- [ ] 1.1 In `vfx_viewport_app.cpp` asset-mode branch (lines ~250-258), replace `overlay->SetVisible(false)` with the same overlay configuration calls used by the default-mode branch (`SetVisible(true)`, `SetNiagaraMode(true)`, `SetLayoutMode(SplitHorizontal)`).
- [ ] 1.2 Wire the emitter manipulation callbacks (`onAddEmitterRequested`, `onEmitterDuplicated`, `onEmitterDeleted`, `onEmitterMoved`, `onEmitterEnableToggled`, `onSelectEmitter`, `onRestart`, `onMenuCommand`) in asset mode using the same lambdas the default-mode branch uses. Skip `onLoadPreset` and `SetPresetNames` since preset cycling stays disabled.
- [ ] 1.3 Update the `m_emitterAssetPath` docstring in `vfx_viewport_app.h` to remove any "overlay disabled in asset mode" wording. Add a one-line `///` note explaining that the editor overlay is shown for both default and asset modes.
- [ ] 1.4 Build runtime + demo (`zig build -Ddll=true -Doptimize=ReleaseFast` then in `runtime/demos/vfx_viewport_app/`).
- [ ] 1.5 Launch with `--vfx-asset vfx/presets/magic_orb.uzvfx` and verify the editor overlay (parameter inspector, splitter, etc.) is visible alongside the 3D viewport.
- [ ] 1.6 Verify the parameter inspector binds to the asset's loaded emitter - changing a value in the inspector visibly changes the running effect.

## 2. Tail (mandatory - enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation - update `docs/guides/vfx-engine-integration.md` Section "Editor Overlay" (create if absent) explaining that the Niagara-parity editor is the SAME widget set in both default-mode and asset-mode. Optionally embed a screenshot showing the overlay over an asset-loaded emitter.
- [ ] 2.2 Write tests covering the new behavior - add an integration check (or extend existing) in `runtime/tests/demos/vfx_viewport_app_test.cpp` that constructs the demo with an asset path, runs `OnStart()`, and asserts `GetVfxEditorOverlay()->IsVisible() == true`.
- [ ] 2.3 Run tests and confirm they pass - `runtime/build/bin/test_runtime.exe --gtest_filter="*VfxViewportApp*Overlay*"` 100% pass.
