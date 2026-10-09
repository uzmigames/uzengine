/// UzEngine Slate UI — drag-and-drop manager.
///
/// Phase 18 / Step 9. Tracks the lifecycle of a single drag operation:
///
///   - `BeginDetect(pos, source)` — call on PointerDown over a draggable
///     widget. Stores the start position so a subsequent move can
///     trigger the actual drag.
///   - `Update(currentPos, ...)` — call on PointerMove. If the cursor
///     has moved further than `kDragThreshold` from the start AND the
///     manager is not yet dragging, asks the source widget's
///     `OnDragDetected` to produce a `DragOperation`. If non-null, the
///     manager enters the dragging state and the operation's `payload`
///     can be inspected by drop targets.
///   - `Update` while dragging routes `OnDragEnter` / `OnDragLeave`
///     hover transitions to widgets under the cursor.
///   - `EndDrop(pos, target)` — call on PointerUp. If dragging, calls
///     the current target's `OnDrop(op)` and clears state.
///   - `Cancel()` — abort an in-progress drag (e.g. ESC key).

#pragma once

#include "engine/ui/slate_widget.h"

#include <any>
#include <string>

namespace uz::ui {

struct DragOperation {
    std::any     payload;       ///< User-defined; consumers downcast via std::any_cast.
    std::string  typeTag;       ///< e.g. "vfx.module" / "asset.uzmesh".
    SP<Widget>   ghost;         ///< Optional visual that follows the cursor.
};

/// Distance (Slate units) the cursor must move from the start position
/// before a drag is officially detected.
constexpr float kDragThreshold = 4.0f;

class DragDropManager {
public:
    /// Records a potential drag start. No state change is observable
    /// until `Update` crosses the threshold.
    void BeginDetect(Vec2 startPos, SP<Widget> source);

    /// Per-Move update. If a drag has started, fires hover transitions
    /// (`OnDragEnter` / `OnDragLeave`) on `currentTarget`. Returns true
    /// if a drag is actively in progress.
    ///
    /// `srcOnDragDetected` is the widget-supplied callback that builds
    /// the actual `DragOperation` once the threshold is crossed; pass
    /// `nullptr` if the source did not register one (the manager will
    /// still track positions but no drop fires).
    bool Update(Vec2 currentPos,
                const SP<Widget>& currentTarget,
                const std::function<SP<DragOperation>(Vec2)>& srcOnDragDetected);

    /// PointerUp. If dragging and `target` is set, calls
    /// `target->OnDrop(op)`. Always clears state. Returns true if a
    /// drop was actually performed.
    bool EndDrop(Vec2 endPos, const SP<Widget>& target);

    /// Aborts an in-progress drag without firing OnDrop.
    void Cancel();

    [[nodiscard]] bool IsDetecting() const noexcept { return m_detecting; }
    [[nodiscard]] bool IsDragging()  const noexcept { return m_dragging; }
    [[nodiscard]] SP<Widget> Source() const noexcept { return m_source.lock(); }
    [[nodiscard]] SP<DragOperation> Active() const noexcept { return m_op; }

private:
    void EnterDragMode(SP<DragOperation> op);
    void NotifyEnterLeave(const SP<Widget>& newTarget);

    bool       m_detecting{false};
    bool       m_dragging{false};
    Vec2       m_startPos{};
    WP<Widget> m_source;
    WP<Widget> m_lastEnter;
    SP<DragOperation> m_op;
};

DragDropManager& DragDrop();

} // namespace uz::ui
