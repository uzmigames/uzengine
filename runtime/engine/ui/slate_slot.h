/// UzEngine Slate UI — slot descriptor for container widgets.
///
/// Phase 18 / Step 3. A slot is the per-child layout configuration in
/// HBox/VBox/Overlay (and any future container). It describes:
///
///   - which child widget occupies the slot (`SP<Widget> widget`)
///   - how much space it should claim (`sizeRule` + `fillFactor`)
///   - how it should align inside the allocated rect (`hAlign`, `vAlign`)
///   - inset padding (`padding` — left, top, right, bottom in Slate units).
///
/// All four chained setters (`HAlign`, `VAlign`, `Padding`, `FillWidth`,
/// `FillHeight`, `FixedSize`) return `*this` for fluent declaration:
///
/// ```cpp
/// hbox->AddSlot(textBlock).HAlign(HAlign::Right).Padding(Vec4{8,4,8,4});
/// ```
///
/// `SizeRule::Auto` makes the slot claim `Measure(maxChildArea).x`
/// (or `.y` for VBox); `SizeRule::Fill` makes it expand to consume the
/// remaining axis space, distributed pro-rata by `fillFactor` among all
/// Fill siblings (default 1.0 = equal share).

#pragma once

#include "engine/ui/slate_core.h"

#include <cstdint>

namespace uz::ui {

class Widget;

enum class HAlign : std::uint8_t { Left, Center, Right, Fill };
enum class VAlign : std::uint8_t { Top,  Center, Bottom, Fill };

enum class SizeRule : std::uint8_t {
    Auto,  ///< Claim child's Measure() result on the axis.
    Fill,  ///< Claim a share of remaining axis space (fillFactor).
};

struct SlotBase {
    SP<Widget> widget;
    Vec4       padding{0.0f, 0.0f, 0.0f, 0.0f}; ///< L, T, R, B in Slate units.
    HAlign     hAlign{HAlign::Fill};
    VAlign     vAlign{VAlign::Fill};
    SizeRule   sizeRule{SizeRule::Auto};
    float      fillFactor{1.0f};

    SlotBase& HAlign_(HAlign a)     { hAlign = a; return *this; }
    SlotBase& VAlign_(VAlign a)     { vAlign = a; return *this; }
    SlotBase& Padding_(Vec4 p)      { padding = p; return *this; }
    SlotBase& Padding_(float all)   { padding = Vec4{all, all, all, all}; return *this; }

    /// Sets the slot to grow as `factor` of the remaining axis space.
    SlotBase& FillSize(float factor = 1.0f) {
        sizeRule = SizeRule::Fill;
        fillFactor = factor;
        return *this;
    }

    /// Sets the slot back to Auto sizing.
    SlotBase& AutoSize() {
        sizeRule = SizeRule::Auto;
        return *this;
    }

    [[nodiscard]] float PaddingX() const noexcept { return padding.x + padding.z; }
    [[nodiscard]] float PaddingY() const noexcept { return padding.y + padding.w; }
};

} // namespace uz::ui
