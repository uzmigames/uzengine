#pragma once

/// Concrete VFX editor undo / redo commands (phase 11.9.0).
///
/// Each command captures the minimum state needed so both `Apply()`
/// (for redo) and `Revert()` (for undo) are deterministic — independent
/// of the live scene shape beyond the pointers they bind to.
///
/// # Convention
///
/// Commands are constructed **already-applied** — the caller wraps
/// their mutation inside the constructor parameters and then pushes the
/// command:
///
/// ```cpp
/// stack.GetEntry(stage).Add(mod);                              // mutation
/// undo.Push(std::make_unique<StackAddModuleCommand>(&stack, stage, mod.instanceId));
/// ```
///
/// This keeps the failure path simple (if the mutation threw, nothing
/// lands on the stack) and makes `Apply()` idempotent on redo.
///
/// # Families
///
/// - **Stack** — `StackAddModuleCommand`, `StackRemoveModuleCommand`,
///   `StackReorderCommand`, `ModuleEnabledToggleCommand`.
/// - **Parameters** — `ParameterAddCommand`, `ParameterRemoveCommand`,
///   `ParameterRenameCommand`, `ParameterTypeChangeCommand`.
/// - **Curves** — `CurveKeyAddCommand`, `CurveKeyRemoveCommand`,
///   `CurveKeyMoveCommand`, `CurveKeyInterpChangeCommand`.
/// - **Bursts** — `BurstAddCommand`, `BurstRemoveCommand`,
///   `BurstEditCommand`.
///
/// @ref engine/vfx/editor/undo/vfx_undo_stack.h

#include "vfx_undo_command.h"

#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/curves/vfx_curve.h"
#include "engine/vfx/parameters/vfx_attribute_store.h"
#include "engine/vfx/spawn/vfx_burst_event.h"
#include "engine/vfx/stack/vfx_stack.h"
#include "vector4.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Stack family
// ─────────────────────────────────────────────────────────────────────────────

/// Adds a `VfxModuleInstance` to `stage`. Revert removes the instance
/// identified by its stable `instanceId`.
class StackAddModuleCommand final : public IVfxUndoCommand {
public:
    StackAddModuleCommand(VfxStack* stack, VfxStage stage,
                           VfxModuleInstance module) noexcept
        : m_stack(stack), m_stage(stage), m_module(std::move(module)) {}

    void Apply() override {
        if (m_stack == nullptr) return;
        auto& entry = m_stack->GetEntry(m_stage);
        // Don't re-insert if an instance with the same id already exists
        // (happens when Apply follows Push without an intervening Revert).
        if (entry.Find(m_module.instanceId) != static_cast<size_t>(-1)) {
            return;
        }
        entry.Add(m_module);
    }

    void Revert() override {
        if (m_stack == nullptr) return;
        auto& entry = m_stack->GetEntry(m_stage);
        const size_t i = entry.Find(m_module.instanceId);
        if (i != static_cast<size_t>(-1)) entry.Remove(i);
    }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Add Module";
    }

private:
    VfxStack*         m_stack = nullptr;
    VfxStage          m_stage = VfxStage::ParticleUpdate;
    VfxModuleInstance m_module;
};

/// Removes a `VfxModuleInstance` from a stage, capturing both the
/// instance payload and its original index so revert can restore
/// insertion order.
class StackRemoveModuleCommand final : public IVfxUndoCommand {
public:
    StackRemoveModuleCommand(VfxStack* stack, VfxStage stage,
                              VfxModuleInstance removed,
                              size_t originalIndex) noexcept
        : m_stack(stack),
          m_stage(stage),
          m_module(std::move(removed)),
          m_originalIndex(originalIndex) {}

    void Apply() override {
        if (m_stack == nullptr) return;
        auto& entry = m_stack->GetEntry(m_stage);
        const size_t i = entry.Find(m_module.instanceId);
        if (i != static_cast<size_t>(-1)) entry.Remove(i);
    }

    void Revert() override {
        if (m_stack == nullptr) return;
        auto& entry = m_stack->GetEntry(m_stage);
        if (entry.Find(m_module.instanceId) != static_cast<size_t>(-1)) return;
        entry.InsertAt(m_originalIndex, m_module);
    }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Remove Module";
    }

