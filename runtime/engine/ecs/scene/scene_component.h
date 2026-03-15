/// SceneComponent — Component with a spatial transform and scene hierarchy.
///
/// Derives from `Component` and adds a local TRS (translation/rotation/scale)
/// transform, a lazily-evaluated world matrix cache, and a parent-child
/// hierarchy for building scene graphs.
///
/// # Hierarchy
/// Attach a component to a parent with `AttachTo(parent)`. The parent retains
/// a raw (non-owning) pointer to each child. World transform is computed
/// lazily on `GetWorldMatrix()` by walking the parent chain.
///
/// # Dirty Propagation
/// Changing any local transform component calls `MarkDirty()`, which
/// recursively marks all descendants dirty in O(N) where N is subtree size.
/// The world matrix is only recomputed on the next `GetWorldMatrix()` call.
///
/// # Thread Safety
/// All public methods must be called from the main thread only.
/// The `mutable` cached fields are protected by the single-threaded contract.

#pragma once

#include "component.h"
#include "../math/vector3.h"
#include "../math/quaternion.h"
#include "../math/mat4.h"

#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// SceneComponent
// ─────────────────────────────────────────────────────────────────────────────

/// Component with a 3D transform that participates in the scene hierarchy.
///
/// Each `SceneComponent` stores a local TRS transform relative to its parent.
/// If it has no parent, the local transform is the world transform. Attaching
/// a component to a parent with `AttachTo()` is how scene graphs are formed.
///
/// The world-space `Mat4` is cached and only recomputed when the component (or
/// any ancestor) is marked dirty. `GetWorldMatrix()` triggers recomputation if
/// needed and always returns a valid result.
///
/// # Ownership
/// Parent-child relationships use raw non-owning pointers. Lifetime is managed
/// by the ECS World. Destroying a `SceneComponent` automatically detaches it
/// from its parent and from all its children.
///
/// # Examples
/// ```cpp
/// SceneComponent root, child;
/// root.SetPosition({10.f, 0.f, 0.f});
/// child.SetPosition({5.f, 0.f, 0.f});
/// child.AttachTo(&root);
/// // child world position is now {15, 0, 0}
/// Mat4 worldMat = child.GetWorldMatrix();
/// Vec3 worldPos = child.GetWorldPosition(); // {15, 0, 0}
/// ```
class SceneComponent : public Component {
public:
    /// Constructor. Initializes to identity transform (origin, no rotation, unit scale).
    SceneComponent() = default;

    /// Destructor. Detaches from parent and removes this component from all children.
    ///
    /// Children become root-level components (their parent pointer is set to null).
    /// Their world matrices are marked dirty so they recompute on next access.
    ~SceneComponent() override;

    // ── Local Transform Setters ───────────────────────────────────────────────

    /// Sets the local position and marks the transform dirty.
    ///
    /// Position is relative to the parent's world transform, or world-space
    /// if this component has no parent.
    ///
    /// # Arguments
    /// * `pos` - New local position in 3D space.
    void SetPosition(const Vec3& pos);

    /// Sets the local rotation and marks the transform dirty.
    ///
    /// # Arguments
    /// * `rot` - New local rotation as a unit quaternion.
    void SetRotation(const Quat& rot);

    /// Sets the local scale and marks the transform dirty.
    ///
    /// Non-uniform scale is supported. Negative scale produces mirroring but
    /// may cause unexpected results with normal transforms.
    ///
    /// # Arguments
    /// * `scl` - New local scale per axis.
    void SetScale(const Vec3& scl);

    // ── Local Transform Getters ───────────────────────────────────────────────

    /// Returns the local position (relative to parent).
    [[nodiscard]] const Vec3& GetPosition() const noexcept { return m_position; }

    /// Returns the local rotation (relative to parent).
    [[nodiscard]] const Quat& GetRotation() const noexcept { return m_rotation; }

    /// Returns the local scale (relative to parent).
    [[nodiscard]] const Vec3& GetScale() const noexcept { return m_scale; }

