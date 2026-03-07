#pragma once

// Scene Node — data types for the scene graph node tree.
//
// A SceneNode is a plain data struct stored in a flat array inside Scene.
// The node tree is implicit: each node stores its parent_id and a small
// inline child list. This gives O(1) parent lookup and O(children) iteration,
// with cache-friendly flat storage.
//
// NodeId is a simple uint32_t index. ID 0 is reserved as the "null/invalid"
// sentinel (the implicit scene root). Valid node IDs start at 1.
//
// Node names are fixed-size char arrays (64 bytes) to keep nodes in a flat
// array without heap allocations. Names longer than 63 characters are truncated.

#include "engine/scene/transform.h"

#include <cstdint>
#include "common/safe_string.h"

namespace uz {

// ============================================================================
// NodeId — plain uint32_t index into the Scene node array
// ============================================================================

using NodeId = uint32_t;

/// The null/invalid node ID. Parent=0 means child of scene root.
inline constexpr NodeId kNodeIdInvalid = 0;

// ============================================================================
// Node type — string name, not an enum
//
// The type of a node is stored as a plain string (e.g. "StaticMesh",
// "Camera", "SkyAtmosphere"). There is no central enum — new types are
// registered in ComponentRegistry without touching this header.
//
// For dispatch in rendering/physics/audio, use component_has<T>() instead
// of comparing type_name strings (Rule 2).  The type_name is used only for:
//   - Editor display (scene tree icon/color via ComponentRegistry::find())
//   - Serialization (written as "type = X" in .uzscene files)
// ============================================================================

// ============================================================================
// NodeFlags — per-node bitflags
// ============================================================================

enum NodeFlags : uint16_t {
    NODE_FLAG_NONE          = 0,
    NODE_FLAG_ACTIVE        = 1 << 0,   // Node is active (default on)
    NODE_FLAG_VISIBLE       = 1 << 1,   // Node is visible for rendering
    NODE_FLAG_TRANSFORM_DIRTY = 1 << 2, // World transform needs recomputation
};

// ============================================================================
// SceneNode — flat, cache-friendly node data
// ============================================================================

/// Maximum number of inline children per node. Nodes with more children than
/// this require the scene to manage overflow externally (not yet implemented;
/// 32 is generous for most scene trees).
inline constexpr uint32_t kMaxInlineChildren = 32;

/// Maximum length of a node name (including null terminator).
inline constexpr uint32_t kNodeNameMaxLen = 64;

struct SceneNode {
    NodeId      id;                         // This node's ID (1-based)
    NodeId      parent_id;                  // Parent node ID (0 = scene root)
    char        type_name[kNodeNameMaxLen]; // Node type string, e.g. "StaticMesh"
    uint16_t    flags;                      // NodeFlags bitfield
    char        name[kNodeNameMaxLen];       // Null-terminated name

    Transform   local_transform;            // Local-space TRS
    Mat4        world_transform;            // Cached world-space matrix

    NodeId      children[kMaxInlineChildren]; // Inline child ID list
    uint32_t    child_count;                // Number of valid entries in children[]

    bool        alive;                      // Slot is in use (false = free slot)

    /// Helper to set the name, safely truncating at kNodeNameMaxLen-1.
    void set_name(const char* n) noexcept {
        if (!n) {
            name[0] = '\0';
            return;
        }
        uz::str_copy(name, sizeof(name), n);
    }
};

} // namespace uz
