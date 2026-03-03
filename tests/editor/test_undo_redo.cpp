// test_undo_redo.cpp — 100% branch + line coverage of undo_redo.h / undo_redo.cpp
//
// Covers: ICommand interface, UndoStack::execute, undo, redo, clear,
//         peek_undo/peek_redo, max_depth enforcement, try_merge,
//         begin_group/end_group, integration sequences.

#include <gtest/gtest.h>
#include "engine/editor/undo_redo.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace uz;

// ============================================================================
// Test command helpers
// ============================================================================

/// Concrete command that sets an int reference to a new value and stores the
/// old value for undo. This is the workhorse test double used throughout.
class SetValueCommand final : public ICommand {
public:
    SetValueCommand(int& target, int new_value)
        : target_(target)
        , new_value_(new_value)
        , old_value_(target)
    {}

    void execute() override { target_ = new_value_; }
    void undo()    override { target_ = old_value_; }

    [[nodiscard]] const char* name() const override { return "SetValue"; }

private:
    int& target_;
    int  new_value_;
    int  old_value_;
};

/// Command that tracks how many times execute() and undo() have been called.
class CountingCommand final : public ICommand {
public:
    int execute_count = 0;
    int undo_count    = 0;

    void execute() override { ++execute_count; }
    void undo()    override { ++undo_count; }

    [[nodiscard]] const char* name() const override { return "Counting"; }
};

/// Command that always merges with the previous one — used to test try_merge.
class MergeableCommand final : public ICommand {
public:
    MergeableCommand(int& target, int new_value)
        : target_(target)
        , new_value_(new_value)
        , old_value_(target)
    {}

    void execute() override { target_ = new_value_; }
    void undo()    override { target_ = old_value_; }

    bool try_merge(ICommand* prev) override {
        // Merge if the previous is also a MergeableCommand: absorb its old_value_.
        if (auto* p = dynamic_cast<MergeableCommand*>(prev)) {
            old_value_ = p->old_value_;
            return true;
        }
        return false;
    }

    [[nodiscard]] const char* name() const override { return "Mergeable"; }

private:
    int& target_;
    int  new_value_;
    int  old_value_;
};

/// Command whose try_merge always returns false (explicit non-mergeable).
class NonMergeableCommand final : public ICommand {
public:
    NonMergeableCommand(int& target, int new_value)
        : target_(target)
        , new_value_(new_value)
        , old_value_(target)
    {}

    void execute() override { target_ = new_value_; }
    void undo()    override { target_ = old_value_; }

    bool try_merge(ICommand* /*prev*/) override { return false; }

    [[nodiscard]] const char* name() const override { return "NonMergeable"; }

private:
    int& target_;
    int  new_value_;
    int  old_value_;
};

/// Command with a custom name for peek tests.
class NamedCommand final : public ICommand {
public:
    explicit NamedCommand(const char* cmd_name) : name_(cmd_name) {}

    void execute() override {}
    void undo()    override {}

    [[nodiscard]] const char* name() const override { return name_; }

private:
    const char* name_;
};

// ============================================================================
// ICommand interface
// ============================================================================

TEST(ICommand, ExecuteAppliesChange) {
    int val = 10;
    SetValueCommand cmd(val, 42);
    cmd.execute();
    EXPECT_EQ(val, 42);
}

TEST(ICommand, UndoRestoresOriginalValue) {
    int val = 10;
    SetValueCommand cmd(val, 42);
    cmd.execute();
    cmd.undo();
    EXPECT_EQ(val, 10);
}

TEST(ICommand, NameReturnsExpectedString) {
    int val = 0;
    SetValueCommand cmd(val, 1);
    EXPECT_STREQ(cmd.name(), "SetValue");
}

TEST(ICommand, TryMergeDefaultReturnsFalse) {
    int val = 0;
    SetValueCommand cmd1(val, 1);
    SetValueCommand cmd2(val, 2);
    EXPECT_FALSE(cmd2.try_merge(&cmd1));
}

// ============================================================================
// UndoStack construction
// ============================================================================

TEST(UndoStackConstruction, DefaultMaxDepthIs256) {
    UndoStack stack;
    EXPECT_EQ(stack.max_depth(), UndoStack::kDefaultMaxDepth);
    EXPECT_EQ(stack.max_depth(), 256u);
}

