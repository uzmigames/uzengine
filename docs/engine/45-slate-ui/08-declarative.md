# Slate UI — Declarative Layer (Phase 18, Step 8)

**Status**: Complete. The DSL is header-only and compiles 1:1 to the
existing imperative widget API.

## Files

- `runtime/engine/ui/slate_declarative.h` — `New<W>()`, `NewHBox/VBox/Overlay/Spacer`, `WidgetBuilder<W>`, `Slot()`, `UZ_SLATE_PROP`.
- `runtime/tests/engine/ui/test_slate_declarative.cpp` — 11 unit tests.

## Goal

Construct a widget tree with the same shape as Unreal's Slate `SNew`
syntax, without macro magic that hides types:

```cpp
auto root = NewVBox()
    [{
        New<TextBlock>().Text("Header"),
        NewHBox()
            [{
                New<Button>().Padding(8.0f).OnClick([]{ ... }),
                NewSpacer(),
                New<TextBlock>().Text("Right"),
            }],
    }];
SP<VerticalBox> r = root;
```

The braces inside `[{ ... }]` are an `initializer_list<SP<Widget>>` —
each element is implicitly converted from a `WidgetBuilder<W>` via
`operator SP<Widget>()`.

## How it works

```cpp
template <typename W>
class WidgetBuilder {
public:
    WidgetBuilder() : ptr(std::make_shared<W>()) { ptr->Construct(); }

    WidgetBuilder& operator[](std::initializer_list<SP<Widget>> children) {
        if constexpr (HasAddSlot<W>) for (auto& c : children) ptr->AddSlot(c);
        return *this;
    }

    operator SP<W>()      const { return ptr; }
    operator SP<Widget>() const { return ptr; }

    SP<W> ptr;
};

template <typename W>
inline WidgetBuilder<W> New() { return {}; }

inline WidgetBuilder<HorizontalBox> NewHBox() { return New<HorizontalBox>(); }
inline WidgetBuilder<VerticalBox>   NewVBox() { return New<VerticalBox>();   }
inline WidgetBuilder<Overlay>       NewOverlay() { return New<Overlay>();   }
inline WidgetBuilder<Spacer>        NewSpacer()  { return New<Spacer>();    }
```

`HasAddSlot<W>` is a `void_t` SFINAE helper that detects whether `W`
has an `AddSlot(SP<Widget>)` member. If yes, `operator[]` adds children;
if not (e.g. for `TextBlock`), it's a compile-time no-op.

## UZ_SLATE_PROP — chainable setter macro

```cpp
#define UZ_SLATE_PROP(Type, Member) \
    auto& Member(Type v)                  { ptr->Member = std::move(v); return *this; } \
    auto& Member(std::function<Type()> g) { ptr->Member = std::move(g); return *this; }
```

Each `Attribute<T>` member of a widget is exposed as a chainable setter
on its `WidgetBuilder<W>` specialisation. Both literal and lambda
overloads are generated, mirroring `Attribute<T>::operator=`.

## Specialisations

`TextBlock`, `Button`, and `Spacer` have explicit `WidgetBuilder<W>`
specialisations that call `UZ_SLATE_PROP` for each public attribute:

| Builder | Setters |
|---|---|
| `WidgetBuilder<TextBlock>` | `Text`, `TextColor`, `FontSize` |
| `WidgetBuilder<Button>` | `NormalColor`, `HoverColor`, `PressedColor`, `CornerRadius`, `BorderColor`, `BorderThickness`, `Padding(Vec4|float)`, `Content(SP<Widget>)`, `OnClick(callback)` |
| `WidgetBuilder<Spacer>` | `Size` |

Other widgets (HBox, VBox, Overlay, ScrollBox, EditableText, ComboBox)
use the primary `WidgetBuilder<W>` template; users set attributes via
`builder.Get()->Attr = value` when no specialisation is needed yet.
Step 10 adds specialisations as the editor exercises each widget.

## Slot helper

```cpp
SlotBuilder Slot(SP<Widget> w);   // wraps + exposes spec
sb.HAlign(HAlign::Right).VAlign(VAlign::Center).Padding(4.0f).FillSize(2.0f);
sb.AutoSize();
```

The `SlotBuilder` carries a `SlotBase` description alongside the child
pointer. It's convertible to `SP<Widget>` so it can sit inside a
container's `[{ ... }]` list. The full per-slot config wiring with
container builders is finished in Step 10 — for now, when slot-specific
settings are needed, callers use the imperative form:

```cpp
auto box = NewHBox();
box.Get()->AddSlot(text).FillSize(1.0f);
box.Get()->AddSlot(btn).HAlign_(HAlign::Right).Padding_(Vec4{8,4,8,4});
```

## Test coverage

Slate suite is now **98/98** passing in 1 ms total:

| Suite             | New tests | Description |
|-------------------|-----------|-------------|
| SlateDeclarative  | 11        | New<W>() returns built widget; chained setters; lambda prop; operator[] populates slots; NewHBox/VBox alias; Button content via [{}]; Button OnClick; Slot helper default + fluent setters; implicit conversion to base; nested tree structure. |

## Visualization

DSL parity vs imperative form is observable from the tests (every DSL
call funnels through the same widget code paths used in Steps 1-7).
The imperative-vs-DSL line-count comparison and visual side-by-side is
folded into Step 10's editor — the editor is the primary consumer and
its commit message will document the LOC delta.

## Design choices

- **No macro magic.** `New<W>()` and `UZ_SLATE_PROP` are the only DSL
  primitives. They expand to ordinary inline functions and templates.
  Compile errors point at the user's source, not at macro expansions.
- **Specialisations for hot widgets, primary template otherwise.**
  Widgets that get used a lot (TextBlock / Button / Spacer) have
  curated builders. Less-common widgets fall through to the primary
  template; the user calls `builder.Get()->Member = ...` until
  someone adds a specialisation.
- **Implicit conversion to `SP<W>` AND `SP<Widget>`.** Container
  initializer lists need `SP<Widget>`; downstream code that wants the
  concrete type uses `SP<W>`. Both conversions are const-correct.
- **`Spacer::FillSize` is a stub.** Spacer's fill behaviour belongs to
  the slot, not the widget. The stub exists so the chain syntax
  doesn't require a separate `Slot(NewSpacer()).FillSize(1)` wrap when
  the user just wants any Spacer child.
- **Nested `[{ }]` works because `WidgetBuilder<HBox>` is implicitly
  convertible to `SP<Widget>`.** No special handling needed for
  arbitrary nesting depth.