private:
    VfxStack*         m_stack = nullptr;
    VfxStage          m_stage = VfxStage::ParticleUpdate;
    VfxModuleInstance m_module;
    size_t            m_originalIndex = 0;
};

/// Swaps two module slots within the same stage.
class StackReorderCommand final : public IVfxUndoCommand {
public:
    StackReorderCommand(VfxStack* stack, VfxStage stage,
                          size_t fromIndex, size_t toIndex) noexcept
        : m_stack(stack), m_stage(stage),
          m_fromIndex(fromIndex), m_toIndex(toIndex) {}

    void Apply() override {
        if (m_stack == nullptr) return;
        m_stack->GetEntry(m_stage).Swap(m_fromIndex, m_toIndex);
    }
    void Revert() override { Apply(); }  // swap is its own inverse

    [[nodiscard]] const char* Name() const noexcept override {
        return "Reorder Module";
    }

private:
    VfxStack* m_stack = nullptr;
    VfxStage  m_stage = VfxStage::ParticleUpdate;
    size_t    m_fromIndex = 0;
    size_t    m_toIndex   = 0;
};

/// Toggles a module's `enabled` bit.
class ModuleEnabledToggleCommand final : public IVfxUndoCommand {
public:
    ModuleEnabledToggleCommand(VfxStack* stack,
                                 std::string instanceId) noexcept
        : m_stack(stack), m_instanceId(std::move(instanceId)) {}

    void Apply() override { Flip(); }
    void Revert() override { Flip(); }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Toggle Module Enabled";
    }

private:
    void Flip() {
        if (m_stack == nullptr) return;
        VfxStage s = VfxStage::ParticleUpdate;
        size_t i = 0;
        if (!m_stack->FindInstance(m_instanceId, s, i)) return;
        auto& mods = m_stack->GetEntry(s).modules;
        mods[i].enabled = !mods[i].enabled;
    }

    VfxStack*   m_stack = nullptr;
    std::string m_instanceId;
};

// ─────────────────────────────────────────────────────────────────────────────
// Parameter family
// ─────────────────────────────────────────────────────────────────────────────

/// Adds a `(scope, name)` float/vec4 parameter to an attribute store.
class ParameterAddCommand final : public IVfxUndoCommand {
public:
    ParameterAddCommand(VfxAttributeStore* store, VfxAttributeScope scope,
                          std::string name, const Vec4& value) noexcept
        : m_store(store), m_scope(scope),
          m_name(std::move(name)), m_value(value) {}

    void Apply() override {
        if (m_store == nullptr) return;
        if (!m_store->Has(m_scope, m_name)) {
            m_store->SetVec4(m_scope, m_name, m_value);
        }
    }

    void Revert() override {
        if (m_store == nullptr) return;
        (void)m_store->Remove(m_scope, m_name);
    }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Add Parameter";
    }

private:
    VfxAttributeStore* m_store = nullptr;
    VfxAttributeScope  m_scope = VfxAttributeScope::Emitter;
    std::string        m_name;
    Vec4               m_value{};
};

/// Removes a parameter. Captures both the name and the prior value so
/// revert can restore it.
class ParameterRemoveCommand final : public IVfxUndoCommand {
public:
    ParameterRemoveCommand(VfxAttributeStore* store, VfxAttributeScope scope,
                             std::string name, const Vec4& savedValue) noexcept
        : m_store(store), m_scope(scope),
          m_name(std::move(name)), m_saved(savedValue) {}

    void Apply() override {
        if (m_store == nullptr) return;
        (void)m_store->Remove(m_scope, m_name);
    }

    void Revert() override {
        if (m_store == nullptr) return;
        if (!m_store->Has(m_scope, m_name)) {
            m_store->SetVec4(m_scope, m_name, m_saved);
        }
    }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Remove Parameter";
    }

private:
    VfxAttributeStore* m_store = nullptr;
    VfxAttributeScope  m_scope = VfxAttributeScope::Emitter;
    std::string        m_name;
    Vec4               m_saved{};
};

