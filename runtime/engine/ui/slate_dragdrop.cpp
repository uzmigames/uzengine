#include "engine/ui/slate_dragdrop.h"

#include <cmath>

namespace uz::ui {

void DragDropManager::BeginDetect(Vec2 startPos, SP<Widget> source) {
    m_detecting = true;
    m_dragging = false;
    m_startPos = startPos;
    m_source = source;
    m_op.reset();
    m_lastEnter.reset();
}

void DragDropManager::EnterDragMode(SP<DragOperation> op) {
    m_dragging = true;
    m_op = std::move(op);
}

void DragDropManager::NotifyEnterLeave(const SP<Widget>& newTarget) {
    SP<Widget> last = m_lastEnter.lock();
    if (newTarget == last) return;
    if (last)      last->OnDragLeave();
    if (newTarget) newTarget->OnDragEnter();
    m_lastEnter = newTarget;
}

bool DragDropManager::Update(Vec2 currentPos,
                             const SP<Widget>& currentTarget,
                             const std::function<SP<DragOperation>(Vec2)>& srcOnDragDetected) {
    if (!m_detecting && !m_dragging) return false;

    if (!m_dragging) {
        // Threshold check.
        const float dx = currentPos.x - m_startPos.x;
        const float dy = currentPos.y - m_startPos.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < kDragThreshold) return false;

        // Crossed threshold — ask the source for a DragOperation.
        if (!srcOnDragDetected) {
            // Source didn't supply a callback; drop the detect state.
            m_detecting = false;
            return false;
        }
        SP<DragOperation> op = srcOnDragDetected(currentPos);
        if (!op) {
            m_detecting = false;
            return false;
        }
        EnterDragMode(std::move(op));
    }

    NotifyEnterLeave(currentTarget);
    return true;
}

bool DragDropManager::EndDrop(Vec2 /*endPos*/, const SP<Widget>& target) {
    bool dropFired = false;
    if (m_dragging && target) {
        target->OnDrop();
        dropFired = true;
    }
    // Always clean up.
    if (auto last = m_lastEnter.lock()) last->OnDragLeave();
    m_detecting = false;
    m_dragging = false;
    m_startPos = Vec2{};
    m_source.reset();
    m_lastEnter.reset();
    m_op.reset();
    return dropFired;
}

void DragDropManager::Cancel() {
    if (auto last = m_lastEnter.lock()) last->OnDragLeave();
    m_detecting = false;
    m_dragging = false;
    m_source.reset();
    m_lastEnter.reset();
    m_op.reset();
}

DragDropManager& DragDrop() {
    static DragDropManager g;
    return g;
}

} // namespace uz::ui
