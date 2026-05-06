#include "engine/ui/slate_box.h"

#include "engine/ui/slate_renderer.h"

#include <algorithm>

namespace uz::ui {

namespace {

/// Applies HAlign / VAlign of a child within an arena, returning the
/// final child rect after the alignment is resolved. `childSize` is what
/// Measure returned (inset already applied).
Geometry AlignedChildGeometry(const SlotBase& slot,
                              Vec2 arenaPos, Vec2 arenaSize,
                              Vec2 childDesiredSize,
                              float dpiScale) {
    Vec2 size = childDesiredSize;
    Vec2 origin = arenaPos;

    // X axis.
    if (slot.hAlign == HAlign::Fill) {
        size.x = arenaSize.x;
    } else {
        if (size.x > arenaSize.x) size.x = arenaSize.x;
        switch (slot.hAlign) {
        case HAlign::Left:
            break; // origin.x stays.
        case HAlign::Center:
            origin.x = arenaPos.x + (arenaSize.x - size.x) * 0.5f;
            break;
        case HAlign::Right:
            origin.x = arenaPos.x + (arenaSize.x - size.x);
            break;
        case HAlign::Fill:
            break;
        }
    }

    // Y axis.
    if (slot.vAlign == VAlign::Fill) {
        size.y = arenaSize.y;
    } else {
        if (size.y > arenaSize.y) size.y = arenaSize.y;
        switch (slot.vAlign) {
        case VAlign::Top:
            break;
        case VAlign::Center:
            origin.y = arenaPos.y + (arenaSize.y - size.y) * 0.5f;
            break;
        case VAlign::Bottom:
            origin.y = arenaPos.y + (arenaSize.y - size.y);
            break;
        case VAlign::Fill:
            break;
        }
    }

    Geometry g{};
    g.absolutePos = origin;
    g.size = size;
    g.dpiScale = dpiScale;
    return g;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// HorizontalBox
// ─────────────────────────────────────────────────────────────────────────────

SlotBase& HorizontalBox::AddSlot(SP<Widget> child) {
    SlotBase s;
    s.widget = std::move(child);
    m_slots.push_back(std::move(s));
    return m_slots.back();
}

Vec2 HorizontalBox::Measure(Vec2 maxSize) {
    float totalW = 0.0f;
    float maxH = 0.0f;
    for (auto& slot : m_slots) {
        if (!slot.widget) continue;
        const float padX = slot.PaddingX();
        const float padY = slot.PaddingY();
        const Vec2 childMax{
            std::max(0.0f, maxSize.x - totalW - padX),
            std::max(0.0f, maxSize.y - padY)};
        Vec2 desired = slot.widget->Measure(childMax);
        totalW += desired.x + padX;
        maxH = std::max(maxH, desired.y + padY);
    }
    if (maxSize.x > 0.0f && totalW > maxSize.x) totalW = maxSize.x;
    if (maxSize.y > 0.0f && maxH   > maxSize.y) maxH   = maxSize.y;
    return Vec2{totalW, maxH};
}

void HorizontalBox::Arrange(Geometry geometry) {
    m_geometry = geometry;

    // Pass 1: figure out Auto widths and total fillFactor.
    std::vector<float> autoWidths(m_slots.size(), 0.0f);
    float autoTotal = 0.0f;
    float fillTotal = 0.0f;
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        SlotBase& s = m_slots[i];
        if (!s.widget) continue;
        const float padX = s.PaddingX();
        const float padY = s.PaddingY();
        if (s.sizeRule == SizeRule::Auto) {
            const Vec2 childMax{
                std::max(0.0f, geometry.size.x - padX),
                std::max(0.0f, geometry.size.y - padY)};
            autoWidths[i] = s.widget->Measure(childMax).x;
            autoTotal += autoWidths[i] + padX;
        } else {
            fillTotal += std::max(0.0f, s.fillFactor);
            autoTotal += padX; // Padding still consumes space.
        }
    }

    const float remaining = std::max(0.0f, geometry.size.x - autoTotal);

    // Pass 2: arrange each child at the running cursor.
    float cursor = geometry.absolutePos.x;
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        SlotBase& s = m_slots[i];
        if (!s.widget) continue;
        const float padX = s.PaddingX();
        const float padY = s.PaddingY();

        float allocW = 0.0f;
        if (s.sizeRule == SizeRule::Auto) {
            allocW = autoWidths[i];
        } else if (fillTotal > 0.0f) {
            allocW = remaining * (std::max(0.0f, s.fillFactor) / fillTotal);
        }

        const Vec2 arenaPos{
            cursor + s.padding.x,
            geometry.absolutePos.y + s.padding.y};
        const Vec2 arenaSize{
            allocW,
            std::max(0.0f, geometry.size.y - padY)};

        // Child desired size for alignment math.
        Vec2 childDesired = arenaSize;
        if (s.hAlign != HAlign::Fill || s.vAlign != VAlign::Fill) {
            childDesired = s.widget->Measure(arenaSize);
        }

        Geometry childGeo = AlignedChildGeometry(s, arenaPos, arenaSize,
                                                 childDesired, geometry.dpiScale);
        s.widget->Arrange(childGeo);

        cursor += allocW + padX;
    }
}

void HorizontalBox::Paint(SlateRenderer& renderer) {
    for (auto& slot : m_slots) {
        if (slot.widget) slot.widget->Paint(renderer);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// VerticalBox
// ─────────────────────────────────────────────────────────────────────────────

SlotBase& VerticalBox::AddSlot(SP<Widget> child) {
    SlotBase s;
    s.widget = std::move(child);
    m_slots.push_back(std::move(s));
    return m_slots.back();
}

Vec2 VerticalBox::Measure(Vec2 maxSize) {
    float totalH = 0.0f;
    float maxW = 0.0f;
    for (auto& slot : m_slots) {
        if (!slot.widget) continue;
        const float padX = slot.PaddingX();
        const float padY = slot.PaddingY();
        const Vec2 childMax{
            std::max(0.0f, maxSize.x - padX),
            std::max(0.0f, maxSize.y - totalH - padY)};
        Vec2 desired = slot.widget->Measure(childMax);
        totalH += desired.y + padY;
        maxW = std::max(maxW, desired.x + padX);
    }
    if (maxSize.x > 0.0f && maxW   > maxSize.x) maxW   = maxSize.x;
    if (maxSize.y > 0.0f && totalH > maxSize.y) totalH = maxSize.y;
    return Vec2{maxW, totalH};
}

void VerticalBox::Arrange(Geometry geometry) {
    m_geometry = geometry;

    std::vector<float> autoHeights(m_slots.size(), 0.0f);
    float autoTotal = 0.0f;
    float fillTotal = 0.0f;
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        SlotBase& s = m_slots[i];
        if (!s.widget) continue;
        const float padX = s.PaddingX();
        const float padY = s.PaddingY();
        if (s.sizeRule == SizeRule::Auto) {
            const Vec2 childMax{
                std::max(0.0f, geometry.size.x - padX),
                std::max(0.0f, geometry.size.y - padY)};
            autoHeights[i] = s.widget->Measure(childMax).y;
            autoTotal += autoHeights[i] + padY;
        } else {
            fillTotal += std::max(0.0f, s.fillFactor);
            autoTotal += padY;
        }
    }

    const float remaining = std::max(0.0f, geometry.size.y - autoTotal);

    float cursor = geometry.absolutePos.y;
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        SlotBase& s = m_slots[i];
        if (!s.widget) continue;
        const float padX = s.PaddingX();
        const float padY = s.PaddingY();

        float allocH = 0.0f;
        if (s.sizeRule == SizeRule::Auto) {
            allocH = autoHeights[i];
        } else if (fillTotal > 0.0f) {
            allocH = remaining * (std::max(0.0f, s.fillFactor) / fillTotal);
        }

        const Vec2 arenaPos{
            geometry.absolutePos.x + s.padding.x,
            cursor + s.padding.y};
        const Vec2 arenaSize{
            std::max(0.0f, geometry.size.x - padX),
            allocH};

        Vec2 childDesired = arenaSize;
        if (s.hAlign != HAlign::Fill || s.vAlign != VAlign::Fill) {
            childDesired = s.widget->Measure(arenaSize);
        }

        Geometry childGeo = AlignedChildGeometry(s, arenaPos, arenaSize,
                                                 childDesired, geometry.dpiScale);
        s.widget->Arrange(childGeo);

        cursor += allocH + padY;
    }
}

void VerticalBox::Paint(SlateRenderer& renderer) {
    for (auto& slot : m_slots) {
        if (slot.widget) slot.widget->Paint(renderer);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Overlay
// ─────────────────────────────────────────────────────────────────────────────

SlotBase& Overlay::AddSlot(SP<Widget> child) {
    SlotBase s;
    s.widget = std::move(child);
    m_slots.push_back(std::move(s));
    return m_slots.back();
}

Vec2 Overlay::Measure(Vec2 maxSize) {
    Vec2 result{0.0f, 0.0f};
    for (auto& slot : m_slots) {
        if (!slot.widget) continue;
        const float padX = slot.PaddingX();
        const float padY = slot.PaddingY();
        const Vec2 childMax{
            std::max(0.0f, maxSize.x - padX),
            std::max(0.0f, maxSize.y - padY)};
        Vec2 desired = slot.widget->Measure(childMax);
        result.x = std::max(result.x, desired.x + padX);
        result.y = std::max(result.y, desired.y + padY);
    }
    if (maxSize.x > 0.0f && result.x > maxSize.x) result.x = maxSize.x;
    if (maxSize.y > 0.0f && result.y > maxSize.y) result.y = maxSize.y;
    return result;
}

void Overlay::Arrange(Geometry geometry) {
    m_geometry = geometry;
    for (auto& slot : m_slots) {
        if (!slot.widget) continue;
        const Vec2 arenaPos{
            geometry.absolutePos.x + slot.padding.x,
            geometry.absolutePos.y + slot.padding.y};
        const Vec2 arenaSize{
            std::max(0.0f, geometry.size.x - slot.PaddingX()),
            std::max(0.0f, geometry.size.y - slot.PaddingY())};

        Vec2 childDesired = arenaSize;
        if (slot.hAlign != HAlign::Fill || slot.vAlign != VAlign::Fill) {
            childDesired = slot.widget->Measure(arenaSize);
        }
        Geometry childGeo = AlignedChildGeometry(slot, arenaPos, arenaSize,
                                                 childDesired, geometry.dpiScale);
        slot.widget->Arrange(childGeo);
    }
}

void Overlay::Paint(SlateRenderer& renderer) {
    for (auto& slot : m_slots) {
        if (slot.widget) slot.widget->Paint(renderer);
    }
}

} // namespace uz::ui
