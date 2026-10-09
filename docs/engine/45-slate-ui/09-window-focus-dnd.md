# Slate UI — Focus + Tooltip + Drag-and-Drop (Phase 18, Step 9)

**Status**: Complete for Focus, Tooltip, and DragDrop. Window
infrastructure (HWND + swapchain wrapping for modal popouts) deferred
into Step 10's editor — every existing demo / editor already runs in
its own window via `DemoApplication`, so a separate `Window` class
adds value only when the VFX editor needs *secondary* windows.

## Files

- `runtime/engine/ui/slate_focus.{h,cpp}` — `FocusManager`, `Focus()`.
- `runtime/engine/ui/slate_tooltip.{h,cpp}` — `TooltipManager`, `Tooltip()`.
- `runtime/engine/ui/slate_dragdrop.{h,cpp}` — `DragDropManager`, `DragDrop()`, `DragOperation`.
- Updated `slate_widget.h` — `OnFocusReceived/Lost`, `IsFocusable()`,
  `OnDragEnter/Leave`, `OnDrop()` virtuals.
- Updated `slate_editable_text.h` — opts into the tab cycle.
- `runtime/tests/engine/ui/test_slate_focus_tooltip_dnd.cpp` — 16 tests.

## FocusManager

```cpp
FocusManager& Focus();      // process-wide singleton

Focus().SetRoot(rootWidget);
Focus().SetFocus(editText); // fires OnFocusLost on previous + OnFocusReceived on new
auto next = Focus().CycleNext();   // Tab
auto prev = Focus().CyclePrev();   // Shift+Tab
```

Mechanics:

- `SetFocus` short-circuits when the new target equals the current one
  (no spurious lost/received pair).
- `CycleNext` / `CyclePrev` walk the tree DFS via `HitTestChildren()`,
  collect every widget where `IsFocusable() == true`, and advance with
  wrap-around. Empty trees / no focusable widgets return nullptr.
- `EditableText::IsFocusable()` returns true; its `OnFocusReceived`
  delegates to `Focus()`, `OnFocusLost` to `Blur()`. Other widgets are
  non-focusable by default.

## TooltipManager

```cpp
TooltipManager& Tooltip();

Tooltip().Register(button, []{ return New<TextBlock>().Text("Save"); }, 0.5f);

// Per-frame:
Tooltip().Update(hoveredWidget, cursorPos, dt);
auto active = Tooltip().Active(); // SP<Widget> or nullptr
auto anchor = Tooltip().AnchorPos();
```

Behaviour:

- Hover < `delaySec` (default 0.5 s) → tooltip not yet materialised.
- Hover ≥ delay → factory called once, result cached as `Active`.
- Hover changes (cursor leaves owner) → active cleared, timer reset.
- Owner widget destroyed → entry auto-cleaned on next Update via
  `WP<Widget>::expired`.

The host (Step 10) draws the active tooltip on the TopMost layer at
the recorded `AnchorPos`. Tooltip itself never touches the renderer.

## DragDropManager

```cpp
DragDropManager& DragDrop();

DragDrop().BeginDetect(downPos, sourceWidget);

// Per Move:
DragDrop().Update(cursorPos, hoveredWidget,
    [&](Vec2) {
        auto op = std::make_shared<DragOperation>();
        op->typeTag = "vfx.module";
        op->payload = moduleId;
        return op;
    });

// On Up:
bool dropped = DragDrop().EndDrop(upPos, hoveredTarget);
```

Lifecycle:

1. `BeginDetect(start, source)` — records the start position. Detection
   is silent until the cursor moves past `kDragThreshold = 4` Slate units.
2. `Update` is called every PointerMove. Below threshold → no-op.
   Crossing the threshold invokes `srcOnDragDetected(currentPos)`; if
   it returns a non-null `DragOperation`, the manager enters the
   dragging state.
3. While dragging, target hover transitions fire `OnDragEnter` /
   `OnDragLeave` on the widgets under the cursor.
4. `EndDrop(pos, target)` → if dragging and target non-null, calls
   `target->OnDrop()` and clears state.
5. `Cancel()` aborts without firing OnDrop (e.g. ESC).

`DragOperation::payload` is `std::any` — receivers downcast via
`std::any_cast<MyType>(op.payload)`. `typeTag` is a string convention
for cheap pre-filtering ("vfx.module" / "asset.uzmesh").

## Window class (deferred)

A dedicated `Window` class that wraps an HWND + DXGI swapchain for
secondary windows (modal "Save As?" dialog, popout viewport) is not
shipped in Step 9. The current `DemoApplication` and `vfx_viewport_app`
are already single-window setups; a second window adds value only for
the VFX editor's confirm-discard-changes modal. That's deferred into
Step 10's editor where the modal's exact shape (size, parent,
input-gating semantics) is decided alongside the editor's overall
flow. The Focus / Tooltip / DragDrop pieces above are already
window-agnostic — they operate on widget trees, not on HWNDs — so Step
10 inherits the full input infrastructure without rework.

## Test coverage

Slate suite is now **114/114** passing in 2 ms total:

| Suite                | New tests | Description |
|----------------------|-----------|-------------|
| SlateFocus           | 6         | SetFocus hooks, same-target no-op, CycleNext skip + wrap, CyclePrev backwards, empty tree returns nullptr. |
| SlateTooltip         | 4         | No-materialize before delay, materialize once after delay, hover change drops active, AnchorPos updates. |
| SlateDragDrop        | 6         | Below threshold no drag, above threshold drag detected, OnDragEnter/Leave on target change, EndDrop fires OnDrop, EndDrop with no target clears, Cancel drops state without firing OnDrop. |

## Visualization

Real demo with click → tooltip → drag-and-drop reorder rolls up into
Step 10 (`vfx_viewport_app` rebuild) where an actual Niagara module
list demonstrates DragDrop reordering on screen. The behavioural
contracts above are the deterministic layer this on-screen demo will
ride on.

## Design choices

- **Singletons not contexts.** Same rationale as `Styles()` in Step 6:
  the UI thread is single, and the per-window `FocusManager` /
  `TooltipManager` / `DragDropManager` would introduce a context-
  passing burden across every widget for no observable benefit.
  Multi-window (Step 10's modal) carries its own state via the
  existing `DemoApplication` pattern; cross-window drag is out of
  scope.
- **TopMost host-driven.** Active tooltip / drag ghost / popups are
  exposed as `SP<Widget>` queries (`Tooltip::Active`, `DragDrop::Active`).
  The host paints them on whatever its TopMost layer is. The framework
  doesn't dictate the layer — Step 10's editor declares its own.
- **`std::any` payload, `string` typeTag.** Mirrors the proposal
  exactly: type-erased payload for flexibility, prefilter tag for
  cheap downcast avoidance.
- **`Cancel()` still fires OnDragLeave.** Defensive: a cancelled drag
  shouldn't leave a "drag-hover" highlight stuck on the last entered
  widget. Tests pin this behaviour.
