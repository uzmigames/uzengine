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
#include <cstring>

namespace uz {

// ============================================================================
// NodeId — plain uint32_t index into the Scene node array
// ============================================================================

using NodeId = uint32_t;

/// The null/invalid node ID. Parent=0 means child of scene root.
inline constexpr NodeId kNodeIdInvalid = 0;

// ============================================================================
// NodeType — discriminator for node behavior
// ============================================================================

enum class NodeType : uint16_t {
    Empty           = 0,    // Named container, no transform
    Node3D          = 1,    // 3D node with local transform
    StaticMesh      = 2,    // Renderable static mesh
    DirectionalLight = 3,   // Directional light
    PointLight      = 4,    // Point light
    SpotLight       = 5,    // Spot light
    Camera          = 6,    // Camera node
    AudioSource     = 7,    // Audio emitter
    AudioListener   = 8,    // Audio receiver

    Count                   // Sentinel — not a valid type
};

/// Convert a NodeType to its string name. Returns "Unknown" for invalid types.
[[nodiscard]] inline const char* node_type_to_string(NodeType type) noexcept {
    switch (type) {
        case NodeType::Empty:            return "Empty";
        case NodeType::Node3D:           return "Node3D";
        case NodeType::StaticMesh:       return "StaticMesh";
        case NodeType::DirectionalLight: return "DirectionalLight";
        case NodeType::PointLight:       return "PointLight";
        case NodeType::SpotLight:        return "SpotLight";
        case NodeType::Camera:           return "Camera";
        case NodeType::AudioSource:      return "AudioSource";
        case NodeType::AudioListener:    return "AudioListener";
        default:                         return "Unknown";
    }
}

/// Parse a string to a NodeType. Returns NodeType::Empty if unrecognized.
[[nodiscard]] inline NodeType node_type_from_string(const char* str) noexcept {
    if (!str) return NodeType::Empty;
    if (std::strcmp(str, "Empty") == 0)            return NodeType::Empty;
    if (std::strcmp(str, "Node3D") == 0)            return NodeType::Node3D;
    if (std::strcmp(str, "StaticMesh") == 0)        return NodeType::StaticMesh;
    if (std::strcmp(str, "DirectionalLight") == 0)  return NodeType::DirectionalLight;
    if (std::strcmp(str, "PointLight") == 0)        return NodeType::PointLight;
    if (std::strcmp(str, "SpotLight") == 0)          return NodeType::SpotLight;
    if (std::strcmp(str, "Camera") == 0)            return NodeType::Camera;
    if (std::strcmp(str, "AudioSource") == 0)        return NodeType::AudioSource;
    if (std::strcmp(str, "AudioListener") == 0)      return NodeType::AudioListener;
    return NodeType::Empty;
}

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
    NodeType    type;                       // Node type discriminator
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
        std::strncpy(name, n, kNodeNameMaxLen - 1);
        name[kNodeNameMaxLen - 1] = '\0';
    }
};

} // namespace uz
