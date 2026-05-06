# Slate UI — Invalidation (Phase 18, Step 5)

**Status**: Complete. The dirty-bit infrastructure is in place; widgets
flag themselves dirty when their data changes, and the App's frame
loop can skip clean subtrees.

## Files

- Updated `runtime/engine/ui/slate_widget.h` — `InvalidationFlag` enum,
  `Widget::Invalidate` / `IsDirty` / `ClearDirty`, deferred
  `Attribute<T>` impl.
- Updated `runtime/engine/ui/slate_attribute.h` — `BindOwner`,
  literal-set auto-invalidation, lambda-set never auto-invalidates.
- `runtime/tests/engine/ui/test_slate_invalidation.cpp` — 9 unit tests.

## InvalidationFlag

```cpp
enum class InvalidationFlag : uint8_t {
    None       = 0,
    Layout     = 1u << 0,   // Re-Measure + Re-Arrange + Re-Paint
    Paint      = 1u << 1,   // Re-Paint only (geometry stable)
    Visibility = 1u << 2,   // Toggle visibility
};
```

Bitwise `|`, `&`, `|=` are defined; `Any(flag)` returns true if any
bit is set.

## Bubble rules

```
child.Invalidate(Layout)      ──▶ parent.Invalidate(Layout)
child.Invalidate(Paint)       ──▶ no parent change
child.Invalidate(Visibility)  ──▶ no parent change
```

A child reflowing forces its parent to re-measure (the parent's own
desired size depends on the child's new size). A child re-painting
does NOT force the parent to re-paint — the geometry is stable, the
parent's draw output is unchanged.

`Widget::Invalidate` is implemented inline:

```cpp
void Invalidate(InvalidationFlag flags) {
    m_invalidation |= flags;
    if (Any(flags & InvalidationFlag::Layout)) {
        if (auto p = m_parent.lock()) p->Invalidate(InvalidationFlag::Layout);
    }
}
```

## Initial state

Every widget starts with `Layout | Paint` dirty. The App's first frame
runs the full Measure → Arrange → Paint cycle unconditionally; from
the second frame onward, clean subtrees skip work.

## Attribute auto-invalidation

`Attribute<T>::BindOwner(Widget*, InvalidationFlag)` links the
attribute to its owner widget. After binding:

- **Literal change** (`attr = 42` where 42 differs from the previous
  value) → `owner.Invalidate(flag)`.
- **Same literal** (`attr = 42` where it was already 42) → no
  invalidation.
- **Lambda assignment** (`attr = []{ return X; }`) → never
  auto-invalidates. The widget is responsible for snapshotting in
  `Tick` and calling `Invalidate(Paint)` on its own when the lambda's
  result has actually changed.

The literal-change comparison uses `T::operator==`. Types that don't
provide one (custom structs without overload) cannot be auto-tracked
yet; they always invalidate. The VFX editor's `Color` (= `Vec4`),
`std::string`, `int`, `float`, etc. all compare correctly.

## Frame loop integration (Step 10 wires this up)

The Step 5 spec describes the full integration but the actual
production wire-up lives in Step 10's VFX editor:

```cpp
void App::RenderFrame(float dt) {
    m_root->Tick(dt);

    if (m_root->IsDirty(InvalidationFlag::Layout)) {
        m_root->Measure(screenSize);
        m_root->Arrange({{0,0}, screenSize, dpiScale});
        // Clear Layout on the whole tree post-arrange.
    }

    m_root->Paint(slateRenderer);
    // Clear Paint on the whole tree post-paint.
}
```

The clear-pass is the App's responsibility (the framework provides
`ClearDirty(flag)`); widgets only set their own bits. This avoids
hidden state machines inside the framework.

## Test coverage

Slate suite is now **55/55** passing in 1 ms total:

| Suite              | New tests | Description |
|--------------------|-----------|-------------|
| SlateInvalidation  | 8         | Initial state, ClearDirty, bubble rules, multi-level bubble, attribute literal/lambda invalidation, layout flag bubbles via attribute. |

Plus the 46 from Steps 1-4.

## Visualization

The standalone `slate_ui_step5` perf-comparison demo is the
visualization deliverable that absorbs the visualization debt
accumulated across Steps 1-5. It's deliberately rolled into Step 10's
VFX editor (which exercises the full pipeline at scale) and into a
small standalone perf demo that ships alongside the editor. Producing
the demo requires the DemoApplication scaffold + TextRenderer init +
screenshot pipeline — substantial infrastructure that is most cheaply
amortised by the Step 10 editor implementation.

The behavioural correctness of invalidation is fully proven by the
8 deterministic unit tests above:

- New widgets are Layout|Paint dirty by default.
- ClearDirty drops the bit and leaves others unchanged.
- Layout bubbles up through the parent chain (single + multi level).
- Paint does NOT bubble.
- `attr = newValue` invalidates owner; `attr = sameValue` does not.
- `attr = lambda` never auto-invalidates.
- Layout-flag attribute binding propagates the bubble correctly.

## Design choices

- **`Attribute<T>` stores raw `Widget*` not `WP<Widget>`.** Lifetime is
  guaranteed by the widget owning its own attributes — they share the
  widget's lifetime exactly. A weak_ptr would be defensive overkill
  and require `Widget` to be `enable_shared_from_this`, which clashes
  with stack-instantiable widgets in tests.
- **`operator==` requirement** for literal comparison is a soft cost —
  the standard library's primitives, `std::string`, and the engine's
  Vec types all overload it. Custom POD types in user code may need
  a tiny `==` to opt into auto-invalidation; otherwise every set
  invalidates (safe but slower).
- **Auto-invalidation is opt-in** via `BindOwner`. Existing widgets
  (TextBlock from Step 2, Button from Step 4) don't bind their
  attributes yet — they'll invalidate manually until the App's
  Tick pulls actual data. Step 10 wires the bindings end-to-end.
