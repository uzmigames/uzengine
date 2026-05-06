# Slate UI — Architectural Decisions (Phase 18, Step 0)

**Status**: Locked. Subsequent phases (1..10) MUST adhere to these decisions.
**Last updated**: 2026-05-06

This document records the six load-bearing decisions for the UzEngine
Slate-style retained-mode UI framework. Each decision is paired with the
alternative that was rejected, the reason, and the downstream impact on
the codebase.

The framework lives in `runtime/engine/ui/` and is consumed by the VFX
editor (Step 10) and any future runtime UI surface (Niagara-style editors,
in-game HUD, debug overlays).

---

## Decision 1 — Core language: C++17 (project std, no scripting in the core)

**Decision.** The core widget tree, layout, paint, input, invalidation,
style, and window subsystems are written in pure C++17 — the language
standard already used by the rest of the engine (`runtime/build.zig`
sets `-std=c++17`). No Lua, no scripting, no JIT inside
`runtime/engine/ui/`.

**Alternative rejected.** Bind every Widget to Lua at the core level
(Slate-via-Lua), so designers could compose UIs from script.

**Reason.**
- The runtime is shipped as a `.exe` (and a `.dll` variant for the VSCode
  extension). Embedding script bindings in the core layer pulls LuaJIT
  into every translation unit, raises compile times, and forces every
  widget API to be marshalled through `lua_State*`.
- The performance budget for the VFX editor (200+ widgets, 60 FPS) leaves
  no room for per-frame script→C++ trips on the hot path (Tick, Paint).
- A Lua wrapper layer is still possible, but it goes **on top** of the
  C++ API in a separate module (`runtime/engine/ui_lua/` if/when needed).

**Downstream impact.**
- `runtime/engine/ui/` depends only on the engine's existing C++
  foundations (`uz::Vec2`, `Color`, `UZ_LOG`, allocators, `TextRenderer`).
- No `lua.h` inclusion under `runtime/engine/ui/`.
- All public widget APIs are normal C++ types — no `LuaRef`, no
  `sol::table`, no `lua_pushXxx` shims.

---

## Decision 2 — Memory model: `std::shared_ptr<Widget>` + `std::weak_ptr`

**Decision.** Widgets are owned by `std::shared_ptr<Widget>`. Aliases
defined in `runtime/engine/ui/slate_core.h`:

```cpp
template <typename T> using SP = std::shared_ptr<T>;
template <typename T> using WP = std::weak_ptr<T>;
```

Containers store `SP<Widget>` to children; back-references (parent,
focused widget, drag-source) store `WP<Widget>`. No raw `Widget*` is
held across frames.

**Alternative rejected.** Custom intrusive refcount (`Widget : RefCounted`,
`TRefCountPtr<Widget>`).

**Reason.**
- `std::shared_ptr` integrates with the standard library, with thread-safe
  reference counting, with `make_shared` (single allocation), and with
  every existing engine pattern that already uses `std::shared_ptr`.
- The intrusive variant would require everyone to remember to call
  `AddRef`/`Release` correctly; it's a known footgun and the perf delta
  is irrelevant outside hot inner loops (which UI is not).
- `std::weak_ptr` gives us cycle breaking (parent ↔ child) and safe
  late-binding (FocusManager, DragDropManager) without a custom protocol.

**Lifetime contracts.**
- A container holds `SP<Widget>` to each child slot. Removing the slot
  releases the strong ref; the child dies if no one else holds it.
- A child holds `WP<Widget> m_parent`; never `SP<Widget> m_parent`
  (would create a cycle).
- `FocusManager`, `DragDropManager`, `TooltipManager` store `WP<Widget>`
  for any widget reference that outlives a single call frame.
- Construction is via `std::make_shared<W>()` — wrappers like `New<W>()`
  (Step 8 declarative DSL) MUST funnel through `make_shared`.

**Downstream impact.**
- Step 1 (renderer) does not own widgets — it consumes draw lists.
- Step 2 (Widget base) provides a `EnableSharedFromThis<Widget>` mixin if
  a widget needs to hand out an `SP` to itself.