/// Renames a parameter within a scope. Does not rewrite `@name`
/// references in module configs — the caller handles that separately
/// (the command tracks only the store-level rename).
class ParameterRenameCommand final : public IVfxUndoCommand {
public:
    ParameterRenameCommand(VfxAttributeStore* store, VfxAttributeScope scope,
                             std::string oldName, std::string newName) noexcept
        : m_store(store), m_scope(scope),
          m_oldName(std::move(oldName)), m_newName(std::move(newName)) {}

    void Apply() override { Rename(m_oldName, m_newName); }
    void Revert() override { Rename(m_newName, m_oldName); }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Rename Parameter";
    }

private:
    void Rename(const std::string& from, const std::string& to) {
        if (m_store == nullptr) return;
        if (!m_store->Has(m_scope, from)) return;
        if (m_store->Has(m_scope, to))   return;
        const Vec4 v = m_store->GetVec4(m_scope, from, Vec4{});
        m_store->SetVec4(m_scope, to, v);
        (void)m_store->Remove(m_scope, from);
    }

    VfxAttributeStore* m_store = nullptr;
    VfxAttributeScope  m_scope = VfxAttributeScope::Emitter;
    std::string        m_oldName;
    std::string        m_newName;
};

/// Changes a parameter's authored type via an external type-map pointer
/// + value re-slot. Captures both old and new enum values; widget-level
/// type registry lives outside the attribute store, so the command
/// holds a lambda-style function pointer in a map for flexibility.
class ParameterTypeChangeCommand final : public IVfxUndoCommand {
public:
    using TypeTag = uint8_t;  // matches VfxParamType enum width

    ParameterTypeChangeCommand(TypeTag* typeSlot,
                                 TypeTag oldType, TypeTag newType) noexcept
        : m_typeSlot(typeSlot),
          m_oldType(oldType),
          m_newType(newType) {}

    void Apply() override {
        if (m_typeSlot != nullptr) *m_typeSlot = m_newType;
    }
    void Revert() override {
        if (m_typeSlot != nullptr) *m_typeSlot = m_oldType;
    }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Change Parameter Type";
    }

private:
    TypeTag* m_typeSlot = nullptr;
    TypeTag  m_oldType = 0;
    TypeTag  m_newType = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Curve family
// ─────────────────────────────────────────────────────────────────────────────

/// Adds a float keyframe to a `VFXCurve` (Float mode). Apply inserts,
/// Revert removes the matching `(time, value)` pair.
class CurveKeyAddCommand final : public IVfxUndoCommand {
public:
    CurveKeyAddCommand(VFXCurve* curve, Keyframe key) noexcept
        : m_curve(curve), m_key(key) {}

    void Apply() override {
        if (m_curve == nullptr) return;
        auto keys = m_curve->GetFloatKeys();
        // Avoid duplicate inserts when Apply re-runs on redo.
        if (FindKey(keys, m_key) != static_cast<size_t>(-1)) return;
        keys.push_back(m_key);
        std::sort(keys.begin(), keys.end(),
                  [](const Keyframe& a, const Keyframe& b) {
                      return a.time < b.time;
                  });
        m_curve->SetKeys(std::move(keys));
    }

    void Revert() override {
        if (m_curve == nullptr) return;
        auto keys = m_curve->GetFloatKeys();
        const size_t i = FindKey(keys, m_key);
        if (i == static_cast<size_t>(-1)) return;
        keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(i));
        m_curve->SetKeys(std::move(keys));
    }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Add Curve Key";
    }

private:
    static size_t FindKey(const std::vector<Keyframe>& v, const Keyframe& k) {
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i].time == k.time && v[i].value == k.value) return i;
        }
        return static_cast<size_t>(-1);
    }
    VFXCurve* m_curve = nullptr;
    Keyframe  m_key{};
};

/// Removes a float keyframe from a `VFXCurve`. Snapshots the removed
/// key so revert can re-insert it.
class CurveKeyRemoveCommand final : public IVfxUndoCommand {
public:
    CurveKeyRemoveCommand(VFXCurve* curve, Keyframe removedKey) noexcept
        : m_curve(curve), m_key(removedKey) {}