TEST(UndoStackConstruction, CustomMaxDepth) {
    UndoStack stack(128);
    EXPECT_EQ(stack.max_depth(), 128u);
}

TEST(UndoStackConstruction, ZeroMaxDepthClampedTo1) {
    // A max_depth of 0 is nonsensical — implementation clamps to 1.
    UndoStack stack(0);
    EXPECT_EQ(stack.max_depth(), 1u);
}

TEST(UndoStackConstruction, InitialStateIsEmpty) {
    UndoStack stack;
    EXPECT_EQ(stack.undo_depth(), 0u);
    EXPECT_EQ(stack.redo_depth(), 0u);
    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());
    EXPECT_FALSE(stack.in_group());
}

// ============================================================================
// UndoStack::execute
// ============================================================================

TEST(UndoStackExecute, ExecuteIncrementsUndoDepth) {
    UndoStack stack;
    int val = 0;
    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    EXPECT_EQ(stack.undo_depth(), 1u);
}

TEST(UndoStackExecute, ExecuteCallsCommandExecute) {
    UndoStack stack;
    int val = 10;
    stack.execute(std::make_unique<SetValueCommand>(val, 42));
    EXPECT_EQ(val, 42);
}

TEST(UndoStackExecute, ExecuteClearsRedoStack) {
    UndoStack stack;
    int val = 0;
    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.undo();
    EXPECT_EQ(stack.redo_depth(), 1u);

    // New execute must clear redo.
    stack.execute(std::make_unique<SetValueCommand>(val, 2));
    EXPECT_EQ(stack.redo_depth(), 0u);
}

TEST(UndoStackExecute, MultipleExecutesIncrementDepth) {
    UndoStack stack;
    int val = 0;
    for (int i = 1; i <= 5; ++i) {
        stack.execute(std::make_unique<SetValueCommand>(val, i));
    }
    EXPECT_EQ(stack.undo_depth(), 5u);
    EXPECT_EQ(val, 5);
}

TEST(UndoStackExecute, CanUndoIsTrueAfterExecute) {
    UndoStack stack;
    int val = 0;
    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    EXPECT_TRUE(stack.can_undo());
}

// ============================================================================
// Max depth enforcement
// ============================================================================

TEST(UndoStackMaxDepth, OldestCommandDroppedWhenFull) {
    constexpr uint32_t kMax = 3;
    UndoStack stack(kMax);
    int val = 0;

    // Push kMax+1 commands — oldest should be dropped.
    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.execute(std::make_unique<SetValueCommand>(val, 2));
    stack.execute(std::make_unique<SetValueCommand>(val, 3));
    stack.execute(std::make_unique<SetValueCommand>(val, 4));

    EXPECT_EQ(stack.undo_depth(), kMax);
    EXPECT_EQ(val, 4);
}

TEST(UndoStackMaxDepth, DepthNeverExceedsMax) {
    constexpr uint32_t kMax = 2;
    UndoStack stack(kMax);
    int val = 0;

    for (int i = 0; i < 100; ++i) {
        stack.execute(std::make_unique<SetValueCommand>(val, i));
    }
    EXPECT_EQ(stack.undo_depth(), kMax);
}

TEST(UndoStackMaxDepth, MaxDepthOfOneKeepsOnlyLastCommand) {
    UndoStack stack(1);
    int val = 0;

    stack.execute(std::make_unique<SetValueCommand>(val, 10));
    stack.execute(std::make_unique<SetValueCommand>(val, 20));

    EXPECT_EQ(stack.undo_depth(), 1u);

    // Undo should revert to the state before command 2 (val was 10).
    stack.undo();
    EXPECT_EQ(val, 10);
}

// ============================================================================
// try_merge
// ============================================================================

TEST(UndoStackMerge, MergeableCommandsStayAtDepth1) {
    UndoStack stack;
    int val = 0;

    stack.execute(std::make_unique<MergeableCommand>(val, 10));
    stack.execute(std::make_unique<MergeableCommand>(val, 20));
    stack.execute(std::make_unique<MergeableCommand>(val, 30));

    // All three merged — depth stays at 1.
    EXPECT_EQ(stack.undo_depth(), 1u);
    EXPECT_EQ(val, 30);
}