- Step 4 (Input) routes events via `WP<Widget>` snapshots taken at
  hit-test time.

---

## Decision 3 — Data model: Pull via `Attribute<T>`

**Decision.** Widget properties that may change between frames are typed
as `Attribute<T>`. An `Attribute<T>` accepts either:

1. A literal `T` value (`text->Text = "Hello"`), or
2. A `std::function<T()>` getter (`text->Text = []{ return GetUserName(); }`).

The widget pulls the current value via `attr.Get()` during `Tick()` or
`PaintImpl()`. Setting a literal that differs from the previous literal
invalidates the owner widget (Paint by default). Setting a lambda does
NOT auto-invalidate — the widget compares snapshot vs current in `Tick`
and decides whether to invalidate.

Push (callback) is reserved for **events** only:
`OnClick`, `OnTextChanged`, `OnSelectionChanged`, `OnFocusReceived`,
`OnPointerDown`, `OnDrop`, etc.

**Alternative rejected.** Push-style data binding (observer pattern):
every widget property is a property bag with subscribers; setting the
property fires callbacks to interested widgets and the renderer.

**Reason.**
- Pull is dirt-simple to reason about: at any frame, every widget knows
  exactly when its data is read and can choose whether to skip work.
- Push requires a global event bus, registration/unregistration on
  widget destroy, and is much harder to debug ("who fired this event?").
