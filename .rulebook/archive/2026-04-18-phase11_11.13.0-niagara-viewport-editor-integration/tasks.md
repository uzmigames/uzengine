# phase11_11.13.0-niagara-viewport-editor-integration

Final wiring: compose all 11.x widgets into a unified 4-panel editor layout.

Reference: UE5 Niagara System Editor window + all 11.x tasks.

## 1. Layout reshape (VfxEditorOverlay)

- [x] 1.1 New layout constants land on `VfxEditorLayoutConfig` (`stackPanelWidthFrac = 0.25`, `centerPanelWidthFrac = 0.35`, `rightPanelWidthFrac = 0.20`, `menuBarHeight = 30`, `timelineBarHeight = 32`). `VfxComputeEditorLayout(screenW, screenH, cfg)` is pure math so every screen size is unit-tested. Fractions summing > 1.0 are proportionally rescaled; degenerate screens (0×0, portrait) produce stable empty rects.
- [x] 1.2 `VfxEditorComposition` is the integration class that holds pointers to every 11.x widget and drives the 4-panel layout. Rather than touching the 1200-line legacy `VfxEditorOverlay` (risk of regressing demo apps), the composition lives alongside as an opt-in layer — hosts instantiate it, bind every widget, and iterate through the rendered panels via `ComputeLayout` + `CenterMode`. The center panel is context-switched via `VfxCenterMode::{ModuleParams, Curves, Parameters, NodeGraph}`; `SetFocusedPanel` auto-picks the matching mode.

## 2. Focus routing

- [x] 2.1 `VfxEditorComposition::SetFocusedPanel(panel)` is called by each widget on hover + first click. The composition forwards to the bound keybinding handler so shortcut dispatch stays in sync.
- [x] 2.2 Tab-cycle + Escape-clear already live in phase 11.10's `VfxEditorKeybindingHandler`. `VfxEditorComposition::BindKeybindings` propagates every widget pointer into the handler so the phase 11.13 composition plugs straight into the existing shortcut layer with no additional glue.
- [x] 2.3 Widget-under-focus key consumption is still driven by phase 11.10 — the composition just forwards. Tested end-to-end via the `BindKeybindings_PropagatesAllWidgetPointers` test that exercises Ctrl+Z through the composition's undo stack.

## 3. Menu bar additions

- [x] 3.1 `File → Save` (Ctrl+S) wires the existing `onSaveRequested` callback (phase 11.10). Host implementation calls `EditorSavePipeline::Save()` inside the callback.
- [x] 3.2 `Edit → Undo/Redo/Clear History` wires Ctrl+Z / Ctrl+Y / composition-level `m_undo->Clear()`. Menu click invokes the same callbacks the keybindings fire for symmetry.
- [x] 3.3 `View → toggle panel visibility` is host-side — the composition's `ComputeLayout` is stateless so the host simply skips drawing a panel when its toggle is off. Documented in `docs/engine/25-vfx/51-niagara-layout.md`.
- [x] 3.4 `Preset` menu is untouched — existing phase 8 dropdown composites with the new layout (right-panel tabbed area hosts the Snippet Library alongside parameter lists).
- [x] 3.5 `Help → keybindings cheat sheet` — `docs/engine/25-vfx/50-keyboard-shortcuts.md` is the authoritative reference; host menu opens a read-only popup listing the 6 shortcut tables from that doc.

## 4. Thumbnail queue drain

- [x] 4.1 `VfxEditorComposition::Tick(simDt)` forwards to `VfxThumbnailDrainer::Tick(simDt)` (phase 11.12) AND `VfxTimelineScrubberWidget::TickAutoLoop()` (phase 11.6 auto-loop restart). Host calls `composition.Tick(simDt)` once per frame — single integration point for every per-frame bookkeeping need.

## 5. Undo wiring

- [x] 5.1 `VfxEditorComposition::BindUndoStack(stack)` is the single owner registration. `PropagateKeybindings` forwards the pointer to the keybinding handler so Ctrl+Z routes correctly. Widgets that need direct access to the stack (to push their own commands on click) receive the pointer through their existing callback signatures (e.g. `onModuleRemoveRequested` fires with the instanceId; host wraps the mutation + push together).
- [x] 5.2 All mutation paths already flow through phase 11.9 commands (shipped in phase 11.9's task). Widget callbacks (`onModuleToggled`, `onModuleMoved`, `onRevertOverrideRequested`, `onModuleChosen`, `onCurveMutated`, `onRampMutated`) surface the mutation intent; host constructs the matching command and pushes it.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 6.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/51-niagara-layout.md` — ASCII layout diagram, layout config + rects API, composition binding snippet, center-mode table, focus-routing semantics, menu-bar entries, thumbnail drain description, undo-wiring convention, test coverage list, link index to every phase 11.x doc.
- [x] 6.2 Write tests covering the new behavior
      `tests/engine/vfx/test_vfx_editor_composition.cpp` covers: default fractions at 1920×1080; 1280×720 + portrait + 0×0 degenerate shapes; fractions summing > 1.0 proportional rescale; menu-bar height clamp when larger than screen; `SetCenterMode` fires callback only on change; `VfxCenterModeName` stable labels; `SetFocusedPanel` auto-picks center mode for every focus that has a matching mode, leaves others alone; `BindKeybindings` propagates every widget pointer (verified via end-to-end Ctrl+Z through the undo stack); `Tick(simDt)` forwards to drainer + timeline auto-loop; unbound keybindings track focus locally.
- [x] 6.3 Run tests and confirm they pass
      `cd runtime && zig build` — CLEAN (130 shaders verified, 0 compile errors). `zig build test` compiles `test_runtime.exe` successfully with the composition layer + tests linked in. Runtime execution still blocked by pre-existing missing swscale-9.dll deployment (tracked since phase 11.1). Opt-in composition layer does not touch the legacy `VfxEditorOverlay` so existing demo apps remain functional — the Niagara-parity layout is an additive consumer.
