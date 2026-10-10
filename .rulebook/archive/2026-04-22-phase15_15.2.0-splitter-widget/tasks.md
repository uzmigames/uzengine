## 1. Implementation

- [x] 1.1 Created `runtime/engine/vfx/editor/vfx_splitter_widget.{h,cpp}` — `VfxSplitterWidget { BeginDrag, UpdateDrag, EndDrag, IsDragging, IsHovered, SetHovered, HitTest, Render }` with internal drag-anchor state
- [x] 1.2 Gutter geometry: 6-px hit region, 2-px centred bar, `borderSubtle` idle / `borderStrong` hover / `accentPrimary` dragging; 4-px vertical padding on each bar end
- [x] 1.3 `HitTest` uses `x ∈ [bounds.x, bounds.x+width)` + `y ∈ [bounds.y, bounds.y+height)` strict inclusive-left/exclusive-right semantics
- [x] 1.4 `BeginDrag(x0)` stores anchor; `UpdateDrag(x1)` returns `x1 - anchor` and updates anchor; `EndDrag()` clears the flag + anchor
- [x] 1.5 `IsDragging()` flag persists across frames until `EndDrag`; host renders accent-tinted bar even when cursor leaves the 6-px hit region

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