TEST(UndoStackMerge, UndoAfterMergeRestoresToOriginal) {
    UndoStack stack;
    int val = 0;

    stack.execute(std::make_unique<MergeableCommand>(val, 10));
    stack.execute(std::make_unique<MergeableCommand>(val, 20));

    // Undo should restore to original (0), not intermediate (10).
    stack.undo();
    EXPECT_EQ(val, 0);
}

TEST(UndoStackMerge, NonMergeableCommandsDoNotMerge) {
    UndoStack stack;
    int val = 0;

    stack.execute(std::make_unique<NonMergeableCommand>(val, 10));
    stack.execute(std::make_unique<NonMergeableCommand>(val, 20));

    EXPECT_EQ(stack.undo_depth(), 2u);
}

TEST(UndoStackMerge, MergeDoesNotMergeWithDifferentType) {
    UndoStack stack;
    int val = 0;

    // Push a SetValueCommand, then a MergeableCommand — different types, no merge.
    stack.execute(std::make_unique<SetValueCommand>(val, 10));
    stack.execute(std::make_unique<MergeableCommand>(val, 20));

    EXPECT_EQ(stack.undo_depth(), 2u);
}

// ============================================================================
// UndoStack::undo
// ============================================================================

TEST(UndoStackUndo, UndoDecrementsUndoDepthAndIncrementsRedoDepth) {
    UndoStack stack;
    int val = 0;
    stack.execute(std::make_unique<SetValueCommand>(val, 1));

    stack.undo();

    EXPECT_EQ(stack.undo_depth(), 0u);
    EXPECT_EQ(stack.redo_depth(), 1u);
}

TEST(UndoStackUndo, UndoCallsCommandUndo) {
    UndoStack stack;
    int val = 10;
    stack.execute(std::make_unique<SetValueCommand>(val, 42));
    ASSERT_EQ(val, 42);

    stack.undo();
    EXPECT_EQ(val, 10);
}

TEST(UndoStackUndo, UndoOnEmptyStackReturnsFalse) {
    UndoStack stack;
    EXPECT_FALSE(stack.undo());
}

TEST(UndoStackUndo, UndoOnEmptyStackDoesNotCrash) {
    UndoStack stack;
    // Should be a safe no-op.
    EXPECT_FALSE(stack.undo());
    EXPECT_EQ(stack.undo_depth(), 0u);
    EXPECT_EQ(stack.redo_depth(), 0u);
}

TEST(UndoStackUndo, CanUndoIsFalseAfterUndoingAll) {
    UndoStack stack;
    int val = 0;
    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.undo();
    EXPECT_FALSE(stack.can_undo());
}

TEST(UndoStackUndo, MultipleUndosRestoreInReverseOrder) {
    UndoStack stack;
    int val = 0;

    stack.execute(std::make_unique<SetValueCommand>(val, 10));
    stack.execute(std::make_unique<SetValueCommand>(val, 20));
    stack.execute(std::make_unique<SetValueCommand>(val, 30));

    stack.undo();
    EXPECT_EQ(val, 20);

    stack.undo();
    EXPECT_EQ(val, 10);

    stack.undo();
    EXPECT_EQ(val, 0);
}

// ============================================================================
// UndoStack::redo
// ============================================================================

TEST(UndoStackRedo, RedoAfterUndoRestoresState) {
    UndoStack stack;
    int val = 0;
    stack.execute(std::make_unique<SetValueCommand>(val, 42));

    stack.undo();
    ASSERT_EQ(val, 0);

    stack.redo();
    EXPECT_EQ(val, 42);
}

TEST(UndoStackRedo, RedoOnEmptyRedoStackReturnsFalse) {
    UndoStack stack;
    EXPECT_FALSE(stack.redo());
}

TEST(UndoStackRedo, RedoOnEmptyRedoStackDoesNotCrash) {
    UndoStack stack;
    EXPECT_FALSE(stack.redo());
    EXPECT_EQ(stack.undo_depth(), 0u);
    EXPECT_EQ(stack.redo_depth(), 0u);
}

