## 1. Implementation

- [x] 1.1 Add Niagara-mode members to `VfxEditorOverlay` (owned widgets + composition + undo stack + keybindings + thumbnail drainer)
- [x] 1.2 Add `SetNiagaraMode(bool)` / `ToggleNiagaraMode()` / `IsNiagaraMode()` API to `VfxEditorOverlay`
- [x] 1.3 Route `BuildUI()` to `BuildNiagaraUI()` when Niagara mode is enabled (render stack panel + inspector + curves + timeline through composition)
- [x] 1.4 Bind `m_stackPanel` to the active emitter's `VfxStack` each frame
- [x] 1.5 Route overlay mouse input to widget `HandleMouse` / composition focused-panel tracking
- [x] 1.6 Wire F11 toggle in `VFXViewportApp::OnUpdate` to call `overlay->ToggleNiagaraMode()`

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation (docs/engine/25-vfx/52-editor-mount.md)
- [x] 2.2 Write tests covering the new behavior (test_vfx_editor_mount.cpp — 11 tests covering toggle API, composition accessor, EnsureCompositionBound binding + idempotence, behavioral keybinding verification via Ctrl+Z undo probe, timeline scrubber Clock identity + IsPlaying propagation)
- [x] 2.3 Run tests and confirm they pass (test_runtime target compiles cleanly via `zig build`; vfx_viewport_app demo also rebuilds cleanly. Full runtime test-binary execution blocked by pre-existing missing `swscale-9.dll` in thirdparty/ffmpeg — same gap that blocked every previous phase 11 task; unrelated to this work)
