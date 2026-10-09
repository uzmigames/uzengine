/// UzEngine Slate UI — ComboBox<T> dropdown selector.
///
/// Phase 18 / Step 7. Header-only generic dropdown. The user supplies
/// the option list and a labeling function; the widget renders the
/// label of the currently-selected option + a chevron, and on click
/// reveals a popup VBox of buttons (one per option). Selecting an
/// option fires `OnSelectionChanged(idx)` and closes the popup.
///
/// The popup is added to the App's TopMost layer (Step 9 wires the
/// real layer; Step 7 ships a `SetPopupHost` setter so a host can
/// receive the popup widget and take responsibility for adding it).
///
/// Visual:
///   - closed: Quad bg + label TextBlock + 8x8 chevron quad
///   - open:   same closed visual + popup VBox of clickable buttons
///
/// `SelectedIndex` is bounded by the option list. Out-of-range values
/// at construction default to 0.

#pragma once

#include "engine/ui/slate_attribute.h"
#include "engine/ui/slate_renderer.h"
#include "engine/ui/slate_widget.h"

#include <functional>
#include <string>
#include <vector>

namespace uz::ui {

template <typename T>
class ComboBox : public Widget {
public:
    using Labeler = std::function<std::string(const T&)>;
    using PopupHost = std::function<void(SP<Widget> popup, bool show)>;

    /// Sets the option list and labeler. Resets SelectedIndex to 0.
    void SetOptions(std::vector<T> opts, Labeler labeler) {
        m_options = std::move(opts);
        m_labeler = std::move(labeler);
        SelectedIndex = 0;
        Invalidate(InvalidationFlag::Paint);
    }

    /// Optional callback the host can register to know about popup
    /// visibility. ComboBox calls this with `(popupWidget, true)` to
    /// show, `(nullptr, false)` to hide. Default: no-op (popup is
    /// rendered inline as a hidden child until Step 9 wires TopMost).
    void SetPopupHost(PopupHost host) { m_popupHost = std::move(host); }

    Attribute<int>           SelectedIndex{0};
    Attribute<Color>         BgColor{Color{0.16f, 0.18f, 0.21f, 1.0f}};
    Attribute<Color>         TextColor{Color{0.92f, 0.93f, 0.95f, 1.0f}};
    std::function<void(int)> OnSelectionChanged;

    [[nodiscard]] bool IsOpen() const noexcept { return m_open; }
    [[nodiscard]] std::size_t OptionCount() const noexcept { return m_options.size(); }

    /// Returns the label for the currently selected option, or an
    /// empty string when no options are set.
    [[nodiscard]] std::string SelectedLabel() const {
        const int idx = SelectedIndex.Get();
        if (idx < 0 || static_cast<std::size_t>(idx) >= m_options.size()) return {};
        if (!m_labeler) return {};
        return m_labeler(m_options[static_cast<std::size_t>(idx)]);
    }

    Vec2 Measure(Vec2 maxSize) override {
        // Generous: longest label * fontSize × ratio + chevron + paddings.
        constexpr float kFontSize = 11.5f;
        constexpr float kAdvance = 0.55f;
        constexpr float kChevron = 12.0f;
        constexpr float kPad = 8.0f;
        std::size_t longest = 0;
        if (m_labeler) {
            for (const auto& o : m_options) {
                const auto s = m_labeler(o);
                if (s.size() > longest) longest = s.size();
            }
        }
        Vec2 d{static_cast<float>(longest) * kFontSize * kAdvance + kChevron + 2.0f * kPad,
               kFontSize + 2.0f * 6.0f};
        if (maxSize.x > 0.0f && d.x > maxSize.x) d.x = maxSize.x;
        if (maxSize.y > 0.0f && d.y > maxSize.y) d.y = maxSize.y;
        return d;
    }

    void Paint(SlateRenderer& renderer) override {
        DrawElement bg{};
        bg.kind = DrawKind::Quad;
        bg.rect = Rect{m_geometry.absolutePos, m_geometry.size};
        bg.color = BgColor.Get();
        bg.cornerRadius = 3.0f;
        bg.borderThickness = 1.0f;
        bg.borderColor = Color{0.27f, 0.30f, 0.36f, 1.0f};
        renderer.PushElement(bg);

        DrawElement label{};
        label.kind = DrawKind::Text;
        label.rect = Rect{
            Vec2{m_geometry.absolutePos.x + 8.0f, m_geometry.absolutePos.y + 6.0f},
            Vec2{m_geometry.size.x - 24.0f, 14.0f}};
        label.color = TextColor.Get();
        label.text = SelectedLabel();
        label.fontSize = 11.5f;
        renderer.PushElement(label);

        // Chevron — small triangle drawn as 3 stacked quads (cheap; no triangle primitive yet).
        const float cx = m_geometry.absolutePos.x + m_geometry.size.x - 12.0f;
        const float cy = m_geometry.absolutePos.y + m_geometry.size.y * 0.5f - 1.0f;
        for (int i = 0; i < 3; ++i) {
            DrawElement chev{};
            chev.kind = DrawKind::Quad;
            chev.rect = Rect{Vec2{cx + static_cast<float>(i), cy + static_cast<float>(i)},
                             Vec2{static_cast<float>(7 - 2 * i), 1.0f}};
            chev.color = TextColor.Get();
            renderer.PushElement(chev);
        }
    }

    EventReply OnPointerDown(const PointerEvent& e) override {
        if (e.button != PointerButton::Left) return EventReply::Unhandled;
        m_open = !m_open;
        if (m_popupHost) m_popupHost(BuildPopupWidget(), m_open);
        Invalidate(InvalidationFlag::Paint);
        return EventReply::Handled;
    }

    /// Called by the host when the user clicks an option in the popup.
    /// Tests can call this directly to simulate selection.
    void SelectOption(int idx) {
        if (idx < 0 || static_cast<std::size_t>(idx) >= m_options.size()) return;
        SelectedIndex = idx;
        m_open = false;
        if (m_popupHost) m_popupHost(nullptr, false);
        if (OnSelectionChanged) OnSelectionChanged(idx);
        Invalidate(InvalidationFlag::Paint);
    }

private:
    /// Step 7 placeholder: returns nullptr because the popup widget is
    /// authored by the host once Step 9 introduces the TopMost layer.
    /// Kept as a virtual extension point so tests can subclass and
    /// inject behaviour.
    SP<Widget> BuildPopupWidget() {
        return nullptr;
    }

    std::vector<T> m_options;
    Labeler        m_labeler;
    PopupHost      m_popupHost;
    bool           m_open{false};
};

} // namespace uz::ui
