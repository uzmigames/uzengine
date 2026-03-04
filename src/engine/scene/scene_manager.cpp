// scene_manager.cpp — Scene graph implementation.
//
// Implements all functions declared in scene.h:
//   scene_init, scene_clear, scene_create_node, scene_destroy_node,
//   scene_set_parent, scene_get_node, scene_find_by_name,
//   scene_iter_children, scene_get_world_transform,
//   scene_update_world_transforms, scene_collect_visible,
//   scene_is_descendant_of.
//
// No heap allocations. All data lives in the flat Scene::nodes[] array.

#include "engine/scene/scene.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_store.h"

#include <cstring>

namespace uz {

// ============================================================================
// Internal helpers
// ============================================================================

/// Remove `child_id` from `parent`'s inline children list.
/// Maintains order by shifting subsequent entries down.
static void remove_child_from_parent(SceneNode* parent, NodeId child_id) noexcept {
    for (uint32_t i = 0; i < parent->child_count; ++i) {
        if (parent->children[i] == child_id) {
            // Shift remaining children down by one.
            for (uint32_t j = i; j + 1 < parent->child_count; ++j) {
                parent->children[j] = parent->children[j + 1];
            }
            --parent->child_count;
            return;
        }
    }
}

/// Add `child_id` to `parent`'s inline children list.
/// Returns true on success, false if the child list is already full.
static bool add_child_to_parent(SceneNode* parent, NodeId child_id) noexcept {
    if (parent->child_count >= kMaxInlineChildren) {
        return false;
    }
    parent->children[parent->child_count] = child_id;
    ++parent->child_count;
    return true;
}

// ============================================================================
// Scene lifecycle
// ============================================================================

void scene_init(Scene* scene, const char* name) {
    std::memset(scene, 0, sizeof(Scene));

    // Copy the scene name, safely truncating.
    if (name) {
        std::strncpy(scene->name, name, kNodeNameMaxLen - 1);
        scene->name[kNodeNameMaxLen - 1] = '\0';
    }

    scene->version = 1;
    scene->next_id = 1;
    // node_count is already 0 from memset.
    // All node slots have alive=false from memset.

    // Initialize the component store and register known component types.
    scene->components.init();
    component_store_register<CameraData>(&scene->components);
    component_store_register<DirectionalLightData>(&scene->components);
}

void scene_clear(Scene* scene) {
    // Mark every slot as dead and remove components.
    for (uint32_t i = 1; i < scene->next_id; ++i) {
        scene->nodes[i].alive = false;
        scene->components.remove_all(i);
    }
    scene->node_count = 0;
    scene->next_id = 1;
}

// ============================================================================
// Node creation / destruction
// ============================================================================

NodeId scene_create_node(Scene* scene, const char* name, NodeType type, NodeId parent_id) {
    // Scene is full if we have used kSceneMaxNodes-1 slots (slot 0 is reserved).
    if (scene->node_count >= kSceneMaxNodes - 1) {
        return kNodeIdInvalid;
    }

    // Find a free slot. First try to reclaim a dead slot in [1..next_id).
    uint32_t slot = 0;
    for (uint32_t i = 1; i < scene->next_id; ++i) {
        if (!scene->nodes[i].alive) {
            slot = i;
            break;
        }
    }

    // If no dead slot found, use next_id (expand the used range).
    if (slot == 0) {
        if (scene->next_id >= kSceneMaxNodes) {
            return kNodeIdInvalid;
        }
        slot = scene->next_id;
        ++scene->next_id;
    }

    // Initialize the node.
    SceneNode* node = &scene->nodes[slot];
    std::memset(node, 0, sizeof(SceneNode));

    node->id             = slot;
    node->parent_id      = parent_id;
    node->type           = type;
    node->flags          = NODE_FLAG_ACTIVE | NODE_FLAG_VISIBLE;
    node->local_transform = Transform::identity();
    node->world_transform = Mat4::identity();
    node->child_count    = 0;
    node->alive          = true;

    node->set_name(name);

    // Register as child of parent (if parent is a valid alive node).
    if (parent_id != kNodeIdInvalid
        && parent_id < scene->next_id
        && scene->nodes[parent_id].alive) {
        add_child_to_parent(&scene->nodes[parent_id], slot);
    }

    ++scene->node_count;
    return slot;
}

void scene_destroy_node(Scene* scene, NodeId id) {
    if (id == kNodeIdInvalid || id >= kSceneMaxNodes) {
        return;
    }

    SceneNode* node = &scene->nodes[id];
    if (!node->alive) {
        return;
    }

    // Recursively destroy children first. Copy the children array because
    // scene_destroy_node will modify it as each child is removed.
    NodeId children_copy[kMaxInlineChildren];
    const uint32_t child_count = node->child_count;
    std::memcpy(children_copy, node->children, sizeof(NodeId) * child_count);

    for (uint32_t i = 0; i < child_count; ++i) {
        scene_destroy_node(scene, children_copy[i]);
    }

    // Remove this node from its parent's child list.
    if (node->parent_id != kNodeIdInvalid
        && node->parent_id < scene->next_id
        && scene->nodes[node->parent_id].alive) {
        remove_child_from_parent(&scene->nodes[node->parent_id], id);
    }

    // Remove all components for this node.
    scene->components.remove_all(id);

    // Mark slot as dead.
    node->alive = false;
    --scene->node_count;
}

// ============================================================================
// Hierarchy manipulation
// ============================================================================

bool scene_set_parent(Scene* scene, NodeId id, NodeId new_parent) {
    // Validate node id.
    if (id == kNodeIdInvalid || id >= kSceneMaxNodes) {
        return false;
    }
    if (!scene->nodes[id].alive) {
        return false;
    }

    // Validate new parent (kNodeIdInvalid = root, which is always valid).
    if (new_parent != kNodeIdInvalid) {
        if (new_parent >= kSceneMaxNodes || !scene->nodes[new_parent].alive) {
            return false;
        }
        // Prevent cycles: new_parent must not be a descendant of id.
        if (scene_is_descendant_of(scene, new_parent, id)) {
            return false;
        }
        // Cannot re-parent a node to itself.
        if (new_parent == id) {
            return false;
        }
    }

    SceneNode* node = &scene->nodes[id];
    const NodeId old_parent = node->parent_id;

    // No-op if parent hasn't changed.
    if (old_parent == new_parent) {
        return true;
    }

    // Remove from old parent's child list.
    if (old_parent != kNodeIdInvalid
        && old_parent < scene->next_id
        && scene->nodes[old_parent].alive) {
        remove_child_from_parent(&scene->nodes[old_parent], id);
    }

    // Add to new parent's child list.
    if (new_parent != kNodeIdInvalid) {
        if (!add_child_to_parent(&scene->nodes[new_parent], id)) {
            // New parent's child list is full. Revert to old parent.
            if (old_parent != kNodeIdInvalid
                && old_parent < scene->next_id
                && scene->nodes[old_parent].alive) {
                add_child_to_parent(&scene->nodes[old_parent], id);
            }
            return false;
        }
    }

    node->parent_id = new_parent;
    return true;
}

// ============================================================================
// Node access
// ============================================================================

SceneNode* scene_get_node(Scene* scene, NodeId id) {
    if (id == kNodeIdInvalid || id >= kSceneMaxNodes) {
        return nullptr;
    }
    SceneNode* node = &scene->nodes[id];
    if (!node->alive) {
        return nullptr;
    }
    return node;
}

const SceneNode* scene_get_node(const Scene* scene, NodeId id) {
    if (id == kNodeIdInvalid || id >= kSceneMaxNodes) {
        return nullptr;
    }
    const SceneNode* node = &scene->nodes[id];
    if (!node->alive) {
        return nullptr;
    }
    return node;
}

// ============================================================================
// Name search
// ============================================================================

NodeId scene_find_by_name(const Scene* scene, const char* name) {
    if (!name) {
        return kNodeIdInvalid;
    }

    for (uint32_t i = 1; i < scene->next_id; ++i) {
        if (scene->nodes[i].alive && std::strcmp(scene->nodes[i].name, name) == 0) {
            return i;
        }
    }
    return kNodeIdInvalid;
}

// ============================================================================
// Child iteration
// ============================================================================

void scene_iter_children(Scene* scene, NodeId parent_id, NodeVisitFn fn, void* user_data) {
    if (!fn) {
        return;
    }

    for (uint32_t i = 1; i < scene->next_id; ++i) {
        SceneNode* node = &scene->nodes[i];
        if (node->alive && node->parent_id == parent_id) {
            if (!fn(scene, node, user_data)) {
                return; // Early exit requested by callback.
            }
        }
    }
}

// ============================================================================
// Transform system
// ============================================================================

Mat4 scene_get_world_transform(const Scene* scene, NodeId id) {
    if (id == kNodeIdInvalid || id >= kSceneMaxNodes || !scene->nodes[id].alive) {
        return Mat4::identity();
    }

    // Walk up the parent chain, collecting transforms in child-to-root order.
    // Then multiply root-to-child to get the final world matrix.
    //
    // Maximum depth is bounded by kSceneMaxNodes (no cycles possible in a
    // well-formed tree), but in practice scenes are far shallower.
    // We use a stack to avoid recursion.

    // Collect the chain of node IDs from this node up to the root.
    NodeId chain[256]; // Generous stack depth for parent chains.
    uint32_t depth = 0;

    NodeId current = id;
    while (current != kNodeIdInvalid && depth < 256) {
        if (current >= kSceneMaxNodes || !scene->nodes[current].alive) {
            break;
        }
        chain[depth++] = current;
        current = scene->nodes[current].parent_id;
    }

    // Multiply from root (last in chain) down to the node (first in chain).
    Mat4 world = Mat4::identity();
    for (uint32_t i = depth; i > 0; --i) {
        const SceneNode& n = scene->nodes[chain[i - 1]];
        world = world * transform_to_mat4(n.local_transform);
    }

    return world;
}

void scene_update_world_transforms(Scene* scene) {
    // Process nodes in topological order: parents before children.
    // Since parent IDs are always less than their children's IDs when the tree
    // is built top-down, a simple forward pass over the array handles most
    // cases. However, re-parenting can break this invariant, so we use a
    // two-pass approach:
    //
    // Pass 1: Process root-level nodes (parent_id == kNodeIdInvalid) — their
    //         world_transform is just their local_transform as a Mat4.
    // Pass 2: Process all alive nodes whose parent is alive — multiply
    //         parent.world_transform * local_to_mat4().
    //
    // Because a parent's slot index may be greater than a child's after
    // re-parenting, we iterate multiple passes until all nodes are processed.
    // In practice, this is almost always 1 pass for well-formed trees.

    // Mark all alive nodes as needing update.
    bool updated[kSceneMaxNodes];
    std::memset(updated, 0, sizeof(bool) * scene->next_id);

    // First pass: root-level nodes (no parent).
    for (uint32_t i = 1; i < scene->next_id; ++i) {
        SceneNode& node = scene->nodes[i];
        if (!node.alive) {
            continue;
        }
        if (node.parent_id == kNodeIdInvalid) {
            node.world_transform = transform_to_mat4(node.local_transform);
            updated[i] = true;
        }
    }

    // Iterative passes: keep going until no more nodes can be updated.
    // This handles arbitrary parent ordering in the flat array.
    bool progress = true;
    while (progress) {
        progress = false;
        for (uint32_t i = 1; i < scene->next_id; ++i) {
            if (updated[i]) {
                continue;
            }
            SceneNode& node = scene->nodes[i];
            if (!node.alive) {
                continue;
            }
            // If parent is valid and already updated, we can compute this node.
            const NodeId pid = node.parent_id;
            if (pid != kNodeIdInvalid && pid < kSceneMaxNodes
                && scene->nodes[pid].alive && updated[pid]) {
                node.world_transform = scene->nodes[pid].world_transform
                                       * transform_to_mat4(node.local_transform);
                updated[i] = true;
                progress = true;
            }
        }
    }
}

// ============================================================================
// Visibility culling
// ============================================================================

uint32_t scene_collect_visible(const Scene* scene, const Frustum& frustum, NodeId* out_nodes, uint32_t max_count) {
    if (!out_nodes || max_count == 0) {
        return 0;
    }

    uint32_t count = 0;

    for (uint32_t i = 1; i < scene->next_id && count < max_count; ++i) {
        const SceneNode& node = scene->nodes[i];
        if (!node.alive) {
            continue;
        }

        // Only test nodes that are flagged as visible.
        if (!(node.flags & NODE_FLAG_VISIBLE)) {
            continue;
        }

        // Extract world position from the world_transform matrix.
        // Mat4 is column-major: m[col][row]. Translation is in column 3.
        const float px = node.world_transform.m[3][0];
        const float py = node.world_transform.m[3][1];
        const float pz = node.world_transform.m[3][2];

        // Test the point against all 6 frustum planes.
        // A point is inside the frustum if, for every plane (nx, ny, nz, d):
        //   nx*px + ny*py + nz*pz + d >= 0
        bool inside = true;
        for (int p = 0; p < 6; ++p) {
            const Vec4& plane = frustum.planes[p];
            const float dist = plane.x * px + plane.y * py + plane.z * pz + plane.w;
            if (dist < 0.0f) {
                inside = false;
                break;
            }
        }

        if (inside) {
            out_nodes[count++] = node.id;
        }
    }

    return count;
}

// ============================================================================
// Utility
// ============================================================================

bool scene_is_descendant_of(const Scene* scene, NodeId descendant, NodeId ancestor) {
    if (descendant == kNodeIdInvalid || ancestor == kNodeIdInvalid) {
        return false;
    }
    if (descendant == ancestor) {
        return false; // A node is not a descendant of itself.
    }
    if (ancestor >= kSceneMaxNodes || descendant >= kSceneMaxNodes) {
        return false;
    }
    if (!scene->nodes[ancestor].alive || !scene->nodes[descendant].alive) {
        return false;
    }

    // Walk up from descendant following parent_id until we find ancestor or
    // reach the root (kNodeIdInvalid). Guard against cycles with a step limit.
    NodeId current = scene->nodes[descendant].parent_id;
    uint32_t steps = 0;
    const uint32_t max_steps = kSceneMaxNodes; // Absolute upper bound.

    while (current != kNodeIdInvalid && steps < max_steps) {
        if (current == ancestor) {
            return true;
        }
        if (current >= kSceneMaxNodes || !scene->nodes[current].alive) {
            break;
        }
        current = scene->nodes[current].parent_id;
        ++steps;
    }

    return false;
}

} // namespace uz
