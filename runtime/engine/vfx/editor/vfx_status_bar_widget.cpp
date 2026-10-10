/// VfxStatusBarWidget — implementation (phase 14.6.0).

#include "vfx_status_bar_widget.h"

#include "editor/font/bitmap_font.h"
#include "editor/font/msdf_font.h"
#include "editor/font/text_renderer.h"
#include "engine/vfx/editor/vfx_editor_theme.h"

#include <cstdio>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Notifications
// ─────────────────────────────────────────────────────────────────────────────

void VfxStatusBarWidget::PushNotification(const std::string& msg,
                                          VfxStatusLevel level) {
    VfxStatusNotification n;
    n.message = msg;
    n.level   = level;
    n.secondsRemaining = 3.0f;
    m_queue.push_back(n);
    while (m_queue.size() > 8u) m_queue.pop_front();
}

void VfxStatusBarWidget::Tick(float dt) {
    if (m_queue.empty()) return;
    m_queue.front().secondsRemaining -= dt;
    if (m_queue.front().secondsRemaining <= 0.0f) {
        m_queue.pop_front();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Colour helpers
// ─────────────────────────────────────────────────────────────────────────────

Vec4 VfxStatusBarWidget::FpsTint(float fps) {
    const auto& c = VfxThemeColors();
    if (fps >= 55.0f) return c.stateSuccess;
    if (fps >= 30.0f) return c.stateWarning;
    return c.stateError;
}

Vec4 VfxStatusBarWidget::NotificationTint(VfxStatusLevel level) const {
    const auto& c = VfxThemeColors();
    switch (level) {
    case VfxStatusLevel::Info:    return c.stateInfo;
    case VfxStatusLevel::Warning: return c.stateWarning;
    case VfxStatusLevel::Error:   return c.stateError;
    }
    return c.textPrimary;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void VfxStatusBarWidget::Render(TextRenderer& text, const BitmapFont& font,
                                const MsdfFont* msdf,
                                const VfxStackRect& barBounds) const {
    const auto& palette = VfxThemeColors();

    text.AddRect(barBounds.x, barBounds.y, barBounds.width, barBounds.height,
                 palette.barBg);
    // Top-edge 1-px divider.
    text.AddRect(barBounds.x, barBounds.y, barBounds.width, 1.0f,
                 palette.borderSubtle);

    const float textY = barBounds.y + (barBounds.height - 10.0f) * 0.5f + 1.0f;
    auto drawText = [&](const std::string& s, float x, const Vec4& tint) {
        if (msdf != nullptr) {
            text.AddMsdfText(*msdf, s, x, textY, 10.0f, tint);
        } else {
            text.AddText(font, s, x, barBounds.y + 6.0f, tint);
        }
    };

    // ── Left cluster: FPS / particles / draw calls / VRAM ───────────────
    float x = barBounds.x + 8.0f;
    char buf[64];

    std::snprintf(buf, sizeof(buf), "FPS %.0f", m_fps);
    drawText(buf, x, FpsTint(m_fps));
    x += chipWidth;
    text.AddRect(x - chipGap, barBounds.y + 4.0f, 1.0f, barBounds.height - 8.0f,
                 palette.borderSubtle);

    std::snprintf(buf, sizeof(buf), "P %llu / %llu",
                   static_cast<unsigned long long>(m_alive),
                   static_cast<unsigned long long>(m_maxCount));
    drawText(buf, x, palette.textSecondary);
    x += chipWidth;
    text.AddRect(x - chipGap, barBounds.y + 4.0f, 1.0f, barBounds.height - 8.0f,
                 palette.borderSubtle);

    std::snprintf(buf, sizeof(buf), "DC %u", m_drawCalls);
    drawText(buf, x, palette.textSecondary);
    x += chipWidth;

    // ── Right cluster: editor version / saved / VRAM ────────────────────
    float rx = barBounds.x + barBounds.width - 8.0f;
    std::snprintf(buf, sizeof(buf), "v%s", m_editorVersion.c_str());
    // Approximate width — `rx - label width`.
    const float verW = static_cast<float>(std::strlen(buf)) * 7.0f + 6.0f;
    drawText(buf, rx - verW, palette.textDim);
    rx -= verW + chipGap;

    text.AddRect(rx, barBounds.y + 4.0f, 1.0f, barBounds.height - 8.0f,
                 palette.borderSubtle);
    rx -= chipGap;

    const char* savedLabel = m_dirty ? "unsaved" : "saved";
    const Vec4   savedTint = m_dirty ? palette.stateWarning
                                      : palette.stateSuccess;
    const float savedW = static_cast<float>(std::strlen(savedLabel)) * 7.0f
                           + 12.0f;
    drawText(savedLabel, rx - savedW, savedTint);
    rx -= savedW + chipGap;

    text.AddRect(rx, barBounds.y + 4.0f, 1.0f, barBounds.height - 8.0f,
                 palette.borderSubtle);
    rx -= chipGap;

    std::snprintf(buf, sizeof(buf), "VRAM %.0f/%.0f MB",
                   m_vramUsedMb, m_vramTotalMb);
    const float vramW = static_cast<float>(std::strlen(buf)) * 7.0f + 6.0f;
    drawText(buf, rx - vramW, palette.textSecondary);

    // ── Centre transient notification ───────────────────────────────────
    if (!m_queue.empty()) {
        const auto& n = m_queue.front();
        const float nW = static_cast<float>(n.message.size()) * 7.0f;
        const float nx = barBounds.x + (barBounds.width - nW) * 0.5f;
        drawText(n.message, nx, NotificationTint(n.level));
    }
}

} // namespace uz
