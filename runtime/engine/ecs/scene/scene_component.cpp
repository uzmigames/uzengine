/// SceneComponent implementation — local TRS transform, lazy world-matrix
/// cache, and parent-child hierarchy management.
///
/// # Matrix Convention
/// DirectXMath uses row-vector convention: a point `p` is transformed as
/// `p' = p * M`. Consequently, the TRS matrix is built as:
///     local = Scale * Rotate * Translate
/// and the world matrix is:
///     world = local * parentWorld
/// This matches DirectXMath's XMMatrixScaling/RotationQuaternion/Translation
/// multiplication order used elsewhere in the engine.

#include "scene_component.h"

namespace uz {
using namespace DirectX;

// ─────────────────────────────────────────────────────────────────────────────
// Destructor
// ─────────────────────────────────────────────────────────────────────────────

/// Detaches this component from its parent and orphans all children.
///
/// Children are not destroyed — they simply lose their parent pointer and
/// their world matrices are invalidated. Lifetime is managed by the ECS World.
SceneComponent::~SceneComponent() {
    // Detach from parent first to keep the parent's children list consistent.
    Detach();

    // Orphan all children. We iterate a copy because each child's Detach()
    // would modify m_children while we're iterating it.
    std::vector<SceneComponent*> childrenCopy = m_children;
    for (SceneComponent* child : childrenCopy) {
        if (child) {
            // Clear the child's back-pointer directly to avoid a redundant
            // linear scan inside child->Detach(), which would try to remove
            // the child from our (already-being-destroyed) m_children.
            child->m_parent = nullptr;
            child->MarkDirty();
        }
    }
    m_children.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Local Transform Setters
// ─────────────────────────────────────────────────────────────────────────────

/// Sets the local position and propagates dirty flags to this and all descendants.
void SceneComponent::SetPosition(const Vec3& pos) {
    m_position = pos;
    MarkDirty();
    MarkCBDirty(TransformDirty::Position);
}

/// Sets the local rotation and propagates dirty flags to this and all descendants.
void SceneComponent::SetRotation(const Quat& rot) {
    m_rotation = rot;
    MarkDirty();
    MarkCBDirty(TransformDirty::Rotation);
}

/// Sets the local scale and propagates dirty flags to this and all descendants.
void SceneComponent::SetScale(const Vec3& scl) {
    m_scale = scl;
    MarkDirty();
    MarkCBDirty(TransformDirty::Scale);
}

// ─────────────────────────────────────────────────────────────────────────────
// World Matrix (cached, lazy)
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the cached world-space matrix, recomputing it if dirty.
const Mat4& SceneComponent::GetWorldMatrix() const {
    if (m_worldDirty) {
        RecalculateWorldMatrix();
    }
    return m_worldMatrix;
}

/// Extracts the world-space translation from the world matrix.
Vec3 SceneComponent::GetWorldPosition() const {
    return GetWorldMatrix().getTranslation();
}

// ─────────────────────────────────────────────────────────────────────────────
// Hierarchy
// ─────────────────────────────────────────────────────────────────────────────

/// Attaches this component as a child of `parent`, detaching from any current parent first.
void SceneComponent::AttachTo(SceneComponent* parent) {
    if (parent == m_parent) {
        return; // Already attached to this parent — nothing to do.
    }

    // Detach from the existing parent before reparenting.
    Detach();

    m_parent = parent;
    if (m_parent) {
        m_parent->m_children.push_back(this);
    }

    // Recompute world matrix relative to the new parent.
    MarkDirty();
}

/// Detaches this component from its current parent.
void SceneComponent::Detach() {
    if (!m_parent) {
        return; // Already a root component — nothing to do.
    }

    // Remove this component from the parent's children list.
    auto& siblings = m_parent->m_children;
    for (auto it = siblings.begin(); it != siblings.end(); ++it) {
        if (*it == this) {
            siblings.erase(it);
            break;
        }
    }

    m_parent = nullptr;
    MarkDirty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Dirty Flag Propagation
// ─────────────────────────────────────────────────────────────────────────────

/// Marks this component and all descendants dirty (iterative BFS to avoid
/// stack overflow in deep hierarchies).
void SceneComponent::MarkDirty() {
    // Use an explicit stack (iterative DFS) instead of recursion so that
    // very deep hierarchies cannot overflow the call stack.
    // A small inline buffer avoids heap allocation for the common shallow case.
    std::vector<SceneComponent*> stack;
    stack.reserve(16);
    stack.push_back(this);

    while (!stack.empty()) {
        SceneComponent* current = stack.back();
        stack.pop_back();

        if (current->m_worldDirty) {
            // Already dirty — all descendants are dirty too (invariant upheld
            // by previous MarkDirty calls), so we can prune this subtree.
            continue;
        }

        current->m_worldDirty = true;
        for (SceneComponent* child : current->m_children) {
            stack.push_back(child);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private — World Matrix Computation
// ─────────────────────────────────────────────────────────────────────────────

/// Recomputes the cached world matrix from the local TRS and the parent chain.
///
/// DirectXMath row-vector order: Scale * Rotate * Translate, then multiply
/// by parent world matrix to accumulate ancestor transforms.
void SceneComponent::RecalculateWorldMatrix() const {
    // Build local TRS matrix in Scale * Rotate * Translate order.
    // With row-vector convention: vertex_world = vertex_local * S * R * T
    const Mat4 scaleMatrix    = Mat4::Scaling(m_scale);
    const Mat4 rotationMatrix = m_rotation.toMatrix();
    const Mat4 translateMatrix = Mat4::Translation(m_position);

    // Combine: local = S * R * T
    const Mat4 local = scaleMatrix * rotationMatrix * translateMatrix;

    if (m_parent) {
        // world = local * parentWorld (row-vector: point transforms left-to-right)
        m_worldMatrix = local * m_parent->GetWorldMatrix();
    } else {
        m_worldMatrix = local;
    }

    m_worldDirty = false;
}

} // namespace uz
