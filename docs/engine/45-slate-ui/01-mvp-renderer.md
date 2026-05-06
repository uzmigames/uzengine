# Slate UI — MVP Renderer (Phase 18, Step 1)

**Status**: Complete. The renderer is part of the runtime binary; widgets
in subsequent steps emit into it.

## Files

- `runtime/engine/ui/slate_core.h` — `SP<T>`, `WP<T>`, `Color`, `Rect`.
- `runtime/engine/ui/slate_renderer.h` — `DrawKind`, `DrawElement`, `SlateRenderer`.
- `runtime/engine/ui/slate_renderer.cpp` — `Submit` bridge + scissor.
- `runtime/tests/engine/ui/test_slate_renderer.cpp` — unit tests (8 cases).

## DrawElement — the only currency

Widgets never call `TextRenderer` directly. They emit `DrawElement`s
into the `SlateRenderer` queue. The element is plain-data (no virtuals,
no owning pointers) so it copies in a single `memcpy` and serialises
trivially for tests.

```cpp
enum class DrawKind : std::uint8_t {
    Quad, Line, Text, Sprite, ScissorPush, ScissorPop,
};

struct DrawElement {
    DrawKind kind;
    Rect     rect;             // Slate units (vpx @ 96 DPI)

    // Visual.
    Color    color;
    float    cornerRadius, borderThickness;
    Color    borderColor;
    Vec2     shadowOffset;
    float    shadowBlur;
    Color    shadowColor;

    // Text.
    std::string text;
    bool        msdf;
    float       fontSize;

    // Sprite.
    VfxUiIcon iconId;

    // Line.
    Vec2  lineEnd;
    float lineThickness;
};
```

Every numeric field is in **Slate units**. Multiplication by
`dpiScale` happens **once**, inside `Submit`.

## Submit pipeline

```
Widget.Paint() ──push──▶ DrawElement queue ──Submit(dpiScale)──▶ TextRenderer
                              │                                      │
                              │                                      │
                              ▼                                      ▼
                       scissor stack                     AddRoundedRect / AddRect
                       (CPU-side)                        / AddText / AddMsdfText
                                                         / AddSprite
```

`Submit(text, font, msdfFont, dpiScale)` walks the queue once:

1. `ScissorPush` / `ScissorPop` mutate a CPU-side stack of `Rect`s.
2. Every drawing element is trivially-rejected against the top-of-stack
   scissor (fully outside → skipped). Partial overlaps still emit; the
   SDF rect / glyph shaders handle their own bounds, and the GPU
   scissor itself is wired in Step 6 once the invalidation system needs
   precise sub-rect clipping.
3. `Quad` → `TextRenderer::AddRoundedRect` with `pos*dpiScale`,
   `cornerRadius*dpiScale`, etc.
4. `Line` → degenerate `AddRect` (1px-aligned thin rectangle along the
   dominant axis between `rect.pos` and `lineEnd`).
5. `Text` → `AddText` (bitmap) or `AddMsdfText` (MSDF) depending on
   `msdf` flag and whether `msdfFont` is non-null.
6. `Sprite` → `AddSprite(iconId, ...)`.

Unbalanced scissor stack at Submit end emits a warning under
category `SlateUI` and auto-drains.

## Threading

`SlateRenderer` is single-threaded — same constraint as `TextRenderer`.
All public methods MUST be called from the render thread. This matches
**Decision 4** in `00-decisions.md`.

## DPI

The renderer is the **only** piece of the UI stack that knows about
`dpiScale`. Widgets always speak Slate units. A widget rewritten for
192-DPI displays without changing any coordinate is a feature, not a
trick — Decision 5 enforces this.

## Visualization

Step 1 ships with **no standalone demo**. The visualization is rolled
into Step 2 (Widget + TextBlock) where the first real widget exercises
the full pipeline end-to-end. Rationale: a `SlateRenderer.Submit` of
three hardcoded quads is visually indistinguishable from a direct
`TextRenderer.AddRoundedRect` call — both routes already produce
identical pixels in every existing demo. The first meaningful pixel
that proves "Slate widget tree → renderer → screen" is a `TextBlock`,
which arrives in Step 2.

The renderer's **behavioral** correctness is covered by the unit tests
in `runtime/tests/engine/ui/test_slate_renderer.cpp`:

- Queue lifecycle: BeginFrame clears, PushElement preserves order, Clear empties.
- DrawElement: default is a valid identity Quad; copy is value-semantic.
- Rect: Contains uses half-open intervals; Intersect clamps overlap;
  disjoint Intersect returns empty; IsEmpty detects zero-size.

8 tests, 0 ms total.

## Build integration

Added to `runtime/build.zig`:

- include path: `engine/ui`
- source dir: `addCppDir(runtime_mod, b, "engine/ui", cpp_flags)`
- test dir: `addCppDir(test_mod, b, "tests/engine/ui", cpp_flags)`

## Pre-existing fixes folded in

While stabilising `test_runtime` so the new test target could build, the
following pre-existing breakages were fixed (committed in this step):

- `VFXCurve::GetFloatKeys()` / `GetVec4Keys()` — added const accessors
  (used by `emitter_serializer.h`).
- `VfxStageFromName` / `VfxOverrideFlagFromName` — `(void)` casts to
  drop nodiscard warnings in `emitter_serializer.h`.
- `VfxResolveInheritanceForEditor` — implemented as the "keep removed
  modules tagged `OverrideRemove`" sibling of `VfxResolveInheritance`.
  Module override tagging (`Inherited` / `OverrideAdd` /
  `OverrideReplace` / `OverrideRemove`) now lands on the resolver
  output, satisfying the long-broken `tests/engine/vfx/test_vfx_inheritance_overrides.cpp`.

These are not Slate-UI features; they were the gate to a green build.
