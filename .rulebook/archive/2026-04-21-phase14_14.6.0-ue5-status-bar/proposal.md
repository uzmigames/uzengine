# Proposal: phase14_14.6.0-ue5-status-bar

## Why

UE5's editor has a persistent bottom status bar that surfaces
context-sensitive information: current FPS, active source
control branch, last saved timestamp, VRAM usage, particle count,
and transient notifications. The phase-13 editor has the timeline
scrubber docked at the bottom but nothing else — global editor
state is scattered across modal dialogs.

Adding a UE5-style status bar gives the user always-visible
feedback on render health (FPS / draw call count / VRAM),
simulation health (alive particle count / emitter count), and
file state (unsaved changes badge).

## What Changes

1. New `VfxStatusBarWidget` — 22 px horizontal strip docked at
   `screenH - 22` above the timeline scrubber (so the scrubber
   floats over the viewport while the status bar anchors the
   chrome).
2. Left cluster (from left): FPS chip (green `#1E8E3E` when >= 55,
   orange `#FF7B00` when 30-54, red `#C0392B` when < 30);
   particle-count chip (`<alive> / <max>`); draw-call chip.
3. Right cluster (from right): VRAM chip (`used / total MB`);
   saved-state chip (`● unsaved` when dirty, else `✓ saved`);
   editor version chip.
4. Transient notification slot in the centre — host pushes a
   string + level (Info / Warn / Error) that auto-dismisses after
   3 seconds; colour maps to `stateSuccess / stateWarning /
   stateError`.
5. Status bar fill `barBg`; chips separated by 1-px
   `borderSubtle` vertical rects; text 10 px MSDF
   `textSecondary`.

## Impact

- Affected specs: `specs/ue5-status-bar/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_status_bar_widget.{h,cpp}`
    (new).
  - `runtime/engine/vfx/editor/vfx_editor_overlay.cpp`
    (composition — docks the status bar above the timeline
    strip).
- Breaking change: NO — additive.
- User benefit: always-visible telemetry replaces the
  "silent failure" feel of the current editor.