TEST(UndoStackRedo, RedoIsClearedOnNewExecute) {
    UndoStack stack;
    int val = 0;

    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.execute(std::make_unique<SetValueCommand>(val, 2));
    stack.undo(); // redo has 1 entry
    ASSERT_EQ(stack.redo_depth(), 1u);

    // New execute clears redo.
    stack.execute(std::make_unique<SetValueCommand>(val, 3));
    EXPECT_EQ(stack.redo_depth(), 0u);
    EXPECT_FALSE(stack.can_redo());
}

TEST(UndoStackRedo, RedoReturnsTrue) {
    UndoStack stack;
    int val = 0;
    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.undo();
    EXPECT_TRUE(stack.redo());
}

TEST(UndoStackRedo, CanRedoIsTrueAfterUndo) {
    UndoStack stack;
    int val = 0;
    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.undo();
    EXPECT_TRUE(stack.can_redo());
}

TEST(UndoStackRedo, CanRedoIsFalseAfterRedoAll) {
    UndoStack stack;
    int val = 0;
    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.undo();
    stack.redo();
    EXPECT_FALSE(stack.can_redo());
}

TEST(UndoStackRedo, MultipleRedosReapplyInForwardOrder) {
    UndoStack stack;
    int val = 0;

    stack.execute(std::make_unique<SetValueCommand>(val, 10));
    stack.execute(std::make_unique<SetValueCommand>(val, 20));
    stack.execute(std::make_unique<SetValueCommand>(val, 30));

    stack.undo();
    stack.undo();
    stack.undo();
    ASSERT_EQ(val, 0);

    stack.redo();
    EXPECT_EQ(val, 10);

    stack.redo();
    EXPECT_EQ(val, 20);

    stack.redo();
    EXPECT_EQ(val, 30);
}

// ============================================================================
// peek_undo / peek_redo
// ============================================================================

TEST(UndoStackPeek, PeekUndoReturnsCorrectCommandName) {
    UndoStack stack;
    stack.execute(std::make_unique<NamedCommand>("Paint"));
    stack.execute(std::make_unique<NamedCommand>("Move"));

    auto* top = stack.peek_undo();
    ASSERT_NE(top, nullptr);
    EXPECT_STREQ(top->name(), "Move");
}

TEST(UndoStackPeek, PeekUndoReturnsNullptrWhenEmpty) {
    UndoStack stack;
    EXPECT_EQ(stack.peek_undo(), nullptr);
}

TEST(UndoStackPeek, PeekRedoReturnsCorrectCommandName) {
    UndoStack stack;
    stack.execute(std::make_unique<NamedCommand>("Paint"));
    stack.execute(std::make_unique<NamedCommand>("Move"));
    stack.undo();

    auto* top = stack.peek_redo();
    ASSERT_NE(top, nullptr);
    EXPECT_STREQ(top->name(), "Move");
}

TEST(UndoStackPeek, PeekRedoReturnsNullptrWhenEmpty) {
    UndoStack stack;
    EXPECT_EQ(stack.peek_redo(), nullptr);
}

TEST(UndoStackPeek, PeekUndoAfterUndoShowsPreviousCommand) {
    UndoStack stack;
    stack.execute(std::make_unique<NamedCommand>("A"));
    stack.execute(std::make_unique<NamedCommand>("B"));
    stack.undo();

    auto* top = stack.peek_undo();
    ASSERT_NE(top, nullptr);
    EXPECT_STREQ(top->name(), "A");
}

// ============================================================================
// UndoStack::clear
// ============================================================================

TEST(UndoStackClear, ClearEmptiesBothStacks) {
    UndoStack stack;
    int val = 0;

    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.execute(std::make_unique<SetValueCommand>(val, 2));
    stack.undo(); // 1 on undo, 1 on redo

    stack.clear();

    EXPECT_EQ(stack.undo_depth(), 0u);
    EXPECT_EQ(stack.redo_depth(), 0u);
}

TEST(UndoStackClear, CanUndoAndCanRedoAreFalseAfterClear) {
    UndoStack stack;
    int val = 0;

    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.undo();

    stack.clear();

    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());
}

TEST(UndoStackClear, ClearOnEmptyStackIsNoOp) {
    UndoStack stack;
    stack.clear();
    EXPECT_EQ(stack.undo_depth(), 0u);
    EXPECT_EQ(stack.redo_depth(), 0u);
}

