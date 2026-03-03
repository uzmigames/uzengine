#pragma once

// undo_redo.h — Command-pattern undo/redo stack.
//
// Commands are heap-allocated objects derived from ICommand. The UndoStack owns
// them via unique_ptr. Max stack depth is configurable (default 256).
// Executing a new command while the redo stack has entries clears the redo stack.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace uz {

// ============================================================================
// ICommand — abstract command interface
// ============================================================================

struct ICommand {
    virtual ~ICommand() = default;

    /// Apply the command's effect.
    virtual void execute() = 0;

    /// Reverse the command's effect.
    virtual void undo() = 0;

    /// Attempt to merge with the previous command of the same type (e.g.
    /// repeated text edits). Returns true if merged — the caller should then
    /// discard `prev` and keep only `this`. Default: no merge.
    virtual bool try_merge(ICommand* /*prev*/) { return false; }

    /// Human-readable name for UI display (e.g. "Set Color", "Move Object").
    [[nodiscard]] virtual const char* name() const = 0;
};

// ============================================================================
// UndoStack — bounded undo/redo stack with command grouping
// ============================================================================

class UndoStack {
public:
    static constexpr uint32_t kDefaultMaxDepth = 256;

    explicit UndoStack(uint32_t max_depth = kDefaultMaxDepth);
    ~UndoStack();

    // Non-copyable, movable.
    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;
    UndoStack(UndoStack&&) noexcept = default;
    UndoStack& operator=(UndoStack&&) noexcept = default;

    /// Execute a command and push it onto the undo stack.
    /// Clears the redo stack. If stack is full (>= max_depth), drops the oldest
    /// entry. If inside a group, accumulates into the group instead.
    void execute(std::unique_ptr<ICommand> cmd);

    /// Undo the last command. No-op if undo stack is empty.
    /// Returns true if a command was undone.
    bool undo();

    /// Redo the last undone command. No-op if redo stack is empty.
    /// Returns true if a command was redone.
    bool redo();

    /// Clear both undo and redo stacks.
    void clear();

    // -- Stack depth queries ------------------------------------------------

    [[nodiscard]] uint32_t undo_depth() const;
    [[nodiscard]] uint32_t redo_depth() const;
    [[nodiscard]] bool     can_undo()   const;
    [[nodiscard]] bool     can_redo()   const;
    [[nodiscard]] uint32_t max_depth()  const;

    // -- Peek ---------------------------------------------------------------

    /// Peek at the top of the undo stack (for UI display).
    /// Returns nullptr if the stack is empty.
    [[nodiscard]] const ICommand* peek_undo() const;

    /// Peek at the top of the redo stack (for UI display).
    /// Returns nullptr if the stack is empty.
    [[nodiscard]] const ICommand* peek_redo() const;

    // -- Command grouping ---------------------------------------------------

    /// Begin a group — subsequent execute() calls accumulate into a single
    /// undoable unit until end_group(). Nested groups are NOT supported
    /// (begin_group while already in a group fires an assert).
    void begin_group(const char* group_name);

    /// End the current group. Creates a single composite command on the undo
    /// stack. If no commands were executed in the group, nothing is pushed.
    void end_group();

    /// True if currently inside a begin_group() / end_group() bracket.
    [[nodiscard]] bool in_group() const;

private:
    uint32_t max_depth_;
    std::vector<std::unique_ptr<ICommand>> undo_stack_;
    std::vector<std::unique_ptr<ICommand>> redo_stack_;

    // Group state
    bool        in_group_ = false;
    std::string group_name_;
    std::vector<std::unique_ptr<ICommand>> group_cmds_;

    // Internal: push a command onto the undo stack, enforcing max_depth_.
    void push_undo(std::unique_ptr<ICommand> cmd);
};

} // namespace uz
