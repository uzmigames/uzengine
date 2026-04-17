# phase11_11.13.0-niagara-viewport-editor-integration

Final wiring: compose all 11.x widgets into a unified 4-panel editor layout.

Reference: UE5 Niagara System Editor window + all 11.x tasks.

## 1. Layout reshape (VfxEditorOverlay)

- [ ] 1.1 New layout constants: `kStackPanelWidthFrac = 0.25f`,
      `kCenterPanelWidthFrac = 0.35f`, `kRightPanelWidthFrac = 0.20f`,
      `kTimelineBarHeight = 32.0f`. Explicit compute in `SetScreenSize`.
- [ ] 1.2 BuildUI routes:
      - Left panel → `VfxStackPanelWidget` (11.2).
      - Center panel → context-switched renderer:
          * `CenterMode::ModuleParams` → legacy slider form (existing).
          * `CenterMode::Curves` → `VfxCurvesEditorWidget` (11.5).
          * `CenterMode::Parameters` → `VfxParameterInspectorWidget`
            right-pane (11.4).
          * `CenterMode::NodeGraph` → `VfxNodeGraphWidget` (10.4).
      - Right panel: tab bar with Parameters list (11.4 left-pane) +
        Snippet Library (9.4).
      - Bottom bar → `VfxTimelineScrubberWidget` (11.6) with burst
        markers (11.8).

## 2. Focus routing

- [ ] 2.1 Each widget sets `m_focusedPanel` on mouse-hover + click.
- [ ] 2.2 Tab cycles focus; Escape clears.
- [ ] 2.3 Widget under focus consumes keystrokes (11.10 handler).

## 3. Menu bar additions

- [ ] 3.1 File menu: Save (Ctrl+S → `EditorSavePipeline`), Open, Revert.
- [ ] 3.2 Edit menu: Undo (Ctrl+Z), Redo (Ctrl+Y), Clear History.
- [ ] 3.3 View menu: toggle each panel's visibility.
- [ ] 3.4 Preset menu (existing).
- [ ] 3.5 Help menu: keybindings cheat sheet popup.

## 4. Thumbnail queue drain

- [ ] 4.1 Add `UpdateThumbnailCaptureQueue(dt)` call to
      `VFXViewportApp::OnUpdate(dt)` per 11.12.

## 5. Undo wiring

- [ ] 5.1 Every widget receives a `VfxUndoStack*` (owned by the overlay).
- [ ] 5.2 All mutations route through 11.9 commands — no direct writes
      to `VfxStack` / `VfxAttributeStore` / curves from widget code.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 6.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/51-niagara-layout.md` with annotated
      screenshot of the final layout.
- [ ] 6.2 Write tests covering the new behavior
      Layout math (widths/heights at various screen sizes),
      center-mode switching, focus routing.
- [ ] 6.3 Run tests and confirm they pass
      `zig build` clean; full QA playthrough:
      - load fireball, edit color curve, add burst, save
      - verify thumbnail updates, undo/redo work, keybindings respond.