    // ── World Transform (cached, lazy) ────────────────────────────────────────

    /// Returns the cached world-space transform matrix.
    ///
    /// Recomputes the matrix from the local TRS and the parent chain if the
    /// dirty flag is set. Subsequent calls return the cached value until the
    /// transform is modified again.
    ///
    /// # Returns
    /// A reference to the cached world `Mat4`. Valid until any transform in
    /// the ancestor chain is modified.
    [[nodiscard]] const Mat4& GetWorldMatrix() const;

    /// Returns the world-space position extracted from the world matrix.
    ///
    /// Equivalent to `GetWorldMatrix().getTranslation()`.
    ///
    /// # Returns
    /// World-space position as `Vec3`.
    [[nodiscard]] Vec3 GetWorldPosition() const;

    // ── Hierarchy ─────────────────────────────────────────────────────────────

    /// Attaches this component as a child of `parent`.
    ///
    /// If this component already has a parent, it is first detached from the
    /// current parent before being attached to the new one. Attaching to null
    /// is equivalent to calling `Detach()`.
    ///
    /// Marks this component (and all descendants) dirty after attachment.
    ///
    /// # Arguments
    /// * `parent` - The new parent. May be null to detach from hierarchy.
    void AttachTo(SceneComponent* parent);

    /// Detaches this component from its current parent.
    ///
    /// After detachment the local transform is unchanged. This component
    /// becomes a root-level component. No-op if already parentless.
    ///
    /// Marks dirty after detachment.
    void Detach();

    /// Returns the parent `SceneComponent`, or null if this is a root component.
    [[nodiscard]] SceneComponent* GetParent() const noexcept { return m_parent; }

    /// Returns the direct children of this component.
    ///
    /// # Returns
    /// An immutable reference to the children vector. Pointers are non-owning.
    [[nodiscard]] const std::vector<SceneComponent*>& GetChildren() const noexcept {
        return m_children;
    }

    // ── Mobility & Visibility ────────────────────────────────────────────────

    /// Component mobility — controls lighting/shadow bake eligibility.
    /// UE5: EComponentMobility. Default: Movable (fully dynamic).
    enum class Mobility : uint8_t { Static, Stationary, Movable };
    Mobility mobility = Mobility::Movable;

    /// When true, the component is hidden during gameplay but visible in editor.
    /// UE5: bHiddenInGame. Default: false.
    bool hiddenInGame = false;

    // ── Dirty Flag ────────────────────────────────────────────────────────────

    /// Marks this component and all descendants as dirty.
    ///
    /// Called automatically when any local transform setter is used.
    /// May also be called manually if external code modifies parent state
    /// through a back-channel (rare).
    void MarkDirty();

    /// Returns true if the cached world matrix needs recomputation.
    [[nodiscard]] bool IsDirty() const noexcept { return m_worldDirty; }

private:
    // ── Local Transform Storage ───────────────────────────────────────────────

    Vec3 m_position{0.f, 0.f, 0.f}; ///< Local position relative to parent.
    Quat m_rotation = Quat::Identity(); ///< Local rotation relative to parent.
    Vec3 m_scale{1.f, 1.f, 1.f};    ///< Local scale relative to parent.

    // ── World Matrix Cache ────────────────────────────────────────────────────

    mutable Mat4 m_worldMatrix;         ///< Cached world-space transform matrix.
    mutable bool m_worldDirty = true;   ///< True when cache needs recomputation.

    // ── Hierarchy ─────────────────────────────────────────────────────────────

    SceneComponent* m_parent = nullptr;         ///< Non-owning parent pointer.
    std::vector<SceneComponent*> m_children;    ///< Non-owning child pointers.

    // ── Private Helpers ───────────────────────────────────────────────────────

    /// Recomputes `m_worldMatrix` from local TRS and parent world matrix.
    ///
    /// Uses DirectXMath row-vector convention: local = Scale * Rotate * Translate,
    /// then world = local * parentWorld. Clears `m_worldDirty` on completion.
    void RecalculateWorldMatrix() const;
};

} // namespace uz
