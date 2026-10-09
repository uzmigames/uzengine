/// VfxUndoStack — implementation.

#include "vfx_undo_stack.h"

namespace uz {

void VfxUndoStack::Push(std::unique_ptr<IVfxUndoCommand> cmd) {
    if (cmd == nullptr) return;
    // New mutation invalidates any pending redo branch.
    m_redo.clear();
    m_undo.push_back(std::move(cmd));
    // Drop the oldest to honour the capacity bound.
    while (m_undo.size() > kCapacity) {
        m_undo.pop_front();
    }
}

std::string VfxUndoStack::Undo() {
    if (m_undo.empty()) return {};
    std::unique_ptr<IVfxUndoCommand> cmd = std::move(m_undo.back());
    m_undo.pop_back();
    std::string name = cmd->Name();
    cmd->Revert();
    m_redo.push_back(std::move(cmd));
    return name;
}

std::string VfxUndoStack::Redo() {
    if (m_redo.empty()) return {};
    std::unique_ptr<IVfxUndoCommand> cmd = std::move(m_redo.back());
    m_redo.pop_back();
    std::string name = cmd->Name();
    cmd->Apply();
    m_undo.push_back(std::move(cmd));
    return name;
}

void VfxUndoStack::Clear() noexcept {
    m_undo.clear();
    m_redo.clear();
}

void VfxUndoStack::ClearRedoTail() noexcept {
    m_redo.clear();
}

std::string VfxUndoStack::NextUndoName() const {
    if (m_undo.empty()) return {};
    return m_undo.back()->Name();
}

std::string VfxUndoStack::NextRedoName() const {
    if (m_redo.empty()) return {};
    return m_redo.back()->Name();
}

} // namespace uz
