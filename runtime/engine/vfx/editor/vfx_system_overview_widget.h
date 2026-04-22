#pragma once

/// VFX System Overview Widget — right-panel emitter list (phase 12.9.0).
///
/// Niagara's system overview pane shows one row per emitter in the
/// active `VfxSystemComponent` with quick CRUD actions: toggle
/// enable, rename, duplicate, delete, reorder, + Add Emitter.
///
/// # Design — data view
///
/// The widget binds to `const std::vector<VfxOverviewEmitterView>*`
/// instead of `ParticleEmitter*` directly so tests can stand up a
/// mock list without pulling the full GPU particle stack. The host
/// (`VfxEditorOverlay`) refreshes the backing vector once per frame
/// from the active system's emitter list.
///
/// # Caller-applied contract
///
/// Every mutation callback is caller-applied — the widget never edits
/// the bound vector. The host translates `onEmitterMoved / Duplicated /
/// Deleted / AddRequested` into concrete edits on `VfxSystemComponent`.
///
/// @ref docs/engine/25-vfx/61-system-overview.md
/// @ref UE5 `SNiagaraSystemOverview`

#include "engine/vfx/editor/vfx_stack_panel_widget.h"  // VfxStackRect

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

class TextRenderer;
class BitmapFont;

// ─────────────────────────────────────────────────────────────────────────────
// Data view
// ─────────────────────────────────────────────────────────────────────────────

/// One emitter row as the overlay snapshot delivers it to the widget.
struct VfxOverviewEmitterView {
    std::string name;
    uint32_t    aliveCount = 0;
    bool        isEmitting = true;   ///< Runtime flag (`emitter.isEmitting`).
    bool        isEnabled  = true;   ///< Author toggle (usually mirrors `isEmitting`).
};

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxOverviewItemType : uint8_t {
    None           = 0,
    Row            = 1,   ///< Whole emitter row (lowest priority hit).
    DragHandle     = 2,   ///< Left grip sub-rect (reorder drag).
    Checkbox       = 3,   ///< Enable toggle.
    NameLabel      = 4,   ///< Name area (double-click rename — handled by host).
    StatusDot      = 5,   ///< Running/paused/disabled indicator.
    ParticleCount  = 6,   ///< Right-aligned count text.
    BtnMoveUp      = 7,   ///< Move-up action button.
    BtnMoveDown    = 8,   ///< Move-down action button.
    BtnDuplicate   = 9,   ///< Duplicate action button.
    BtnDelete      = 10,  ///< Delete action button.
    AddEmitterRow  = 11,  ///< Trailing "+ Add Emitter" row.
};

struct VfxOverviewLayoutItem {
    VfxOverviewItemType type = VfxOverviewItemType::None;
    VfxStackRect        rect;
    size_t              emitterIndex = static_cast<size_t>(-1);
};

struct VfxOverviewHitResult {
    VfxOverviewItemType type = VfxOverviewItemType::None;
    size_t              emitterIndex = static_cast<size_t>(-1);

    [[nodiscard]] bool IsHit() const noexcept {
        return type != VfxOverviewItemType::None;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Widget
// ─────────────────────────────────────────────────────────────────────────────

class VfxSystemOverviewWidget {
public:
    // ── Tunables ───────────────────────────────────────────────────────
    float rowHeight         = 28.0f;
    float addRowHeight      = 24.0f;
    float dragHandleWidth   = 14.0f;
    float checkboxWidth     = 18.0f;
    float statusDotSize     = 8.0f;
    float actionBtnWidth    = 22.0f;   ///< Per-action button width.
    float padding           = 4.0f;

    // ── Binding ────────────────────────────────────────────────────────

    void Bind(const std::vector<VfxOverviewEmitterView>* emitters) noexcept {
        m_emitters = emitters;
    }

    [[nodiscard]] const std::vector<VfxOverviewEmitterView>* Emitters() const noexcept {
        return m_emitters;
    }

    [[nodiscard]] size_t EmitterCount() const noexcept {
        return m_emitters != nullptr ? m_emitters->size() : 0u;
    }

    // ── Selection ──────────────────────────────────────────────────────

    [[nodiscard]] size_t SelectedIndex() const noexcept { return m_selectedIndex; }
    void SelectIndex(size_t index) noexcept { m_selectedIndex = index; }
    void ClearSelection() noexcept { m_selectedIndex = static_cast<size_t>(-1); }

    // ── Drag (reorder) ─────────────────────────────────────────────────

    void BeginDrag(size_t index) noexcept;
    void EndDrag(size_t targetIndex);
    void CancelDrag() noexcept {
        m_dragActive = false;
        m_dragSrcIndex = static_cast<size_t>(-1);
    }
    [[nodiscard]] bool   IsDragging()  const noexcept { return m_dragActive; }
    [[nodiscard]] size_t DragSrcIndex() const noexcept { return m_dragSrcIndex; }

    // ── Layout / Hit-test / Render ────────────────────────────────────

    [[nodiscard]] std::vector<VfxOverviewLayoutItem> ComputeLayout(
        const VfxStackRect& panelBounds) const;

    [[nodiscard]] VfxOverviewHitResult HitTest(
        const std::vector<VfxOverviewLayoutItem>& layout,
        float x, float y) const;

    void Render(TextRenderer& text, const BitmapFont& font,
                const VfxStackRect& panelBounds) const;

    // ── Callbacks (caller-applied mutations) ──────────────────────────

    std::function<void(size_t)>                 onEmitterSelected;
    std::function<void(size_t, bool enabled)>   onEmitterEnableToggled;
    std::function<void(size_t, size_t)>         onEmitterMoved; ///< (src, dst)
    std::function<void(size_t)>                 onEmitterDuplicated;
    std::function<void(size_t)>                 onEmitterDeleted;
    std::function<void()>                       onAddEmitterRequested;

private:
    const std::vector<VfxOverviewEmitterView>* m_emitters = nullptr;

    size_t m_selectedIndex = static_cast<size_t>(-1);
    size_t m_dragSrcIndex  = static_cast<size_t>(-1);
    bool   m_dragActive    = false;
};

} // namespace uz