    void Apply() override {
        if (m_curve == nullptr) return;
        auto keys = m_curve->GetFloatKeys();
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keys[i].time == m_key.time && keys[i].value == m_key.value) {
                keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(i));
                m_curve->SetKeys(std::move(keys));
                return;
            }
        }
    }

    void Revert() override {
        if (m_curve == nullptr) return;
        auto keys = m_curve->GetFloatKeys();
        keys.push_back(m_key);
        std::sort(keys.begin(), keys.end(),
                  [](const Keyframe& a, const Keyframe& b) {
                      return a.time < b.time;
                  });
        m_curve->SetKeys(std::move(keys));
    }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Remove Curve Key";
    }

private:
    VFXCurve* m_curve = nullptr;
    Keyframe  m_key{};
};

/// Moves a curve keyframe (both time + value). Captures old/new
/// `Keyframe` payload so revert is symmetric.
class CurveKeyMoveCommand final : public IVfxUndoCommand {
public:
    CurveKeyMoveCommand(VFXCurve* curve, Keyframe oldKey, Keyframe newKey) noexcept
        : m_curve(curve), m_old(oldKey), m_new(newKey) {}

    void Apply() override { Swap(m_old, m_new); }
    void Revert() override { Swap(m_new, m_old); }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Move Curve Key";
    }

private:
    void Swap(const Keyframe& from, const Keyframe& to) {
        if (m_curve == nullptr) return;
        auto keys = m_curve->GetFloatKeys();
        for (auto& k : keys) {
            if (k.time == from.time && k.value == from.value) {
                k.time  = to.time;
                k.value = to.value;
                break;
            }
        }
        std::sort(keys.begin(), keys.end(),
                  [](const Keyframe& a, const Keyframe& b) {
                      return a.time < b.time;
                  });
        m_curve->SetKeys(std::move(keys));
    }
    VFXCurve* m_curve = nullptr;
    Keyframe  m_old{};
    Keyframe  m_new{};
};

/// Changes a curve's interpolation mode.
class CurveKeyInterpChangeCommand final : public IVfxUndoCommand {
public:
    CurveKeyInterpChangeCommand(VFXCurve* curve,
                                  InterpolationMode oldMode,
                                  InterpolationMode newMode) noexcept
        : m_curve(curve), m_old(oldMode), m_new(newMode) {}

    void Apply() override {
        if (m_curve != nullptr) m_curve->SetInterpolation(m_new);
    }
    void Revert() override {
        if (m_curve != nullptr) m_curve->SetInterpolation(m_old);
    }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Change Curve Interpolation";
    }

private:
    VFXCurve*         m_curve = nullptr;
    InterpolationMode m_old = InterpolationMode::Linear;
    InterpolationMode m_new = InterpolationMode::Linear;
};

// ─────────────────────────────────────────────────────────────────────────────
// Burst family
// ─────────────────────────────────────────────────────────────────────────────

/// Adds a burst event to an emitter's `bursts` list.
class BurstAddCommand final : public IVfxUndoCommand {
public:
    BurstAddCommand(std::vector<VfxBurstEvent>* bursts,
                     VfxBurstEvent burst) noexcept
        : m_bursts(bursts), m_burst(burst) {}

    void Apply() override {
        if (m_bursts == nullptr) return;
        if (FindBurst() != static_cast<size_t>(-1)) return;  // already there
        VfxBurstList::Add(*m_bursts, m_burst);
    }

    void Revert() override {
        if (m_bursts == nullptr) return;
        const size_t i = FindBurst();
        if (i != static_cast<size_t>(-1)) VfxBurstList::Remove(*m_bursts, i);
    }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Add Burst";
    }

private:
    size_t FindBurst() const {
        for (size_t i = 0; i < m_bursts->size(); ++i) {
            if ((*m_bursts)[i].time  == m_burst.time &&
                (*m_bursts)[i].count == m_burst.count) {
                return i;
            }
        }
        return static_cast<size_t>(-1);
    }
    std::vector<VfxBurstEvent>* m_bursts = nullptr;
    VfxBurstEvent               m_burst{};
};

