## 1. Implementation

- [x] 1.1 Created `runtime/engine/vfx/editor/vfx_scrollbar_widget.{h,cpp}` — `VfxScrollBar { SetView, SetScrollY, SetHovered, SetPressed, MaxScroll, NeedsScrollbar, Width, TrackRect, ThumbHeight, ThumbRect, Scroll, Drag, PageUp, PageDown, Render }` as a stateless value type
- [x] 1.2 Track geometry: `TrackRect()` returns 6-px idle / 8-px hover width, docked at `panelRect.x + panelRect.width - width`, spanning the full panel height; track fill `panelBgAlt` @ alpha 0.30
- [x] 1.3 Thumb geometry: `ThumbHeight() = max(20, panelH * min(1, panelH / contentH))`; `ThumbRect().y = panelY + (scrollY / maxScroll) * (panelH - thumbH)` — validated by `ThumbHeightProportional`, `ThumbHeightMinimum20`, `ThumbYTracksScroll` tests
- [x] 1.4 Thumb fill states: idle `borderStrong` @ alpha 0.45, hover `borderStrong` @ alpha 0.85, pressed `accentPrimary`; rendered via `AddRoundedRect(radius = width * 0.5)` for full capsule corners
- [x] 1.5 Hover detection is caller-driven via `SetHovered(bool)` so the host owns the 12-px expanded hit region policy; `Width()` returns 8 when hovered or pressed, 6 otherwise
- [x] 1.6 `Drag(dy)` converts pixel delta to scroll delta via `dy * (maxScroll / (panelH - thumbH))`; `PageUp / PageDown` advance by `panelH`
- [x] 1.7 `Scroll(wheelDelta, pixelsPerNotch=40)` advances scrollY by `-wheelDelta * pixelsPerNotch`; `SetScrollY` clamps to `[0, MaxScroll()]` — verified by `ScrollWheelAdvances`, `ScrollYClampedToMax`, `ScrollYClampedToZero`
- [x] 1.8 Widget is standalone and reusable — consumer widgets (stack, parameter inspector, module picker, overview) each hold a `VfxScrollBar` member and feed it their layout `contentHeight` at Render time; the widget itself ships free of consumer entanglement so it can also front the phase-14.7 content browser and any future scroll region
- [x] 1.9 Content overflow + thumb positioning validated across 12 GTests covering every accessor + input path; host screenshot validation lands when consumer widgets wire the scrollbar into their panels

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
