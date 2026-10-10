/// UzEngine Slate UI — Spacer widget.
///
/// Phase 18 / Step 3. A widget that takes space but draws nothing.
/// Useful as a flexible gap inside HBox/VBox layouts.
///
/// Two common patterns:
///   - Fixed gap:    `vbox->AddSlot(std::make_shared<Spacer>(Vec2{0, 12}))`
///                   → 12px tall horizontal rule.
///   - Flexible gap: `hbox->AddSlot(std::make_shared<Spacer>()).FillSize(1.0f)`
///                   → consumes remaining axis space, pushing siblings apart.

#pragma once

#include "engine/ui/slate_attribute.h"
#include "engine/ui/slate_widget.h"

namespace uz::ui {

class Spacer : public Widget {
public:
    Spacer() = default;
    explicit Spacer(Vec2 size) { Size = size; }

    /// Desired size in Slate units. Default zero — pair with a Fill slot
    /// rule for "take whatever's left" behaviour.
    Attribute<Vec2> Size{Vec2{0.0f, 0.0f}};

    [[nodiscard]] Vec2 Measure(Vec2 maxSize) override {
        Vec2 d = Size.Get();
        if (maxSize.x > 0.0f && d.x > maxSize.x) d.x = maxSize.x;
        if (maxSize.y > 0.0f && d.y > maxSize.y) d.y = maxSize.y;
        return d;
    }

    void Paint(SlateRenderer& /*renderer*/) override {
        // Intentional no-op.
    }
};

} // namespace uz::ui
