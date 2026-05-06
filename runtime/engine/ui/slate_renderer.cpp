#include "engine/ui/slate_renderer.h"

#include "editor/font/text_renderer.h"
#include "engine/logging/log.h"

#include <cmath>

UZ_DEFINE_LOG_CATEGORY(SlateUI, Info);

namespace uz::ui {

namespace {

/// Returns true if the rect to draw is fully outside the active scissor.
/// An empty `scissor` (zero-size top of stack) is treated as "no clipping",
/// matching the semantics of an explicit ScissorPop returning to the root.
bool RectOutsideScissor(const Rect& draw, const Rect& scissor) {
    if (scissor.size.x <= 0.0f || scissor.size.y <= 0.0f) {
        return false;
    }
    if (draw.Right() <= scissor.Left() || draw.Left() >= scissor.Right()) {
        return true;
    }
    if (draw.Bottom() <= scissor.Top() || draw.Top() >= scissor.Bottom()) {
        return true;
    }
    return false;
}

} // anonymous namespace

SlateRenderer::SlateRenderer() {
    m_elements.reserve(256);
    m_scissorScratch.reserve(8);
}

void SlateRenderer::BeginFrame() {
    m_elements.clear();
    m_scissorScratch.clear();
}

void SlateRenderer::PushElement(const DrawElement& e) {
    m_elements.push_back(e);
}

void SlateRenderer::Clear() noexcept {
    m_elements.clear();
    m_scissorScratch.clear();
}

void SlateRenderer::Submit(TextRenderer& text, const BitmapFont& font,
                           const MsdfFont* msdfFont, float dpiScale) {
    if (dpiScale <= 0.0f) {
        UZ_LOG(SlateUI, Error, "Submit called with non-positive dpiScale={}",
               static_cast<double>(dpiScale));
        return;
    }

    m_scissorScratch.clear();

    auto top_scissor = [&]() -> Rect {
        return m_scissorScratch.empty() ? Rect{} : m_scissorScratch.back();
    };

    for (const DrawElement& e : m_elements) {
        switch (e.kind) {
        case DrawKind::ScissorPush:
            m_scissorScratch.push_back(e.rect);
            continue;

        case DrawKind::ScissorPop:
            if (!m_scissorScratch.empty()) {
                m_scissorScratch.pop_back();
            } else {
                UZ_LOG(SlateUI, Warning, "ScissorPop with empty stack");
            }
            continue;

        default:
            break;
        }

        // Trivial reject against the current scissor. Partially-overlapping
        // rects still go through; the SDF shader handles its own bounds.
        const Rect scissor = top_scissor();
        if (RectOutsideScissor(e.rect, scissor)) {
            continue;
        }

        const float x = e.rect.pos.x   * dpiScale;
        const float y = e.rect.pos.y   * dpiScale;
        const float w = e.rect.size.x  * dpiScale;
        const float h = e.rect.size.y  * dpiScale;

        switch (e.kind) {
        case DrawKind::Quad: {
            text.AddRoundedRect(
                x, y, w, h,
                e.cornerRadius * dpiScale,
                e.color,
                e.borderThickness * dpiScale,
                e.borderColor,
                Vec2{e.shadowOffset.x * dpiScale, e.shadowOffset.y * dpiScale},
                e.shadowBlur * dpiScale,
                e.shadowColor);
            break;
        }
        case DrawKind::Line: {
            // 1px line via thin rect. Aligns to integer pixels for crispness.
            const float t = e.lineThickness * dpiScale;
            const float x0 = e.rect.pos.x * dpiScale;
            const float y0 = e.rect.pos.y * dpiScale;
            const float x1 = e.lineEnd.x  * dpiScale;
            const float y1 = e.lineEnd.y  * dpiScale;
            const bool horizontal = std::abs(y1 - y0) <= std::abs(x1 - x0);
            if (horizontal) {
                const float lx = x0 < x1 ? x0 : x1;
                const float ly = y0 - t * 0.5f;
                const float lw = std::abs(x1 - x0);
                text.AddRect(lx, ly, lw, t, e.color);
            } else {
                const float lx = x0 - t * 0.5f;
                const float ly = y0 < y1 ? y0 : y1;
                const float lh = std::abs(y1 - y0);
                text.AddRect(lx, ly, t, lh, e.color);
            }
            break;
        }
        case DrawKind::Text: {
            if (e.msdf && msdfFont != nullptr) {
                text.AddMsdfText(*msdfFont, e.text, x, y,
                                 e.fontSize * dpiScale, e.color);
            } else {
                text.AddText(font, e.text, x, y, e.color);
            }
            break;
        }
        case DrawKind::Sprite: {
            text.AddSprite(e.iconId, x, y, w, h, e.color);
            break;
        }
        case DrawKind::ScissorPush:
        case DrawKind::ScissorPop:
            // Already handled above.
            break;
        }
    }

    if (!m_scissorScratch.empty()) {
        UZ_LOG(SlateUI, Warning,
               "Submit ended with {} unclosed scissor(s); auto-popping",
               m_scissorScratch.size());
        m_scissorScratch.clear();
    }
}

} // namespace uz::ui