TEST(UndoStackClear, PeekReturnsNullptrAfterClear) {
    UndoStack stack;
    int val = 0;
    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.undo();

    stack.clear();

    EXPECT_EQ(stack.peek_undo(), nullptr);
    EXPECT_EQ(stack.peek_redo(), nullptr);
}

// ============================================================================
// Command grouping
// ============================================================================

TEST(UndoStackGroup, GroupOf3CommandsHasUndoDepth1) {
    UndoStack stack;
    int val = 0;

    stack.begin_group("batch");
    stack.execute(std::make_unique<SetValueCommand>(val, 10));
    stack.execute(std::make_unique<SetValueCommand>(val, 20));
    stack.execute(std::make_unique<SetValueCommand>(val, 30));
    stack.end_group();

    EXPECT_EQ(stack.undo_depth(), 1u);
    EXPECT_EQ(val, 30);
}

TEST(UndoStackGroup, UndoGroupUndoesAllInReverse) {
    UndoStack stack;
    int a = 0, b = 0, c = 0;

    stack.begin_group("triple-set");
    stack.execute(std::make_unique<SetValueCommand>(a, 1));
    stack.execute(std::make_unique<SetValueCommand>(b, 2));
    stack.execute(std::make_unique<SetValueCommand>(c, 3));
    stack.end_group();

    stack.undo();

    // All three should be reverted.
    EXPECT_EQ(a, 0);
    EXPECT_EQ(b, 0);
    EXPECT_EQ(c, 0);
}

TEST(UndoStackGroup, RedoGroupRedoesAllInForwardOrder) {
    UndoStack stack;
    int a = 0, b = 0, c = 0;

    stack.begin_group("triple-set");
    stack.execute(std::make_unique<SetValueCommand>(a, 1));
    stack.execute(std::make_unique<SetValueCommand>(b, 2));
    stack.execute(std::make_unique<SetValueCommand>(c, 3));
    stack.end_group();

    stack.undo();
    stack.redo();

    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 2);
    EXPECT_EQ(c, 3);
}

TEST(UndoStackGroup, InGroupIsTrueInsideFalseOutside) {
    UndoStack stack;
    EXPECT_FALSE(stack.in_group());

    stack.begin_group("test");
    EXPECT_TRUE(stack.in_group());

    stack.end_group();
    EXPECT_FALSE(stack.in_group());
}

TEST(UndoStackGroup, EmptyGroupPushesNothing) {
    UndoStack stack;

    stack.begin_group("empty");
    stack.end_group();

    EXPECT_EQ(stack.undo_depth(), 0u);
    EXPECT_FALSE(stack.can_undo());
}

TEST(UndoStackGroup, GroupCommandNameMatchesGroupName) {
    UndoStack stack;
    int val = 0;

    stack.begin_group("My Group");
    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.end_group();

    auto* top = stack.peek_undo();
    ASSERT_NE(top, nullptr);
    EXPECT_STREQ(top->name(), "My Group");
}

TEST(UndoStackGroup, GroupWithNullNameUsesEmptyString) {
    UndoStack stack;
    int val = 0;

    stack.begin_group(nullptr);
    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.end_group();

    auto* top = stack.peek_undo();
    ASSERT_NE(top, nullptr);
    EXPECT_STREQ(top->name(), "");
}

TEST(UndoStackGroup, GroupClearsRedoStack) {
    UndoStack stack;
    int val = 0;

    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.undo();
    ASSERT_EQ(stack.redo_depth(), 1u);

    // Group pushes a new command — should clear redo.
    stack.begin_group("batch");
    stack.execute(std::make_unique<SetValueCommand>(val, 2));
    stack.end_group();

    EXPECT_EQ(stack.redo_depth(), 0u);
}

TEST(UndoStackGroup, GroupRespectMaxDepth) {
    UndoStack stack(2);
    int val = 0;

    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.execute(std::make_unique<SetValueCommand>(val, 2));

    // Stack is full (2). Group push should drop oldest.
    stack.begin_group("batch");
    stack.execute(std::make_unique<SetValueCommand>(val, 3));
    stack.end_group();

    EXPECT_EQ(stack.undo_depth(), 2u);
}

// ============================================================================
// Nested group assert (debug builds only)
// ============================================================================

