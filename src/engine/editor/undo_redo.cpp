// undo_redo.cpp — Command-pattern undo/redo stack implementation.

#include "engine/editor/undo_redo.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace uz {

// ============================================================================
// GroupCommand — internal composite command for begin_group/end_group
// ============================================================================

namespace {

/// Holds a batch of commands that should be executed/undone as a single unit.
/// execute() replays all in forward order; undo() reverses them.
class GroupCommand final : public ICommand {
public:
    GroupCommand(std::string name, std::vector<std::unique_ptr<ICommand>> cmds)
        : name_(std::move(name))
        , cmds_(std::move(cmds))
    {}

    void execute() override {
        for (auto& cmd : cmds_) {
            cmd->execute();
        }
    }

    void undo() override {
        // Undo in reverse order — last applied is first undone.
        for (auto it = cmds_.rbegin(); it != cmds_.rend(); ++it) {
            (*it)->undo();
        }
    }

    [[nodiscard]] const char* name() const override {
        return name_.c_str();
    }

private:
    std::string name_;
    std::vector<std::unique_ptr<ICommand>> cmds_;
};

} // anonymous namespace

// ============================================================================
// UndoStack — construction / destruction
// ============================================================================

UndoStack::UndoStack(uint32_t max_depth)
    : max_depth_(max_depth)
{
    // A max_depth of 0 makes no sense — treat it as 1 to avoid surprises.
    if (max_depth_ == 0) {
        max_depth_ = 1;
    }
}

UndoStack::~UndoStack() = default;

// ============================================================================
// UndoStack::execute
// ============================================================================

void UndoStack::execute(std::unique_ptr<ICommand> cmd) {
    assert(cmd && "UndoStack::execute() called with nullptr command");

    // Apply the command's effect.
    cmd->execute();

    if (in_group_) {
        // Accumulate into the group batch — redo stack is cleared on end_group
        // push, not here.
        group_cmds_.push_back(std::move(cmd));
        return;
    }

    // Try to merge with the top of the undo stack.
    if (!undo_stack_.empty() && cmd->try_merge(undo_stack_.back().get())) {
        // Merge succeeded — replace the top command with the merged one.
        undo_stack_.back() = std::move(cmd);
    } else {
        push_undo(std::move(cmd));
    }

    // Executing a new command invalidates any redo history.
    redo_stack_.clear();
}

// ============================================================================
// UndoStack::undo
// ============================================================================

bool UndoStack::undo() {
    if (undo_stack_.empty()) {
        return false;
    }

    auto cmd = std::move(undo_stack_.back());
    undo_stack_.pop_back();

    cmd->undo();

    redo_stack_.push_back(std::move(cmd));
    return true;
}

// ============================================================================
// UndoStack::redo
// ============================================================================

bool UndoStack::redo() {
    if (redo_stack_.empty()) {
        return false;
    }

    auto cmd = std::move(redo_stack_.back());
    redo_stack_.pop_back();

    cmd->execute();

    // Push back to undo stack (no max-depth check — it was already counted).
    undo_stack_.push_back(std::move(cmd));
    return true;
}

// ============================================================================
// UndoStack::clear
// ============================================================================

void UndoStack::clear() {
    undo_stack_.clear();
    redo_stack_.clear();
}

// ============================================================================
// Stack depth queries
// ============================================================================

uint32_t UndoStack::undo_depth() const {
    return static_cast<uint32_t>(undo_stack_.size());
}

uint32_t UndoStack::redo_depth() const {
    return static_cast<uint32_t>(redo_stack_.size());
}

bool UndoStack::can_undo() const {
    return !undo_stack_.empty();
}

bool UndoStack::can_redo() const {
    return !redo_stack_.empty();
}

uint32_t UndoStack::max_depth() const {
    return max_depth_;
}

// ============================================================================
// Peek
// ============================================================================

const ICommand* UndoStack::peek_undo() const {
    if (undo_stack_.empty()) {
        return nullptr;
    }
    return undo_stack_.back().get();
}

const ICommand* UndoStack::peek_redo() const {
    if (redo_stack_.empty()) {
        return nullptr;
    }
    return redo_stack_.back().get();
}

// ============================================================================
// Command grouping
// ============================================================================

void UndoStack::begin_group(const char* group_name) {
    assert(!in_group_ && "UndoStack::begin_group() called while already in a group");
    in_group_ = true;
    group_name_ = group_name ? group_name : "";
    group_cmds_.clear();
}

void UndoStack::end_group() {
    assert(in_group_ && "UndoStack::end_group() called without matching begin_group()");
    in_group_ = false;

    if (group_cmds_.empty()) {
        // Empty group — nothing to push.
        group_name_.clear();
        return;
    }

    // Wrap the accumulated commands into a single GroupCommand.
    auto group = std::make_unique<GroupCommand>(
        std::move(group_name_), std::move(group_cmds_));

    push_undo(std::move(group));

    // Executing grouped commands invalidates redo history.
    redo_stack_.clear();
}

bool UndoStack::in_group() const {
    return in_group_;
}

// ============================================================================
// Internal helpers
// ============================================================================

void UndoStack::push_undo(std::unique_ptr<ICommand> cmd) {
    // Enforce max depth — drop the oldest command if at capacity.
    if (static_cast<uint32_t>(undo_stack_.size()) >= max_depth_) {
        undo_stack_.erase(undo_stack_.begin());
    }
    undo_stack_.push_back(std::move(cmd));
}

} // namespace uz
