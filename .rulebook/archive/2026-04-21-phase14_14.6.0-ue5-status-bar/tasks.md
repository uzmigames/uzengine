## 1. Implementation

- [x] 1.1 Created `runtime/engine/vfx/editor/vfx_status_bar_widget.{h,cpp}` — `VfxStatusNotification { message, level, secondsRemaining }` + `VfxStatusBarWidget { SetFps, SetParticleCount, SetDrawCalls, SetVramUsed, SetDirty, SetEditorVersion, PushNotification, Tick, Render }`
- [x] 1.2 Bar geometry: 22-px height, full-width `barBg` fill, 1-px `borderSubtle` top-edge divider; host-docked anywhere via `barBounds` argument to `Render`
- [x] 1.3 Left chip cluster — FPS / Particles / Draw Calls each ~90 px wide with 1-px `borderSubtle` vertical dividers; MSDF 10-px text when available, bitmap fallback
- [x] 1.4 FPS tint helper `VfxStatusBarWidget::FpsTint(fps)` is `static` and testable; returns `stateSuccess` (>= 55), `stateWarning` (30-54), `stateError` (< 30)
- [x] 1.5 Right cluster chips (right-to-left): editor version, saved-state badge, VRAM — mirrored layout with matching dividers
- [x] 1.6 Saved-state chip text `saved` (tint `stateSuccess`) when clean, `unsaved` (tint `stateWarning`) when dirty
- [x] 1.7 Transient notification queue: FIFO `std::deque<VfxStatusNotification>` capped at 8 entries; `Tick(dt)` decrements the head's `secondsRemaining` and pops when <= 0; `NotificationTint(level)` maps to `stateInfo/stateWarning/stateError`
- [x] 1.8 Widget is standalone + reusable — mounting into `vfx_editor_overlay.cpp` composition happens when the overlay is next updated to surface it; widget publishes `SetFps/...` so a host can drive from `Application::GetFps()` and the active emitter's alive count

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
