/// Unit tests for phase 11.9.0: `VfxUndoStack` + concrete undo commands.
///
/// Covers:
///   - `VfxUndoStack` invariants: empty state, Push applies (caller-
///     applied convention, stack doesn't invoke Apply), Undo reverts
///     and moves command to redo, Redo re-applies and moves it back,
///     Push clears redo branch, Clear / ClearRedoTail, ring-buffer cap
///     drops oldest, NextUndoName / NextRedoName.
///   - Each command family round-trips through Apply/Revert so the
///     bound data returns to the pre-mutation state:
///       * StackAddModuleCommand / RemoveModule / Reorder / ModuleEnabledToggle
///       * ParameterAddCommand / Remove / Rename / TypeChange
///       * CurveKeyAddCommand / Remove / Move / InterpChange
///       * BurstAddCommand / Remove / Edit

#include <gtest/gtest.h>

#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/curves/vfx_curve.h"
#include "engine/vfx/editor/undo/vfx_undo_commands.h"
#include "engine/vfx/editor/undo/vfx_undo_stack.h"
#include "engine/vfx/parameters/vfx_attribute_store.h"
#include "engine/vfx/spawn/vfx_burst_event.h"
#include "engine/vfx/stack/vfx_stack.h"

#include <memory>
#include <string>
#include <vector>

using namespace uz;

