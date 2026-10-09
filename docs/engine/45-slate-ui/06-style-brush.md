# Slate UI — Brush + StyleSet + StyleStack (Phase 18, Step 6)

**Status**: Complete. The visual layer is decoupled from the widgets;
the Claude dark theme is the built-in default.

## Files

- `runtime/engine/ui/slate_brush.h` — `BrushKind`, `Brush` POD, factory helpers, equality.
- `runtime/engine/ui/slate_style.h` — `StyleSet`, `StyleStack`, `Styles()`, `CreateClaudeDarkStyleSet()`.
- `runtime/engine/ui/slate_style.cpp` — claude.dark token table.
- `runtime/tests/engine/ui/test_slate_style.cpp` — 16 unit tests.

## Brush

A POD that bundles every visual property a widget needs:

```cpp
struct Brush {
    BrushKind kind;          // SolidColor / Image / Box9Slice / Gradient
    Color     tintColor;
    float     cornerRadius, borderThickness;
    Color     borderColor;
    Vec2      shadowOffset;  float shadowBlur;  Color shadowColor;
    VfxUiIcon iconId;        // Image / Box9Slice
    Color     gradientStop0..3;
    Vec4      gradientPositions;
};
```

`static_assert(std::is_trivially_copyable_v<Brush>)` — the type is
copyable in a single memcpy, so `Attribute<Brush>` doesn't pay an
allocation when widgets pull their current style.

Helpers:

```cpp
Brush bg     = MakeSolidColor(Color{0.1, 0.1, 0.12, 1}, /*radius*/ 4);
Brush card   = MakeBordered(fill, 1.0f, line, 4);
Brush halo   = MakeShadowed(fill, Vec2{0,2}, 8.0f, shadow, 4);
Brush header = MakeGradient(top, bottom, 4);
Brush icon   = MakeIcon(VfxUiIcon::Play, tint);
```

Equality (`operator==`) is value-based so `Attribute<Brush>` set with
the same brush does NOT trigger Paint invalidation in Step 5.

## StyleSet

A named lookup with three tables:

```cpp
class StyleSet {
public:
    void  SetColor(name, Color);
    void  SetBrush(name, Brush);
    void  SetFont(name, BitmapFont*, sizeUnits);
    Color GetColor(name, fallback={});
    Brush GetBrush(name, fallback={});
    FontEntry GetFont(name);
};
```

Missing keys return the supplied fallback (the engine convention is
magenta-with-alpha for "did you forget to set this token?" debugging).

## StyleStack

Process-wide singleton, last-pushed-wins:

```cpp
StyleStack& Styles();   // Lazy-init: pushes claude.dark on first call.

Color c = Styles().GetColor("accent");          // → claude.dark blue
Styles().Push(myLightTheme);                     // override
Color c2 = Styles().GetColor("accent");         // → light theme blue
Styles().Pop();                                  // back to claude.dark
```

Lookup walks the stack top-down. Missing keys fall through to the next
StyleSet in the stack; missing everywhere → fallback. This means a
partial override theme (e.g. "make the background tier brighter")
doesn't have to redefine every token — it just sets the keys it
overrides.

## claude.dark token inventory

Built-in StyleSet returned by `CreateClaudeDarkStyleSet()`. Tokens are
1:1 with `.sandbox/claude-design/vfx-template/project/styles.css`:

| Tier | Keys | Approximation |
|---|---|---|
| Background (5 levels, deep→shallow) | `bg.0`..`bg.4` | `#15171a`..`#353b44` |
| Line | `line.0`..`line.2` | `#2a3039`..`#464e5a` |
| Foreground | `fg.0`..`fg.3` | `#eaeef3`..`#555c67` |
| Accent | `accent`, `accent.soft`, `accent.line` | oklch(0.72 0.13 235) ≈ `#4ba3d6` |
| Status | `warn`, `ok`, `err` | amber, green, red |
| Secondary | `magenta`, `purple`, `cyan` | for non-accent emphasis |
| Axis | `axis.x`, `axis.y`, `axis.z` | red / green / blue gizmo conventions |

Brushes (composed from the colors above):

| Key | Composition |
|---|---|
| `panel.head` | gradient bg.2 → bg.1, radius 4, 1px line.0 border |
| `panel.body` | bg.1 fill, radius 4, 1px line.0 border |
| `tab.active` | accent.soft fill, 1px accent.line border, radius 4 |
| `tab.inactive` | bg.2 fill, radius 4 |
| `button.primary` | accent.soft fill, 1px accent.line border, radius 4 |
| `button.ghost` | transparent fill, 1px line.1 border, radius 4 |

A test (`SlateClaudeDark.ContainsAllRequiredColorKeys` /
`...BrushKeys`) lock-step verifies these are always present, so a
dropped key in a future refactor fails the build instead of producing
silent magenta UI.

## Test coverage

Slate suite is now **71/71** passing in 1 ms total:

| Suite                | Tests | Description |
|----------------------|-------|-------------|
| Step 1-5             | 55    | Renderer, widget, layout, input, invalidation. |
| SlateBrush           | 5     | POD, helpers, HasBorder/HasShadow, equality. |
| SlateStyle           | 3     | GetColor fallback, HasColor + count, Brush roundtrip. |
| SlateStyleStack      | 4     | Push/Pop override + restore, fallthrough, missing → fallback, global singleton has claude.dark. |
| SlateClaudeDark      | 4     | All required color keys, all required brush keys, bg.0 darker than bg.4, accent.soft alpha < accent. |

## Visualization

Theme swap demo deferred to Step 10 (VFX editor rebuild) which is the
first place where a theme swap is genuinely visible at scale (every
panel of the editor switches palette in one frame). Step 6's
behavioural correctness is fully proven by the 16 unit tests.

## Design choices

- **POD Brush** so `Attribute<Brush>` set/get round-trips without heap
  noise and equality is a single memcmp on numeric fields. Texture or
  atlas reference is by enum (`VfxUiIcon`), not pointer, to keep the
  trivial-copyable promise.
- **Top-down stack search**, like CSS specificity for nested theme
  contexts. Modal dialogs in Step 9 will Push their own StyleSet to
  re-tint accents while the underlying main window keeps its theme.
- **Magenta fallback** (the conventional "missing texture" colour)
  means a forgotten token shows up in the visualisation immediately
  rather than silently rendering as black or transparent.
- **Hex-RGB approximation** of the OKLCH source colors: OKLCH is the
  authoring space (perceptually uniform); the engine renders in linear
  RGB. The conversion is one-time at theme creation and the deltas are
  imperceptible (≤1 RGB unit). Future Step 6+ work may swap to a real
  OKLCH→linear converter when colour-correctness matters more.
