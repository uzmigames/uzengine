# Proposal: phase11_11.14.0-mount-vfx-editor-composition

## Why

Phase 11 delivered 13 isolated tasks (stack panel, module picker, parameter
inspector, curves editor, timeline scrubber, spawn-rate curve, burst events,
undo/redo, keybindings, override indicators, thumbnail drainer, editor
composition) that together constitute a Niagara-parity VFX editor. Every
widget is data-complete and unit-tested. **But nothing is visible in the
running demo.** The `VfxEditorComposition` class (phase 11.13) is an
opt-in integration layer that is never instantiated inside
`vfx_viewport_app`, so the author runs the app and sees only the
legacy 5-stage overlay from early phase 8/9.

This task closes the gap: it mounts `VfxEditorComposition` inside
`VFXViewportApp` so F11 toggles between the legacy overlay and the
Niagara-parity composition. Every phase-11 widget becomes visible,
interactive, and testable live in the demo.

## What Changes

1. **`VfxEditorOverlay` gains a Niagara mode.** A new `SetNiagaraMode(bool)`
   flag routes `BuildUI()` to render the `VfxEditorComposition` widgets
   instead of the legacy slider column. The legacy path remains the
   default for backward compatibility with the rest of phase 8/9 demos.
2. **Overlay instantiates phase-11 widgets** (`VfxStackPanelWidget`,
   `VfxParameterInspectorWidget`, `VfxCurvesEditorWidget`,
   `VfxTimelineScrubberWidget`, `VfxModulePickerDialog`, `VfxUndoStack`,
   `VfxThumbnailDrainer`, `VfxEditorKeybindingHandler`,
   `VfxEditorComposition`) as owned members — not pointers.
3. **Widgets render through the overlay's existing `TextRenderer` +
   `BitmapFont`** — no new GPU resources, no new shaders. Each widget's
   `Render(text, font, rect)` is called with a rect derived from
   `VfxEditorComposition::ComputeLayout(screenW, screenH)`.
4. **Hit-tests route through the composition.** Mouse events on the
   stack panel region call `m_stackPanel.HandleMouse(x, y, down)`; on
   the timeline region, `m_timeline.HandleMouse(...)`, etc. A single
   `VfxFocusedPanel` tracks which widget owns the last click.
5. **`VFXViewportApp` gains F11 toggle.** Pressing F11 calls
   `overlay->ToggleNiagaraMode()`. The existing F10 toggle still
   shows/hides the overlay entirely. F11 only matters while the
   overlay is visible.
6. **Composition binds to the active emitter's VfxStack** via
   `m_stackPanel.Bind(&emitter->stack)` in the per-frame update.
   When the stack is empty (the common case for existing presets),
   the stack panel shows only the 7 stage headers with `+ Add Module`
   rows — still a full Niagara-parity authoring surface.

## Impact

- Affected specs: `specs/vfx-editor-mount/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_editor_overlay.{h,cpp}` — Niagara-mode
    path, widget members, `ToggleNiagaraMode`, composition wiring.
  - `runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp` — F11 binding.
  - `runtime/tests/engine/vfx/test_vfx_editor_mount.cpp` — new tests.
  - `docs/engine/25-vfx/52-editor-mount.md` — new doc.
- Breaking change: NO. Niagara mode is off by default; existing callers
  see no behavior change. The legacy `BuildStageTree`/`BuildStackPanel`
  paths remain untouched.
- User benefit: every phase-11 widget becomes visible, interactive, and
  testable live in the demo. The author can finally see the stack panel,
  module picker, parameter inspector, curves editor, and timeline
  scrubber running against a real GPU-backed emitter.
