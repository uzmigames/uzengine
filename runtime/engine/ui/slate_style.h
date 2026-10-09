/// UzEngine Slate UI — StyleSet + StyleStack.
///
/// Phase 18 / Step 6. A `StyleSet` is a named lookup table for colors,
/// brushes, and fonts — the design-tokens equivalent. Widgets fetch
/// values by name (`Styles().GetColor("accent")`) so changing a theme
/// is one `Push` away from a complete visual swap with no widget rebuild.
///
/// `StyleStack` is the process-wide singleton stack of StyleSets. The
/// most-recently-pushed StyleSet wins for each lookup; if a key is
/// missing, the search falls through to the next StyleSet in the stack;
/// if all are missing it, the supplied fallback value is returned.
///
/// Built-in: `claude.dark` populated 1:1 from the project's
/// `.sandbox/claude-design/vfx-template/project/styles.css` tokens
/// (bg.0..bg.4 / line.0..line.2 / fg.0..fg.3 / accent / accent.soft /
/// accent.line / warn / ok / err / magenta / purple / cyan /
/// axis.x / axis.y / axis.z + `panel.head`, `panel.body`, `tab.active`,
/// `tab.inactive`, `button.primary`, `button.ghost`).

#pragma once

#include "engine/ui/slate_brush.h"
#include "engine/ui/slate_core.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace uz {
class BitmapFont;
} // namespace uz

namespace uz::ui {

/// Named lookup of design tokens.
class StyleSet {
public:
    StyleSet() = default;

    void SetColor(const std::string& name, Color c) { m_colors[name] = c; }
    void SetBrush(const std::string& name, Brush b) { m_brushes[name] = b; }
    void SetFont(const std::string& name, const BitmapFont* font, float sizeUnits) {
        m_fonts[name] = FontEntry{font, sizeUnits};
    }

    /// Returns the color for `name`, or `fallback` if absent.
    [[nodiscard]] Color GetColor(const std::string& name, Color fallback = {}) const {
        auto it = m_colors.find(name);
        return it != m_colors.end() ? it->second : fallback;
    }

    /// Returns the brush for `name`, or `fallback` if absent.
    [[nodiscard]] Brush GetBrush(const std::string& name, Brush fallback = {}) const {
        auto it = m_brushes.find(name);
        return it != m_brushes.end() ? it->second : fallback;
    }

    struct FontEntry { const BitmapFont* font; float sizeUnits; };

    [[nodiscard]] FontEntry GetFont(const std::string& name) const {
        auto it = m_fonts.find(name);
        return it != m_fonts.end() ? it->second : FontEntry{nullptr, 0.0f};
    }

    /// True if the StyleSet has a color named `name`.
    [[nodiscard]] bool HasColor(const std::string& name) const {
        return m_colors.find(name) != m_colors.end();
    }
    [[nodiscard]] bool HasBrush(const std::string& name) const {
        return m_brushes.find(name) != m_brushes.end();
    }

    /// Number of color / brush entries (introspection / tests).
    [[nodiscard]] std::size_t ColorCount() const noexcept { return m_colors.size(); }
    [[nodiscard]] std::size_t BrushCount() const noexcept { return m_brushes.size(); }

private:
    std::unordered_map<std::string, Color>      m_colors;
    std::unordered_map<std::string, Brush>      m_brushes;
    std::unordered_map<std::string, FontEntry>  m_fonts;
};

/// Stack of style sets — last pushed wins, missing keys fall through.
class StyleStack {
public:
    void Push(SP<StyleSet> set) { m_stack.push_back(std::move(set)); }
    void Pop() { if (!m_stack.empty()) m_stack.pop_back(); }
    [[nodiscard]] std::size_t Depth() const noexcept { return m_stack.size(); }

    /// Top-down search; returns fallback if no StyleSet has the key.
    [[nodiscard]] Color GetColor(const std::string& name, Color fallback = {}) const {
        for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it) {
            if (*it && (*it)->HasColor(name)) {
                return (*it)->GetColor(name, fallback);
            }
        }
        return fallback;
    }

    [[nodiscard]] Brush GetBrush(const std::string& name, Brush fallback = {}) const {
        for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it) {
            if (*it && (*it)->HasBrush(name)) {
                return (*it)->GetBrush(name, fallback);
            }
        }
        return fallback;
    }

    /// Drops every set, leaving the stack empty. Used by tests.
    void Clear() noexcept { m_stack.clear(); }

private:
    std::vector<SP<StyleSet>> m_stack;
};

/// Process-wide singleton stack. First call creates the stack and pushes
/// the built-in `claude.dark` StyleSet so anonymous widgets get sane
/// defaults out of the box.
StyleStack& Styles();

/// Returns a fresh StyleSet populated with the Claude dark design
/// tokens. The returned set is independent — callers may mutate it
/// freely (used by tests and by theme inheritance flows).
SP<StyleSet> CreateClaudeDarkStyleSet();

} // namespace uz::ui