- Slate (Unreal's UI) uses pull (`TAttribute<T>`) and ships at AAA scale
  — this is a battle-tested choice.
- Push handles cyclic dependency and "fire during fire" by accident; pull
  cannot.

**Downstream impact.**
- Step 2 introduces `Attribute<T>` with `BindOwner(Widget*, InvalidationFlag)`
  for invalidation-on-set semantics.
- Step 5 (invalidation) defines: lambda assignment never auto-invalidates;
  literal assignment with a different value invalidates.
- Step 7 (composite widgets) and Step 10 (VFX editor) rely on `Attribute<T>`
  lambdas to bind UI to live engine state without rebuilding the tree.
- Callbacks are exposed as plain `std::function<void(Args...)>` members,
  not signal/slot.

---

## Decision 4 — Threading: UI single-thread, draw lists immutable

**Decision.** Widget `Construct`, `Tick`, `Measure`, `Arrange`, `Paint`,
input dispatch, focus management, and tooltip/drag-drop bookkeeping all
run on **one** thread (the render thread by default).

The output of Paint is a **list of `DrawElement`** values that is
immutable from the moment Paint returns. The renderer consumes the list
on any thread (CPU command-list build, GPU submit) without locking.

**Alternative rejected.** Multi-threaded UI: layout pass on a worker
thread, paint on render thread, input on input thread.

**Reason.**
- UI workloads are tiny (hundreds of widgets, microseconds per frame).
  Multi-threading would introduce lock contention for negligible win.
- The render thread is already running for command list submission;
  reusing it removes a thread hop for input → tick → paint → submit.
- Slate ships single-threaded for the same reason. ImGui ships
  single-threaded. Avalonia ships single-threaded.
- Immutable draw lists means the renderer/compositor can safely run
  asynchronously to the next frame's tick.

**Downstream impact.**
- No mutex around widget tree access. Callers MUST not touch widgets
  from worker threads.
- `slate_renderer.h` exposes `Submit(DrawList&&)` — moves ownership.
- `Application::Tick()` is the entry point and runs on the render thread.

---

## Decision 5 — Coordinates: DPI-aware, Slate unit = virtual pixel @ 96 DPI

**Decision.** All widget sizes, paddings, positions, and font sizes are
expressed in **Slate units**, defined as virtual pixels at 96 DPI.

The platform DPI scale is queried at startup (and on monitor change) via
`Application::GetDpiScale()`. The compositor multiplies Slate units by
`kDpiScale` exactly once at submit time to produce physical pixels for
the GPU.

**Alternative rejected.** Express sizes directly in physical pixels and
let each widget multiply by DPI individually.

**Reason.**
- A single conversion point (the compositor) eliminates the entire class
  of "off by DPI" bugs that plague UI codebases.
- A widget written for 96 DPI looks correct on 144 DPI without changes.
- Designs from the Claude design (`styles.css`) use 1px = 1 virtual pixel
  — a `border: 1px solid` becomes `borderThickness = 1.0f` in Slate units.
- Slate, Avalonia, WPF, AppKit, and the web all use device-independent
  units. Pixel-baked UIs are a known regression.

**Downstream impact.**
- `Vec2`, `Rect`, `Geometry` carry Slate units. Renderer (`SlateRenderer`)
  is the only place that knows about `kDpiScale`.
- Font sizes follow: `body = 11.5f` Slate units (≈ 11.5px @ 96 DPI),
  scaled at submit.
- Hit-testing happens in Slate units — no coordinate conversion in
  `InputRouter`.

---

## Decision 6 — Text shaping: FreeType + HarfBuzz with ASCII bitmap fallback

**Decision.** Glyph rasterization uses **FreeType**. Complex script
shaping (Latin ligatures, RTL, combining marks) uses **HarfBuzz**.

Both libraries are vendored under `runtime/thirdparty/freetype/` and
`runtime/thirdparty/harfbuzz/`, integrated via `scripts/setup.ps1`.

If the third-party tree is absent, the renderer falls back to a tiny
embedded **ASCII bitmap font** (existing `BitmapFont` from the engine,
covering U+0020..U+007E) so that the engine still boots and shows
diagnostics.

**Alternative rejected.** stb_truetype only (no shaping).

**Reason.**
- `stb_truetype` rasterizes well but does not shape: ligatures (`fi`, `fl`),
  contextual alternates (`ss01`, `cv11` from the Claude design font
  feature settings), combining marks, and bidirectional text are out
  of scope.
- The Claude design explicitly uses font-feature-settings; producing
  the design correctly requires HarfBuzz.
- FreeType is the de-facto standard for engine-grade glyph rasterization
  with hinting; it integrates with HarfBuzz natively.
- Falling back to ASCII bitmap means the engine never crashes on a fresh
  clone where setup.ps1 has not run yet.

**Downstream impact.**
- Step 1 renderer accepts both FreeType-rendered glyphs and bitmap-font
  glyphs through the same `DrawElement::Text` pipeline.
- Font caching: a glyph atlas (texture) per font/size combination, built
  lazily.
- `setup.ps1` integration is a Step-1 sub-task (vendor + build flags).
- Fallback path documented; tests exercise both paths.

---

## Deferred decisions (revisit later)

The following decisions are intentionally deferred. They will be locked
when the relevant layer is mature enough to inform them.

| Decision | Defer until | Why deferred |
|---|---|---|
| Accessibility tree (UIA/AT-SPI) | After Layer 4 (input + focus) | Cannot model AX without a stable focus + role taxonomy. |
| Animation system (tweens, springs) | After Layer 5 (invalidation) | Animations require driving paint invalidation per frame; needs invalidation cost first. |
| Style hot-reload from disk | After Layer 5 + Layer 6 (style) | Reload requires file watch + `Attribute<Brush>` diff; needs both pieces. |
| Localization plumbing (`Loc("key")`) | After Layer 7 (composite widgets) | Need EditableText + ComboBox to know full string-pipeline shape. |
| Touch / multi-pointer input | After Layer 4 | Requires per-pointer-id dispatch tracking. |
| RTL layout flip | After Layer 3 | HBox/VBox must support a flipped axis flag uniformly. |

These deferrals are not "tasks left undone" — they are decisions that
should be made with more information than we currently have. Recording
them prevents them from accidentally being decided implicitly.

---

## Locking + change protocol

These six decisions are **locked** for phase 18. Any subsequent task that
needs to change a decision MUST:

1. Open a follow-up task in the rulebook describing the change and impact.
2. Document the new decision here with `Status: superseded by <new>`.
3. Update every dependent step's spec accordingly.

This prevents the framework from drifting under pressure as later steps
discover constraints.