#ifdef UZENGINE_DEBUG
TEST(UndoStackGroupDebug, NestedBeginGroupAssertsInDebug) {
    UndoStack stack;
    stack.begin_group("outer");
    EXPECT_DEATH(stack.begin_group("inner"), "");
}

TEST(UndoStackGroupDebug, EndGroupWithoutBeginAssertsInDebug) {
    UndoStack stack;
    EXPECT_DEATH(stack.end_group(), "");
}
#endif

// ============================================================================
// Move semantics
// ============================================================================

TEST(UndoStackMove, MoveConstructionTransfersState) {
    UndoStack stack1;
    int val = 0;
    stack1.execute(std::make_unique<SetValueCommand>(val, 42));

    UndoStack stack2(std::move(stack1));

    EXPECT_EQ(stack2.undo_depth(), 1u);
    EXPECT_TRUE(stack2.can_undo());
}

TEST(UndoStackMove, MoveAssignmentTransfersState) {
    UndoStack stack1;
    int val = 0;
    stack1.execute(std::make_unique<SetValueCommand>(val, 42));

    UndoStack stack2;
    stack2 = std::move(stack1);

    EXPECT_EQ(stack2.undo_depth(), 1u);
    EXPECT_TRUE(stack2.can_undo());
}

// ============================================================================
// Integration tests — complex undo/redo sequences
// ============================================================================

TEST(UndoStackIntegration, FiveCommandsUndo2Redo1NewCommandClearsRedo) {
    UndoStack stack;
    int val = 0;

    // Execute 5 commands: val goes 0 -> 1 -> 2 -> 3 -> 4 -> 5
    for (int i = 1; i <= 5; ++i) {
        stack.execute(std::make_unique<SetValueCommand>(val, i));
    }
    ASSERT_EQ(val, 5);
    ASSERT_EQ(stack.undo_depth(), 5u);

    // Undo 2: val goes 5 -> 4 -> 3
    stack.undo();
    stack.undo();
    EXPECT_EQ(val, 3);
    EXPECT_EQ(stack.undo_depth(), 3u);
    EXPECT_EQ(stack.redo_depth(), 2u);

    // Redo 1: val goes 3 -> 4
    stack.redo();
    EXPECT_EQ(val, 4);
    EXPECT_EQ(stack.undo_depth(), 4u);
    EXPECT_EQ(stack.redo_depth(), 1u);

    // Execute new command: val -> 99, redo cleared
    stack.execute(std::make_unique<SetValueCommand>(val, 99));
    EXPECT_EQ(val, 99);
    EXPECT_EQ(stack.undo_depth(), 5u);
    EXPECT_EQ(stack.redo_depth(), 0u);
}

TEST(UndoStackIntegration, UndoRedoUndoRedoCycle) {
    UndoStack stack;
    int val = 0;

    stack.execute(std::make_unique<SetValueCommand>(val, 10));
    stack.execute(std::make_unique<SetValueCommand>(val, 20));

    // Cycle: undo -> redo -> undo -> redo
    stack.undo();
    EXPECT_EQ(val, 10);

    stack.redo();
    EXPECT_EQ(val, 20);

    stack.undo();
    EXPECT_EQ(val, 10);

    stack.redo();
    EXPECT_EQ(val, 20);

    EXPECT_EQ(stack.undo_depth(), 2u);
    EXPECT_EQ(stack.redo_depth(), 0u);
}

TEST(UndoStackIntegration, UndoAllThenRedoAll) {
    UndoStack stack;
    int val = 0;

    for (int i = 1; i <= 5; ++i) {
        stack.execute(std::make_unique<SetValueCommand>(val, i * 10));
    }

    // Undo all
    while (stack.can_undo()) {
        stack.undo();
    }
    EXPECT_EQ(val, 0);
    EXPECT_EQ(stack.undo_depth(), 0u);
    EXPECT_EQ(stack.redo_depth(), 5u);

    // Redo all
    while (stack.can_redo()) {
        stack.redo();
    }
    EXPECT_EQ(val, 50);
    EXPECT_EQ(stack.undo_depth(), 5u);
    EXPECT_EQ(stack.redo_depth(), 0u);
}

