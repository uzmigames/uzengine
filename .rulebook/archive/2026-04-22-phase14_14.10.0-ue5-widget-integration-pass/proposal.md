# Proposal: phase14_14.10.0-ue5-widget-integration-pass

## Why

Phases 14.5 (toolbar), 14.6 (status bar), and 14.4 (tree) shipped
standalone widgets with 100 % unit-test coverage. They are unmounted
— widgets exist in the codebase but nothing in the host overlay
instantiates them yet. User sees the same phase-14.2/14.3 render
in the running app.

## What Changes

1. **Toolbar mount** — `VfxEditorOverlay` gains a `VfxToolbarWidget
   m_toolbar`. Composition adds a 28-px strip directly below the
   emitter bar. Four buttons: Save System / Restart / Play-Pause
   (toggle) / Capture Thumbnail. One separator between Restart
   and Play-Pause.
2. **Status bar mount** — `VfxEditorOverlay` gains a
   `VfxStatusBarWidget m_statusBar`. 22-px strip docked at the
   bottom, above the timeline scrubber. Host feeds `SetFps` from
   `Application::GetFps()` + `SetParticleCount` from the bound
   emitter's alive count.
3. **Tree widget preview** — not a full migration of the stack
   panel (that is a bigger surgery). Just ship a documentation
   sample + integration smoke demo so phase 14.4 is visibly
   grounded.
4. **Composition geometry** — shrink the stack/center/right-panel
   heights by the toolbar (28) + status-bar (22) strips so the
   main content area respects the new chrome.

## Impact

- Affected specs: none (widget specs already shipped).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_editor_overlay.{h,cpp}` (owns
    the toolbar + status bar, wires per-frame state).
  - `runtime/engine/vfx/editor/vfx_editor_composition.{h,cpp}`
    (adds toolbar + status-bar strip heights to the layout).
- Breaking change: NO — existing call sites untouched; new chrome
  docks inside the already-reserved strips.
- User benefit: the running editor finally surfaces the phase-14
  widgets the user can see + click.
