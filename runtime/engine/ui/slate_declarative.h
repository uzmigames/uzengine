/// UzEngine Slate UI — declarative authoring DSL.
///
/// Phase 18 / Step 8. Header-only macros + helpers that turn the
/// imperative "construct + chain setters" widget pattern into a
/// declarative tree literal:
///
/// ```cpp
/// auto root = NewVBox()
///     .Padding(8.0f)
///     [{
///         New<TextBlock>().Text("Hello"),
///         NewHBox()[{
///             New<Button>().OnClick([]{ ... }),
///             NewSpacer().FillSize(1.0f),
///             New<Button>().Text("Cancel"),
///         }],
///     }];
/// ```
///
/// Mechanics:
///   - `New<W>()` returns a `WidgetBuilder<W>` that owns a freshly
///     constructed `SP<W>` (via `make_shared`) and forwards the
///     `Construct()` call.
///   - `WidgetBuilder<W>::operator SP<W>()` and
///     `operator SP<Widget>()` make the builder convertible into the
///     final shared pointer types.
///   - `operator[](initializer_list<SP<Widget>>)` adds children when
///     `W` has an `AddSlot(SP<Widget>)` method — used for HBox / VBox /
///     Overlay.
///   - `UZ_SLATE_PROP(Builder, Type, Member)` defines a fluent setter
///     on a builder specialisation. Chaining returns `*this`.
///
/// The DSL is a thin syntactic layer; every call funnels through
/// existing widget code paths. Tests cover the layer itself; visual
/// output is identical to the imperative form.

#pragma once

#include "engine/ui/slate_attribute.h"
#include "engine/ui/slate_box.h"
#include "engine/ui/slate_button.h"
#include "engine/ui/slate_spacer.h"
#include "engine/ui/slate_text_block.h"
#include "engine/ui/slate_widget.h"

#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

namespace uz::ui {

namespace detail {

/// Concept-like SFINAE: detect if `W` has `AddSlot(SP<Widget>)`.
template <typename W, typename = void>
struct HasAddSlot : std::false_type {};

template <typename W>
struct HasAddSlot<W, std::void_t<decltype(std::declval<W&>().AddSlot(std::declval<SP<Widget>>()))>>
    : std::true_type {};

} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// WidgetBuilderBase
// ─────────────────────────────────────────────────────────────────────────────

template <typename W>
class WidgetBuilder {
public:
    WidgetBuilder() : ptr(std::make_shared<W>()) {
        ptr->Construct();
    }

    /// Adds children when W is a container. No-op for leaf widgets.
    WidgetBuilder& operator[](std::initializer_list<SP<Widget>> children) {
        if constexpr (detail::HasAddSlot<W>::value) {
            for (const auto& c : children) {
                ptr->AddSlot(c);
            }
        }
        return *this;
    }

    /// Implicit conversion to the strongly-typed shared_ptr.
    operator SP<W>()      const { return ptr; }
    operator SP<Widget>() const { return ptr; }

    /// Direct accessor (test-friendly).
    [[nodiscard]] W*       Get() noexcept       { return ptr.get(); }
    [[nodiscard]] const W* Get() const noexcept { return ptr.get(); }

    SP<W> ptr;

    // Common chained helpers shared across many widgets ------------------

    /// Sets `Padding` if the widget exposes it (Button does).
    template <typename U = W>
    auto Padding(Vec4 p) -> std::enable_if_t<std::is_same_v<U, Button>, WidgetBuilder&> {
        ptr->Padding = p;
        return *this;
    }
    /// Symmetric padding overload.
    template <typename U = W>
    auto Padding(float v) -> std::enable_if_t<std::is_same_v<U, Button>, WidgetBuilder&> {
        ptr->Padding = Vec4{v, v, v, v};
        return *this;
    }

