#pragma once

/// Bounded undo / redo stack for the VFX editor (phase 11.9.0).
///
/// Two deques:
///   - `m_undo` holds applied commands ordered oldest-to-newest (tail
///     is the most recent command). Capped at `kCapacity`; the oldest
///     entry is dropped when the cap is exceeded.
///   - `m_redo` holds reverted commands ordered by recency of revert
///     (tail is the next candidate for `Redo`). Cleared whenever a new
///     command is `Push`ed — a divergent edit abandons the redo
///     branch.
///
/// The stack takes ownership of every command via `std::unique_ptr`.
/// Commands must document whether `Push` should call `Apply()` or
/// whether the caller has already applied the mutation. The provided
/// convention is **caller applies first, then pushes** — this keeps the
/// failure path simple (if the mutation threw, nothing lands on the
/// stack).
///
/// # Non-goals
///
/// - No transactional grouping (no "Begin / End" batch). Callers can
///   implement batching later via a composite `MultiCommand` wrapper.
/// - Not thread-safe.
/// - No serialisation — the stack lives only while the editor session
///   is open.

#include "vfx_undo_command.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <string>

namespace uz {

class VfxUndoStack {
public:
    /// Maximum number of retained undoable commands. Matches the task
    /// spec (100). Older commands are dropped when the cap is exceeded.
    static constexpr size_t kCapacity = 100u;

    VfxUndoStack() = default;

    /// Pushes an already-applied command onto the undo stack. Clears the
    /// redo branch (a new edit invalidates the future). When the stack
    /// is at `kCapacity`, the oldest entry is dropped.
    void Push(std::unique_ptr<IVfxUndoCommand> cmd);

    /// Reverts the most recent command and moves it to the redo stack.
    /// Returns the command's name on success, empty string when nothing
    /// is available to undo.
    std::string Undo();

    /// Re-applies the most recently undone command and moves it back to
    /// the undo stack. Returns the command's name on success, empty
    /// string when nothing is available to redo.
    std::string Redo();

    /// Clears both undo + redo stacks. Typically called on "New" /
    /// "Load Asset" flows.
    void Clear() noexcept;

    /// Drops only the redo branch (e.g. on "Save" — keep history, drop
    /// the speculative future).
    void ClearRedoTail() noexcept;

    [[nodiscard]] bool   CanUndo() const noexcept { return !m_undo.empty(); }
    [[nodiscard]] bool   CanRedo() const noexcept { return !m_redo.empty(); }
    [[nodiscard]] size_t UndoDepth() const noexcept { return m_undo.size(); }
    [[nodiscard]] size_t RedoDepth() const noexcept { return m_redo.size(); }

    /// Returns the name of the top of the undo stack (empty when empty).
    [[nodiscard]] std::string NextUndoName() const;

    /// Returns the name of the top of the redo stack (empty when empty).
    [[nodiscard]] std::string NextRedoName() const;

private:
    std::deque<std::unique_ptr<IVfxUndoCommand>> m_undo;
    std::deque<std::unique_ptr<IVfxUndoCommand>> m_redo;
};

} // namespace uz
