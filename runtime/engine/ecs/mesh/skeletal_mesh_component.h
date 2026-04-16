/// SkeletalMeshComponent — Marks an entity as skeletally animated.
///
/// The entity also carries a `StaticMeshComponent` that drives the actual
/// rendering (vertex buffer, material). The animation subsystem reads this
/// component to locate the source `.uzmesh` (which carries the skinned vertex
/// stream), the `.uzskel` skeleton, and the list of `.uzanim` clips to play.
///
/// At scene-load time the subsystem:
///   1. Loads the source mesh + skeleton through the VFS.
///   2. Registers a per-entity procedural mesh (48 B Vertex output buffer) in
///      the scene renderer and stores its ID in `StaticMeshComponent::meshId`.
///   3. Allocates an `AnimationController`, registers the `.uzanim` clips, and
///      stores the controller handle in `controllerId`.
///   4. Registers a `SkeletalMeshInstance` in `SkeletalMeshSubsystem` and
///      stores its handle in `instanceId`.
///
/// Every subsequent frame the subsystem ticks the controller, samples a pose,
/// runs CPU linear-blend-skinning, and uploads the updated 48 B Vertex[] into
/// the per-entity procedural mesh before GBuffer rendering.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// SkeletalMeshComponent
// ─────────────────────────────────────────────────────────────────────────────

struct SkeletalMeshComponent {
    /// VFS path to the `.uzmesh` (must be a UZMS skinned mesh).
    std::string meshPath;

    /// VFS path to the `.uzskel`. If empty, derived from the mesh's embedded
    /// skeleton reference (set by the FBX importer).
    std::string skeletonPath;

    /// Full list of `.uzanim` clip paths registered with the controller.
    /// Clip lookup at runtime is by `AnimationClip::name`, so multiple paths
    /// may resolve to distinct named clips (or be aliases of the same source).
    std::vector<std::string> animationClipPaths;

    /// Name of the clip to play on scene load. Matches `AnimationClip::name`
    /// (the FBX anim_stack name — e.g., "Idle" or "Walk"). Empty = bind pose.
    std::string initialClipName;

    // ── Runtime-populated handles (managed by SkeletalMeshSubsystem) ───────

    /// Handle into `SkeletalMeshSubsystem::m_records`. `UINT32_MAX` until the
    /// subsystem finishes registering the entity.
    uint32_t instanceId = UINT32_MAX;

    /// Handle into `SkeletalMeshSubsystem::m_controllers`.
    uint32_t controllerId = UINT32_MAX;
};

} // namespace uz