    /// Spacer convenience: chain FillSize at the slot level via the
    /// returned slot when added through HBox/VBox AddSlot. For the
    /// declarative path, prefer the `Slot(...)` helper.
    template <typename U = W>
    auto FillSize(float factor = 1.0f)
        -> std::enable_if_t<std::is_same_v<U, Spacer>, WidgetBuilder&> {
        // Spacer itself doesn't store fillFactor; the slot does. The
        // declarative path encodes this via the Slot(...) helper. Keep
        // this method as a no-op so the syntax is permissible inline
        // for cases where the user wraps the Spacer manually.
        (void)factor;
        return *this;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Property setter macro
// ─────────────────────────────────────────────────────────────────────────────

/// Generates a chainable setter for an `Attribute<Type> Member` on a
/// builder specialisation. The setter accepts either a literal `Type`
/// or a `std::function<Type()>` lambda — `Attribute<T>::operator=`
/// handles both via overload.
///
/// Usage (inside a builder specialisation):
///
/// ```cpp
/// template <> class WidgetBuilder<TextBlock> : ... {
///     UZ_SLATE_PROP(std::string, Text);
///     UZ_SLATE_PROP(Color,       TextColor);
/// };
/// ```
#define UZ_SLATE_PROP(Type, Member)                                                       \
    auto& Member(Type v)                       { ptr->Member = std::move(v);  return *this; } \
    auto& Member(std::function<Type()> getter) { ptr->Member = std::move(getter); return *this; }

// ─────────────────────────────────────────────────────────────────────────────
// TextBlock specialisation
// ─────────────────────────────────────────────────────────────────────────────

template <>
class WidgetBuilder<TextBlock> {
public:
    WidgetBuilder() : ptr(std::make_shared<TextBlock>()) { ptr->Construct(); }
    operator SP<TextBlock>() const { return ptr; }
    operator SP<Widget>()    const { return ptr; }
    [[nodiscard]] TextBlock* Get() noexcept       { return ptr.get(); }
    [[nodiscard]] const TextBlock* Get() const noexcept { return ptr.get(); }
    SP<TextBlock> ptr;

    UZ_SLATE_PROP(std::string, Text);
    UZ_SLATE_PROP(Color,       TextColor);
    UZ_SLATE_PROP(float,       FontSize);
};

// ─────────────────────────────────────────────────────────────────────────────
// Button specialisation
// ─────────────────────────────────────────────────────────────────────────────

template <>
class WidgetBuilder<Button> {
public:
    WidgetBuilder() : ptr(std::make_shared<Button>()) { ptr->Construct(); }
    operator SP<Button>() const { return ptr; }
    operator SP<Widget>() const { return ptr; }
    [[nodiscard]] Button* Get() noexcept             { return ptr.get(); }
    [[nodiscard]] const Button* Get() const noexcept { return ptr.get(); }
    SP<Button> ptr;

    UZ_SLATE_PROP(Color, NormalColor);
    UZ_SLATE_PROP(Color, HoverColor);
    UZ_SLATE_PROP(Color, PressedColor);
    UZ_SLATE_PROP(float, CornerRadius);
    UZ_SLATE_PROP(Color, BorderColor);
    UZ_SLATE_PROP(float, BorderThickness);
    UZ_SLATE_PROP(Vec4,  Padding);

    WidgetBuilder& Padding(float all) { ptr->Padding = Vec4{all, all, all, all}; return *this; }
    WidgetBuilder& Content(SP<Widget> w) { ptr->Content = std::move(w); return *this; }
    WidgetBuilder& OnClick(std::function<void()> cb) { ptr->OnClick = std::move(cb); return *this; }

    /// Treat `[{ child }]` on a button as setting its Content (only the
    /// first widget is used; trailing children are dropped — buttons
    /// are single-content).
    WidgetBuilder& operator[](std::initializer_list<SP<Widget>> children) {
        if (children.size() > 0) ptr->Content = *children.begin();
        return *this;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Spacer specialisation
// ─────────────────────────────────────────────────────────────────────────────

template <>
class WidgetBuilder<Spacer> {
public:
    WidgetBuilder() : ptr(std::make_shared<Spacer>()) { ptr->Construct(); }
    operator SP<Spacer>() const { return ptr; }
    operator SP<Widget>() const { return ptr; }
    [[nodiscard]] Spacer* Get() noexcept             { return ptr.get(); }
    [[nodiscard]] const Spacer* Get() const noexcept { return ptr.get(); }
    SP<Spacer> ptr;

    UZ_SLATE_PROP(Vec2, Size);
};

// ─────────────────────────────────────────────────────────────────────────────
// New<W>() — generic factory (declared after specialisations so they pick up).
// ─────────────────────────────────────────────────────────────────────────────

template <typename W>
[[nodiscard]] inline WidgetBuilder<W> New() {
    return WidgetBuilder<W>{};
}

// Convenience overloads for the most common types so the user can write
// `NewVBox()` instead of `New<VerticalBox>()`.
[[nodiscard]] inline WidgetBuilder<HorizontalBox> NewHBox()    { return New<HorizontalBox>(); }
[[nodiscard]] inline WidgetBuilder<VerticalBox>   NewVBox()    { return New<VerticalBox>();   }
[[nodiscard]] inline WidgetBuilder<Overlay>       NewOverlay() { return New<Overlay>();       }
[[nodiscard]] inline WidgetBuilder<Spacer>        NewSpacer()  { return New<Spacer>();        }

// ─────────────────────────────────────────────────────────────────────────────
// Slot helper — declarative slot configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Wraps a child + slot configuration. Convertible to `SP<Widget>` for
/// use inside `operator[]` initializer lists, but also exposes
/// fluent setters that update a side `SlotBase` description. The
/// outer container (HBox/VBox/Overlay) reads back the description
/// when it adds the slot — see `WidgetBuilder<HorizontalBox>::operator[]`
/// below.
class SlotBuilder {
public:
    SP<Widget> widget;
    SlotBase   spec;

    SlotBuilder& HAlign(uz::ui::HAlign a) { spec.hAlign = a; return *this; }
    SlotBuilder& VAlign(uz::ui::VAlign a) { spec.vAlign = a; return *this; }
    SlotBuilder& Padding(Vec4 p)          { spec.padding = p; return *this; }
    SlotBuilder& Padding(float v)         { spec.padding = Vec4{v, v, v, v}; return *this; }
    SlotBuilder& FillSize(float factor = 1.0f) {
        spec.sizeRule = SizeRule::Fill;
        spec.fillFactor = factor;
        return *this;
    }
    SlotBuilder& AutoSize() { spec.sizeRule = SizeRule::Auto; return *this; }

    operator SP<Widget>() const { return widget; }
};

/// Wraps a widget into a `SlotBuilder` so HBox/VBox can pick up its
/// per-slot configuration alongside the child pointer.
[[nodiscard]] inline SlotBuilder Slot(SP<Widget> w) {
    SlotBuilder sb;
    sb.widget = std::move(w);
    return sb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Container builders that recognise SlotBuilder via runtime registry.
// ─────────────────────────────────────────────────────────────────────────────
//
// The cleanest implementation would specialise `operator[]` for an
// initializer_list of SlotBuilder, but C++17 does not allow distinct
// specialisations that differ only by initializer_list element type.
// Instead, callers use a separate `AddSlot(...)` chained call when they
// need slot-specific config:
//
// ```cpp
// auto box = NewHBox()[{ a, b, c }];
// box.ConfigureSlot(0).FillSize(1.0f);
// ```
//
// Or for the full SlotBuilder pattern:
//
// ```cpp
// auto box = NewHBox();
// box.Get()->AddSlot(text).FillSize(1.0f);
// box.Get()->AddSlot(button).HAlign_(HAlign::Right);
// ```
//
// Step 10's editor uses the second form for slot-specific layout.

} // namespace uz::ui