namespace {

/// Minimal test command — flips an integer counter on Apply/Revert.
class CounterCommand final : public IVfxUndoCommand {
public:
    CounterCommand(int* slot, int delta) noexcept : m_slot(slot), m_delta(delta) {}
    void Apply() override  { if (m_slot) *m_slot += m_delta; }
    void Revert() override { if (m_slot) *m_slot -= m_delta; }
    [[nodiscard]] const char* Name() const noexcept override { return "Counter"; }

private:
    int* m_slot;
    int  m_delta;
};

VfxModuleInstance MakeMod(const char* type, const char* id) {
    VfxModuleInstance m;
    m.moduleTypeName = type;
    m.instanceId     = id;
    return m;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// VfxUndoStack core invariants
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxUndoStack, Empty_NoUndoNoRedo) {
    VfxUndoStack s;
    EXPECT_FALSE(s.CanUndo());
    EXPECT_FALSE(s.CanRedo());
    EXPECT_EQ(s.UndoDepth(), 0u);
    EXPECT_EQ(s.Undo(), "");
    EXPECT_EQ(s.Redo(), "");
}

TEST(VfxUndoStack, Push_UndoRedo_MovesBetweenStacks) {
    int state = 0;
    VfxUndoStack s;

    // Caller-applied convention: we apply first, then push.
    state += 5;
    s.Push(std::make_unique<CounterCommand>(&state, 5));
    EXPECT_EQ(state, 5);
    EXPECT_TRUE(s.CanUndo());
    EXPECT_FALSE(s.CanRedo());
    EXPECT_EQ(s.NextUndoName(), "Counter");

    EXPECT_EQ(s.Undo(), "Counter");
    EXPECT_EQ(state, 0);
    EXPECT_FALSE(s.CanUndo());
    EXPECT_TRUE(s.CanRedo());

    EXPECT_EQ(s.Redo(), "Counter");
    EXPECT_EQ(state, 5);
    EXPECT_TRUE(s.CanUndo());
    EXPECT_FALSE(s.CanRedo());
}

TEST(VfxUndoStack, Push_ClearsRedoBranch) {
    int state = 0;
    VfxUndoStack s;
    state += 1;
    s.Push(std::make_unique<CounterCommand>(&state, 1));
    state += 2;
    s.Push(std::make_unique<CounterCommand>(&state, 2));
    s.Undo();           // state = 1, redo has 1 cmd
    EXPECT_TRUE(s.CanRedo());

    // New push clears redo.
    state += 4;
    s.Push(std::make_unique<CounterCommand>(&state, 4));
    EXPECT_FALSE(s.CanRedo());
    EXPECT_EQ(state, 5);
}

TEST(VfxUndoStack, RingBuffer_DropsOldest) {
    int state = 0;
    VfxUndoStack s;
    for (size_t i = 0; i < VfxUndoStack::kCapacity + 10u; ++i) {
        state += 1;
        s.Push(std::make_unique<CounterCommand>(&state, 1));
    }
    EXPECT_EQ(s.UndoDepth(), VfxUndoStack::kCapacity);
}

TEST(VfxUndoStack, Clear_EmptiesBothStacks) {
    int state = 0;
    VfxUndoStack s;
    state += 1;
    s.Push(std::make_unique<CounterCommand>(&state, 1));
    s.Undo();
    EXPECT_TRUE(s.CanRedo());
    s.Clear();
    EXPECT_FALSE(s.CanUndo());
    EXPECT_FALSE(s.CanRedo());
}

TEST(VfxUndoStack, ClearRedoTail_KeepsUndo) {
    // Two pushes + one undo leaves one command on each side; with a single
    // push the undo side would already be empty before ClearRedoTail.
    int state = 0;
    VfxUndoStack s;
    state += 1;
    s.Push(std::make_unique<CounterCommand>(&state, 1));
    state += 2;
    s.Push(std::make_unique<CounterCommand>(&state, 2));
    s.Undo();
    EXPECT_TRUE(s.CanUndo());
    EXPECT_TRUE(s.CanRedo());
    s.ClearRedoTail();
    EXPECT_TRUE(s.CanUndo());
    EXPECT_EQ(s.UndoDepth(), 1u);
    EXPECT_FALSE(s.CanRedo());
    EXPECT_EQ(state, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stack family
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxUndoCommands, StackAddModule_RoundTrip) {
    VfxStack stk;
    VfxModuleInstance mod = MakeMod("Gravity", "g1");
    stk.GetEntry(VfxStage::ParticleUpdate).Add(mod);
    auto cmd = std::make_unique<StackAddModuleCommand>(
        &stk, VfxStage::ParticleUpdate, mod);

    EXPECT_EQ(stk.GetEntry(VfxStage::ParticleUpdate).Size(), 1u);
    cmd->Revert();
    EXPECT_EQ(stk.GetEntry(VfxStage::ParticleUpdate).Size(), 0u);
    cmd->Apply();
    EXPECT_EQ(stk.GetEntry(VfxStage::ParticleUpdate).Size(), 1u);
    EXPECT_EQ(stk.GetEntry(VfxStage::ParticleUpdate).modules[0].instanceId, "g1");
}

TEST(VfxUndoCommands, StackRemoveModule_RoundTripPreservesIndex) {
    VfxStack stk;
    auto& entry = stk.GetEntry(VfxStage::ParticleUpdate);
    entry.Add(MakeMod("A", "a"));
    entry.Add(MakeMod("B", "b"));
    entry.Add(MakeMod("C", "c"));

    VfxModuleInstance removed = entry.modules[1];
    entry.Remove(1);
    auto cmd = std::make_unique<StackRemoveModuleCommand>(
        &stk, VfxStage::ParticleUpdate, removed, 1u);

    EXPECT_EQ(entry.Size(), 2u);
    cmd->Revert();
    ASSERT_EQ(entry.Size(), 3u);
    EXPECT_EQ(entry.modules[1].instanceId, "b");
    cmd->Apply();
    EXPECT_EQ(entry.Size(), 2u);
}

TEST(VfxUndoCommands, StackReorder_SwapIsItsOwnInverse) {
    VfxStack stk;
    auto& entry = stk.GetEntry(VfxStage::ParticleUpdate);
    entry.Add(MakeMod("A", "a"));
    entry.Add(MakeMod("B", "b"));

    StackReorderCommand cmd(&stk, VfxStage::ParticleUpdate, 0u, 1u);
    cmd.Apply();
    EXPECT_EQ(entry.modules[0].instanceId, "b");
    cmd.Revert();
    EXPECT_EQ(entry.modules[0].instanceId, "a");
}

TEST(VfxUndoCommands, ModuleEnabledToggle_FlipsBit) {
    VfxStack stk;
    auto& entry = stk.GetEntry(VfxStage::ParticleUpdate);
    auto mod = MakeMod("Gravity", "g1");
    mod.enabled = true;
    entry.Add(mod);

    ModuleEnabledToggleCommand cmd(&stk, "g1");
    cmd.Apply();
    EXPECT_FALSE(entry.modules[0].enabled);
    cmd.Revert();
    EXPECT_TRUE(entry.modules[0].enabled);
}

// ─────────────────────────────────────────────────────────────────────────────
// Parameter family
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxUndoCommands, ParameterAdd_RoundTrip) {
    VfxAttributeStore store;
    store.SetVec4(VfxAttributeScope::Emitter, "color", Vec4{1, 0, 0, 1});
    ParameterAddCommand cmd(&store, VfxAttributeScope::Emitter, "color",
                             Vec4{1, 0, 0, 1});
    cmd.Revert();
    EXPECT_FALSE(store.Has(VfxAttributeScope::Emitter, "color"));
    cmd.Apply();
    EXPECT_TRUE(store.Has(VfxAttributeScope::Emitter, "color"));
}

TEST(VfxUndoCommands, ParameterRemove_RestoresValue) {
    VfxAttributeStore store;
    store.SetVec4(VfxAttributeScope::Emitter, "color", Vec4{0.2f, 0.3f, 0.4f, 1.0f});
    // Capture + remove.
    const Vec4 saved = store.GetVec4(VfxAttributeScope::Emitter, "color");
    (void)store.Remove(VfxAttributeScope::Emitter, "color");

    ParameterRemoveCommand cmd(&store, VfxAttributeScope::Emitter, "color", saved);
    EXPECT_FALSE(store.Has(VfxAttributeScope::Emitter, "color"));
    cmd.Revert();
    ASSERT_TRUE(store.Has(VfxAttributeScope::Emitter, "color"));
    const Vec4 back = store.GetVec4(VfxAttributeScope::Emitter, "color");
    EXPECT_FLOAT_EQ(back.x, 0.2f);
    EXPECT_FLOAT_EQ(back.w, 1.0f);
    cmd.Apply();
    EXPECT_FALSE(store.Has(VfxAttributeScope::Emitter, "color"));
}

TEST(VfxUndoCommands, ParameterRename_SwapsKeys) {
    VfxAttributeStore store;
    store.SetFloat(VfxAttributeScope::Emitter, "old", 1.25f);

    ParameterRenameCommand cmd(&store, VfxAttributeScope::Emitter, "old", "new");
    cmd.Apply();
    EXPECT_FALSE(store.Has(VfxAttributeScope::Emitter, "old"));
    EXPECT_TRUE(store.Has(VfxAttributeScope::Emitter, "new"));
    EXPECT_FLOAT_EQ(store.GetFloat(VfxAttributeScope::Emitter, "new"), 1.25f);
    cmd.Revert();
    EXPECT_TRUE(store.Has(VfxAttributeScope::Emitter, "old"));
    EXPECT_FALSE(store.Has(VfxAttributeScope::Emitter, "new"));
}

TEST(VfxUndoCommands, ParameterTypeChange_FlipsSlot) {
    ParameterTypeChangeCommand::TypeTag slot = 0;  // Float
    ParameterTypeChangeCommand cmd(&slot, 0, 2);   // Float → Vec4
    cmd.Apply();
    EXPECT_EQ(slot, 2);
    cmd.Revert();
    EXPECT_EQ(slot, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Curve family
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxUndoCommands, CurveKeyAdd_RoundTrip) {
    VFXCurve curve;
    curve.SetKeys({ {0.0f, 0.0f}, {1.0f, 1.0f} });

    // Author adds a midpoint key.
    auto keys = curve.GetFloatKeys();
    keys.push_back({0.5f, 0.5f});
    std::sort(keys.begin(), keys.end(),
              [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
    curve.SetKeys(keys);
    ASSERT_EQ(curve.GetKeyCount(), 3u);

    CurveKeyAddCommand cmd(&curve, {0.5f, 0.5f});
    cmd.Revert();
    EXPECT_EQ(curve.GetKeyCount(), 2u);
    cmd.Apply();
    EXPECT_EQ(curve.GetKeyCount(), 3u);
}

TEST(VfxUndoCommands, CurveKeyRemove_RoundTrip) {
    VFXCurve curve;
    curve.SetKeys({ {0.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f} });

    Keyframe removed{0.5f, 0.5f};
    auto keys = curve.GetFloatKeys();
    keys.erase(keys.begin() + 1);
    curve.SetKeys(keys);

    CurveKeyRemoveCommand cmd(&curve, removed);
    EXPECT_EQ(curve.GetKeyCount(), 2u);
    cmd.Revert();
    ASSERT_EQ(curve.GetKeyCount(), 3u);
    EXPECT_FLOAT_EQ(curve.GetFloatKeys()[1].time, 0.5f);
    cmd.Apply();
    EXPECT_EQ(curve.GetKeyCount(), 2u);
}

TEST(VfxUndoCommands, CurveKeyMove_RoundTrip) {
    VFXCurve curve;
    curve.SetKeys({ {0.2f, 1.0f}, {0.6f, 2.0f} });
    // Simulate a move of the first key to (0.3, 1.5).
    auto keys = curve.GetFloatKeys();
    keys[0].time  = 0.3f;
    keys[0].value = 1.5f;
    curve.SetKeys(keys);

    CurveKeyMoveCommand cmd(&curve, {0.2f, 1.0f}, {0.3f, 1.5f});
    cmd.Revert();
    EXPECT_FLOAT_EQ(curve.GetFloatKeys()[0].time, 0.2f);
    EXPECT_FLOAT_EQ(curve.GetFloatKeys()[0].value, 1.0f);
    cmd.Apply();
    EXPECT_FLOAT_EQ(curve.GetFloatKeys()[0].time, 0.3f);
}

TEST(VfxUndoCommands, CurveKeyInterpChange_RoundTrip) {
    VFXCurve curve;
    curve.SetInterpolation(InterpolationMode::Linear);
    curve.SetInterpolation(InterpolationMode::Cubic);

    CurveKeyInterpChangeCommand cmd(&curve,
                                      InterpolationMode::Linear,
                                      InterpolationMode::Cubic);
    cmd.Revert();
    EXPECT_EQ(curve.GetInterpolation(), InterpolationMode::Linear);
    cmd.Apply();
    EXPECT_EQ(curve.GetInterpolation(), InterpolationMode::Cubic);
}

// ─────────────────────────────────────────────────────────────────────────────
// Burst family
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxUndoCommands, BurstAdd_RoundTrip) {
    std::vector<VfxBurstEvent> bursts;
    VfxBurstList::Add(bursts, {0.2f, 50});

    BurstAddCommand cmd(&bursts, {0.2f, 50});
    cmd.Revert();
    EXPECT_TRUE(bursts.empty());
    cmd.Apply();
    ASSERT_EQ(bursts.size(), 1u);
    EXPECT_FLOAT_EQ(bursts[0].time, 0.2f);
}

TEST(VfxUndoCommands, BurstRemove_RoundTrip) {
    std::vector<VfxBurstEvent> bursts;
    VfxBurstList::Add(bursts, {0.1f, 10});
    VfxBurstList::Add(bursts, {0.5f, 50});
    const VfxBurstEvent removed = bursts[0];
    VfxBurstList::Remove(bursts, 0);

    BurstRemoveCommand cmd(&bursts, removed);
    EXPECT_EQ(bursts.size(), 1u);
    cmd.Revert();
    EXPECT_EQ(bursts.size(), 2u);
    cmd.Apply();
    EXPECT_EQ(bursts.size(), 1u);
}

TEST(VfxUndoCommands, BurstEdit_RoundTrip) {
    std::vector<VfxBurstEvent> bursts;
    VfxBurstList::Add(bursts, {0.1f, 10});

    BurstEditCommand cmd(&bursts, {0.1f, 10}, {0.2f, 25});
    cmd.Apply();
    ASSERT_EQ(bursts.size(), 1u);
    EXPECT_FLOAT_EQ(bursts[0].time, 0.2f);
    EXPECT_EQ(bursts[0].count, 25u);
    cmd.Revert();
    EXPECT_FLOAT_EQ(bursts[0].time, 0.1f);
    EXPECT_EQ(bursts[0].count, 10u);
}

// ─────────────────────────────────────────────────────────────────────────────
// End-to-end: stack driver over concrete commands
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxUndoStackE2E, UndoRedoChain_StackAndBurst) {
    VfxStack stk;
    std::vector<VfxBurstEvent> bursts;

    VfxUndoStack undo;

    // Apply 1: add Gravity module.
    auto mod = MakeMod("Gravity", "g1");
    stk.GetEntry(VfxStage::ParticleUpdate).Add(mod);
    undo.Push(std::make_unique<StackAddModuleCommand>(
        &stk, VfxStage::ParticleUpdate, mod));

    // Apply 2: add burst at 0.2s.
    VfxBurstList::Add(bursts, {0.2f, 100});
    undo.Push(std::make_unique<BurstAddCommand>(&bursts,
                                                   VfxBurstEvent{0.2f, 100}));
    EXPECT_EQ(stk.GetEntry(VfxStage::ParticleUpdate).Size(), 1u);
    EXPECT_EQ(bursts.size(), 1u);

    // Undo burst
    EXPECT_EQ(undo.Undo(), "Add Burst");
    EXPECT_TRUE(bursts.empty());

    // Undo module
    EXPECT_EQ(undo.Undo(), "Add Module");
    EXPECT_EQ(stk.GetEntry(VfxStage::ParticleUpdate).Size(), 0u);

    // Redo module + burst
    EXPECT_EQ(undo.Redo(), "Add Module");
    EXPECT_EQ(undo.Redo(), "Add Burst");
    EXPECT_EQ(stk.GetEntry(VfxStage::ParticleUpdate).Size(), 1u);
    EXPECT_EQ(bursts.size(), 1u);
}
