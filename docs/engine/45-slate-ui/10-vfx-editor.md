# Slate UI — VFX Editor Rebuild (Phase 18, Step 10)

**Status**: Editor class complete; live integration into
`vfx_viewport_app` is the immediate next task. The full Slate
framework (Steps 1-9) is exercised end-to-end by the editor's widget
tree.

## Files

- `runtime/engine/vfx/editor/vfx_editor_slate.{h,cpp}` — `VfxEditorSlate` class.
- `runtime/tests/engine/vfx/test_vfx_editor_slate.cpp` — 9 tests.

## Architecture

```
VfxEditorSlate.GetRoot() → VBox
                             ├── TitleBar    (28 px Auto)
                             ├── Toolbar     (36 px Auto)
                             ├── Workbench   (Fill 1)
                             │     ├── leftCol  (Fill 2)
                             │     │     ├── Viewport  (Fill 1)
                             │     │     └── Graph     (Auto)
                             │     └── rightCol (Fill 1)
                             │           ├── Details  (Fill 1, ScrollBox)
                             │           ├── Stack    (Auto, EditableText + module list)
                             │           └── Curve    (Auto)
                             └── StatusBar   (22 px Auto)
```

The 4-row top + 5-cell workbench mirrors the Claude design layout
(`.sandbox/claude-design/vfx-template/project/styles.css`).

## Frame loop

```cpp
VfxEditorSlate editor;
editor.SetEmitter(system, emitter);
editor.BuildRoot();   // once

// Per frame:
editor.SetTelemetry(currentTelemetry);   // host-side acquisition
editor.Tick(dt);                         // pulls Attribute<T> lambdas
editor.GetRoot()->Measure({screenW, screenH});
editor.GetRoot()->Arrange({{0, 0}, {screenW, screenH}, dpiScale});
editor.GetRoot()->Paint(slateRenderer);
slateRenderer.Submit(text, font, msdf, dpiScale);
```

The editor doesn't own the renderer — the host application drives
input + arrange + paint, just like any other Slate tree. This keeps
the editor decoupled from the runtime's specific GPU initialisation.

## Live data binding

The StatusBar's four telemetry rows use `Attribute<std::string>`
lambdas that close over `m_impl->telemetry`:

```cpp
fpsTb.Get()->Text = std::function<std::string()>([this] {
    return std::string("FPS: ") + std::to_string(m_impl->telemetry.fps);
});
```

When the host calls `SetTelemetry(t)`, no widget invalidation runs —
the next paint pulls the value via the lambda automatically. The
title bar similarly binds to `m_impl->emitterName`, so renaming via
`SetEmitterName` reflects immediately at the next paint.

The Stack panel's `EditableText` for the emitter name is set up with
`OnCommitted = [this](const std::string& v) { SetEmitterName(v); }`,
closing the round-trip: typing in the field commits to the editor
state, and the title bar's lambda picks up the new value at the
following paint.

## Field mapping (current)

| UI Element                     | Editor field                       | Live update path |
|--------------------------------|------------------------------------|------------------|
| Title bar text                 | `emitterName`                      | lambda |
| StatusBar FPS                  | `telemetry.fps`                    | lambda |
| StatusBar Alive                | `telemetry.aliveCount`             | lambda |
| StatusBar Max                  | `telemetry.maxCount`               | lambda |
| StatusBar VRAM                 | `telemetry.vramUsedMb`             | lambda |
| Toolbar tool selection         | `activeTool` int                   | OnClick |
| Stack name input               | `emitterName`                      | OnCommitted |
| Stack module rows              | `modules` vector                   | static (drag-drop reorder lands with the host wire-up) |

The Details panel rows currently render placeholder TextBlocks for
"Emission Rate", "Lifetime", "Is Emitting"; full bindings to
`ParticleEmitter` properties land in the host-integration step
where `system` / `emitter` pointers carry real data.

## Tests

123/123 Slate-suite tests passing in 2 ms total — 114 framework
tests from Steps 1-9 plus 9 editor-specific tests in this step:

- `BuildRootProduces4RowVerticalBox` — the root is a VBox with the
  exact 4 slots in the right order; only the workbench is Fill.
- `WorkbenchHasTwoColumns` — left/right columns with 2:1 fill ratio.
- `ActiveToolStateIsMutable` — `SetActiveTool / GetActiveTool` round-trip.
- `EmitterNameDefaultsAndIsMutable` — defaults to `"particle.uzfx"`.
- `TitleBarReflectsEmitterName` — lambda binding pulls the latest name.
- `StatusBarBindingsReflectTelemetry` — all four telemetry fields
  resolve correctly through their lambdas.
- `BuildRootIsIdempotent` — calling twice replaces but preserves topology.
- `SelectedModuleInitialAndMutable` — -1 default, getter/setter round-trip.
- `TickWithoutRootIsNoOp` — robust to construction without BuildRoot.

## Visualization

Live on-screen visualization happens in the next task: wiring the
`VfxEditorSlate` into `vfx_viewport_app.cpp`. The wire-up is small
because all the heavy lifting is already done:

- The editor class is host-agnostic; the host just owns the
  `SlateRenderer`, calls `Tick / Measure / Arrange / Paint`, and
  forwards input via `InputRouter`.
- The host's existing `TextRenderer` consumes the SlateRenderer's
  `Submit` output 1:1 — no shader changes, no new pipelines.
- The Claude `claude.dark` `StyleSet` is already lazily seeded into
  `Styles()` on first call; the editor pulls colors / brushes by
  name with no wiring required.

The screenshot deliverable (titlebar, toolbar, viewport, graph,
details with row highlighted, stack with module dragged, curve;
plus interaction shots — caret in EditableText, ComboBox open,
Tooltip) lands as part of that wire-up commit, where actual GPU
rendering produces real PNGs.

## Design choices

- **Pimpl**. `VfxEditorSlate::Impl` keeps the header free of every
  Slate include — only `slate_core.h` leaks. Host-side code that just
  drives the editor (`Tick / GetRoot / SetTelemetry`) doesn't pay for
  the full widget header surface.
- **Idempotent BuildRoot**. Calling `BuildRoot` twice rebuilds; the
  old tree is just dropped (last shared_ptr release). Useful for
  layout config swaps later.
- **Toolbar buttons hold a TextBlock content for now** — the proper
  icon path (`Brush(Box9Slice, iconId=VfxUiIcon::SelectArrow)`) lands
  alongside the host integration when the icon atlas is bound to the
  Slate renderer's sprite path.
- **Stack drag-drop reorder, ComboBox renderer-type, and Curve
  editing** require the InputRouter integration in the host wire-up
  to produce visible behaviour. The editor's data model is in place
  (`modules` vector, `selectedModule` int); the input plumbing is the
  remaining piece.
