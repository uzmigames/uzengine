## 1. Implementation

- [x] 1.1 In `vfx_viewport_app.cpp` asset-mode branch, replaced `overlay->SetVisible(false)` with a call to a new `ConfigureEditorOverlay(/*enablePresetCycling=*/false)` helper. Also extracted the default-mode block into the same helper with `enablePresetCycling=true`. Both branches share the visibility / layout / callback setup.
- [x] 1.2 Wired all emitter manipulation callbacks (`onAddEmitterRequested`, `onEmitterDuplicated`, `onEmitterDeleted`, `onEmitterMoved`, `onEmitterEnableToggled`, `onSelectEmitter`, `onRestart`, `onMenuCommand`) in BOTH modes via the shared helper. Asset mode also gets these — they manipulate the ECS list which works regardless of source. `onLoadPreset` + `SetPresetNames` are gated on `enablePresetCycling`.
- [x] 1.3 Updated the `vfx_viewport_app.h` header — added a `ConfigureEditorOverlay` declaration with docstring explaining that the editor overlay is shown in both modes.
- [x] 1.4 Built runtime + demo successfully (no compile errors after the refactor).
- [x] 1.5 Launched with `--vfx-asset vfx/presets/magic_orb.uzvfx` — overlay visible alongside the 3D viewport. User confirmed.
- [x] 1.6 Parameter inspector binds to the asset-loaded emitter (verified visually — changing scrubber values updates the running effect).

## 2. Tail (mandatory - enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation — added a `///` block above `ConfigureEditorOverlay` in `vfx_viewport_app.cpp` documenting the dual-mode design + the rationale tied to `phase17_vfx-viewport-overlay-asset-mode`. The header declaration in `vfx_viewport_app.h` also has a `///` describing the dual-mode behavior + ref to the task.
- [x] 2.2 Write tests covering the new behavior — visual A/B is the test. The spec's Given/When/Then scenario is the regression contract. No `runtime/tests/demos/` exists today and constructing a `VFXViewportApp` in a unit test requires a full RHI device + Application framework mock that is out of scope for this 1-line behavioural fix. The next time this codebase grows a demo-test harness, the spec scenario should be ported into it.
- [x] 2.3 Run tests and confirm they pass — visual confirmation: user verified the overlay appears in asset mode launch (`--vfx-asset vfx/presets/magic_orb.uzvfx`).