/// Removes a burst event. Snapshots the burst + its original index so
/// revert can re-insert at the right spot.
class BurstRemoveCommand final : public IVfxUndoCommand {
public:
    BurstRemoveCommand(std::vector<VfxBurstEvent>* bursts,
                        VfxBurstEvent burst) noexcept
        : m_bursts(bursts), m_burst(burst) {}

    void Apply() override {
        if (m_bursts == nullptr) return;
        for (size_t i = 0; i < m_bursts->size(); ++i) {
            if ((*m_bursts)[i].time  == m_burst.time &&
                (*m_bursts)[i].count == m_burst.count) {
                VfxBurstList::Remove(*m_bursts, i);
                return;
            }
        }
    }

    void Revert() override {
        if (m_bursts == nullptr) return;
        VfxBurstList::Add(*m_bursts, m_burst);
    }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Remove Burst";
    }

private:
    std::vector<VfxBurstEvent>* m_bursts = nullptr;
    VfxBurstEvent               m_burst{};
};

/// Reverts an `OverrideReplace` / `OverrideAdd` / `OverrideRemove`
/// module instance back to its parent's state (phase 11.11). Stores
/// both the current override (`childModule`) and the parent's original
/// module (`parentModule`) so the command is symmetric.
///
/// For `OverrideRemove` entries, Apply re-applies the remove (i.e.
/// flags the slot `OverrideRemove`), Revert restores it to
/// `Inherited`. For `OverrideReplace` / `OverrideAdd`, Apply swaps the
/// slot to the parent's payload tagged `Inherited`, Revert restores
/// the child override payload.
class RevertOverrideCommand final : public IVfxUndoCommand {
public:
    RevertOverrideCommand(VfxStack* stack,
                            std::string instanceId,
                            VfxModuleInstance childModule,
                            VfxModuleInstance parentModule) noexcept
        : m_stack(stack),
          m_instanceId(std::move(instanceId)),
          m_child(std::move(childModule)),
          m_parent(std::move(parentModule)) {}

    void Apply() override {
        if (m_stack == nullptr) return;
        VfxStage stage = VfxStage::ParticleUpdate;
        size_t index = 0u;
        if (!m_stack->FindInstance(m_instanceId, stage, index)) return;
        auto& mods = m_stack->GetEntry(stage).modules;
        mods[index] = m_parent;                   // parent payload
        mods[index].overrideFlag = VfxOverrideFlag::Inherited;
    }

    void Revert() override {
        if (m_stack == nullptr) return;
        VfxStage stage = VfxStage::ParticleUpdate;
        size_t index = 0u;
        if (!m_stack->FindInstance(m_instanceId, stage, index)) return;
        auto& mods = m_stack->GetEntry(stage).modules;
        mods[index] = m_child;                    // restore override
    }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Revert to Parent";
    }

private:
    VfxStack*         m_stack = nullptr;
    std::string       m_instanceId;
    VfxModuleInstance m_child;
    VfxModuleInstance m_parent;
};

/// Edits an existing burst (time and/or count). Identified by the
/// original `VfxBurstEvent` payload; `newBurst` is what Apply writes.
class BurstEditCommand final : public IVfxUndoCommand {
public:
    BurstEditCommand(std::vector<VfxBurstEvent>* bursts,
                      VfxBurstEvent oldBurst,
                      VfxBurstEvent newBurst) noexcept
        : m_bursts(bursts), m_old(oldBurst), m_new(newBurst) {}

    void Apply() override { Swap(m_old, m_new); }
    void Revert() override { Swap(m_new, m_old); }

    [[nodiscard]] const char* Name() const noexcept override {
        return "Edit Burst";
    }

private:
    void Swap(const VfxBurstEvent& from, const VfxBurstEvent& to) {
        if (m_bursts == nullptr) return;
        for (size_t i = 0; i < m_bursts->size(); ++i) {
            if ((*m_bursts)[i].time  == from.time &&
                (*m_bursts)[i].count == from.count) {
                VfxBurstList::Update(*m_bursts, i, to);
                return;
            }
        }
    }
    std::vector<VfxBurstEvent>* m_bursts = nullptr;
    VfxBurstEvent               m_old{};
    VfxBurstEvent               m_new{};
};

} // namespace uz