TEST(UndoStackIntegration, GroupInsideLargerSequence) {
    UndoStack stack;
    int val = 0;

    // Single command: val = 1
    stack.execute(std::make_unique<SetValueCommand>(val, 1));

    // Group: val = 2, then 3
    stack.begin_group("batch");
    stack.execute(std::make_unique<SetValueCommand>(val, 2));
    stack.execute(std::make_unique<SetValueCommand>(val, 3));
    stack.end_group();

    // Single command: val = 4
    stack.execute(std::make_unique<SetValueCommand>(val, 4));

    // Undo depths: [cmd(1), group(2->3), cmd(4)]
    EXPECT_EQ(stack.undo_depth(), 3u);
    EXPECT_EQ(val, 4);

    // Undo the last single command.
    stack.undo();
    EXPECT_EQ(val, 3);

    // Undo the group — should revert both commands in the group.
    stack.undo();
    EXPECT_EQ(val, 1);

    // Undo the first single command.
    stack.undo();
    EXPECT_EQ(val, 0);
}

TEST(UndoStackIntegration, MaxDepthEvictsCorrectlyWithGroup) {
    UndoStack stack(3);
    int val = 0;

    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.execute(std::make_unique<SetValueCommand>(val, 2));
    stack.execute(std::make_unique<SetValueCommand>(val, 3));
    // Stack full: [cmd(1), cmd(2), cmd(3)]

    // Group push — should evict cmd(1).
    stack.begin_group("batch");
    stack.execute(std::make_unique<SetValueCommand>(val, 4));
    stack.end_group();

    EXPECT_EQ(stack.undo_depth(), 3u);

    // Undo all — we should get back to val=1 (not 0, since cmd(1) was evicted).
    stack.undo(); // undo group -> val = 3
    stack.undo(); // undo cmd(3) -> val = 2
    stack.undo(); // undo cmd(2) -> val = 1
    EXPECT_EQ(val, 1);
    EXPECT_FALSE(stack.can_undo());
}

TEST(UndoStackIntegration, ClearThenExecuteWorksNormally) {
    UndoStack stack;
    int val = 0;

    stack.execute(std::make_unique<SetValueCommand>(val, 1));
    stack.clear();

    stack.execute(std::make_unique<SetValueCommand>(val, 99));
    EXPECT_EQ(val, 99);
    EXPECT_EQ(stack.undo_depth(), 1u);

    stack.undo();
    EXPECT_EQ(val, 1); // reverts to state at time of second execute
}

TEST(UndoStackIntegration, MergeWithinGroupDoesNotAffectGroupBehavior) {
    // Mergeable commands inside a group should merge within group_cmds_.
    // However, our design accumulates without merging inside groups (they are
    // merged only at the top-level undo_stack_). Verify group behavior remains
    // correct regardless.
    UndoStack stack;
    int val = 0;

    stack.begin_group("merge-test");
    stack.execute(std::make_unique<SetValueCommand>(val, 10));
    stack.execute(std::make_unique<SetValueCommand>(val, 20));
    stack.end_group();

    EXPECT_EQ(stack.undo_depth(), 1u);

    stack.undo();
    EXPECT_EQ(val, 0);
}

TEST(UndoStackIntegration, RedoAfterGroupUndo) {
    UndoStack stack;
    int val = 0;

    stack.begin_group("batch");
    stack.execute(std::make_unique<SetValueCommand>(val, 10));
    stack.execute(std::make_unique<SetValueCommand>(val, 20));
    stack.end_group();

    stack.undo();
    EXPECT_EQ(val, 0);

    stack.redo();
    EXPECT_EQ(val, 20);
    EXPECT_EQ(stack.undo_depth(), 1u);
}

TEST(UndoStackIntegration, CountingCommandExecuteAndUndoCalledCorrectly) {
    // Verify the exact number of execute/undo calls via a counting command.
    auto cmd = std::make_unique<CountingCommand>();
    auto* raw = cmd.get();

    UndoStack stack;
    stack.execute(std::move(cmd));
    EXPECT_EQ(raw->execute_count, 1);
    EXPECT_EQ(raw->undo_count, 0);

    stack.undo();
    EXPECT_EQ(raw->execute_count, 1);
    EXPECT_EQ(raw->undo_count, 1);

    stack.redo();
    EXPECT_EQ(raw->execute_count, 2);
    EXPECT_EQ(raw->undo_count, 1);
}
