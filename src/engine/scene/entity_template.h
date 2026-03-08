#pragma once

// entity_template.h -- Reusable entity templates (prefab-like).
//
// An EntityTemplate defines a node hierarchy with component data that can be
// instantiated into a Scene. Templates are loaded from .uztemplate JSON files
// and cached by the EntityTemplateManager.
//
// .uztemplate JSON format:
// {
//   "name": "Enemy",
//   "nodes": [
//     {
//       "name": "Root",
//       "type": "StaticMesh",
//       "position": [0, 0, 0],
//       "rotation": [0, 0, 0, 1],
//       "scale": [1, 1, 1],
//       "properties": { "mesh": "res://meshes/enemy.glb", "material": "res://materials/enemy.uzmat" },
//       "children": [
//         { "name": "HitBox", "type": "Node3D", "position": [0, 1, 0] }
//       ]
//     }
//   ],
//   "overridable": ["Root.position", "Root.properties.mesh"]
// }
//
// Template nesting: a node can reference another template:
// {
//   "name": "SpawnPoint",
//   "template": "res://templates/enemy.uztemplate",
//   "position": [5, 0, 0],
//   "overrides": { "mesh": "res://meshes/boss.glb" }
// }
//
// Thread-safety: EntityTemplateManager is NOT thread-safe. All access must
// happen on the Game Logic thread.

#include "engine/scene/scene.h"
#include "engine/core/result.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace uz {

// ============================================================================
// TemplateNode -- one node in a template hierarchy
// ============================================================================

struct TemplateNode {
    std::string name;
    std::string type_name = "Node3D";

    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float scale[3]    = {1.0f, 1.0f, 1.0f};

    /// Component-specific properties. Keys match ComponentRegistry property keys.
    /// e.g. "mesh" -> "res://meshes/enemy.glb"
    std::unordered_map<std::string, std::string> properties;

    /// Child nodes (recursive hierarchy).
    std::vector<TemplateNode> children;

    /// If non-empty, this node is a nested template reference.
    /// The template at this path is instantiated as a subtree.
    std::string template_ref;

    /// Property overrides applied when instantiating a nested template.
    std::unordered_map<std::string, std::string> template_overrides;

    /// Script path attached to this node (optional).
    std::string script;
};

// ============================================================================
// EntityTemplate -- a complete template definition
// ============================================================================

struct EntityTemplate {
    std::string name;

    /// Root-level nodes in the template.
    std::vector<TemplateNode> nodes;

    /// Set of field paths that callers may override at spawn time.
    /// Format: "NodeName.property_key" (e.g. "Root.mesh", "Root.position").
    std::unordered_set<std::string> overridable;
};

// ============================================================================
// SpawnOverrides -- caller overrides applied at instantiation time
// ============================================================================

struct SpawnOverrides {
    /// Property overrides keyed by "NodeName.property_key".
    std::unordered_map<std::string, std::string> fields;

    /// Transform override for the root node (applied additively).
    bool has_position = false;
    float position[3] = {0.0f, 0.0f, 0.0f};
};

// ============================================================================
// SpawnResult -- result of template instantiation
// ============================================================================

struct SpawnResult {
    /// IDs of all nodes created (root node is first).
    std::vector<NodeId> node_ids;

    /// Warnings generated during instantiation (non-fatal).
    std::vector<std::string> warnings;
};

// ============================================================================
// EntityTemplateManager -- load, cache, instantiate templates
// ============================================================================

class EntityTemplateManager {
public:
    EntityTemplateManager() = default;
    ~EntityTemplateManager() = default;

    // Non-copyable, non-movable (owns cache state).
    EntityTemplateManager(const EntityTemplateManager&) = delete;
    EntityTemplateManager& operator=(const EntityTemplateManager&) = delete;

    // -----------------------------------------------------------------------
    // Load / Save
    // -----------------------------------------------------------------------

    /// Parse a .uztemplate JSON string into an EntityTemplate.
    /// Returns Err on parse failure.
    [[nodiscard]] static Result<EntityTemplate> parse(const std::string& json_text);

    /// Serialize an EntityTemplate to a JSON string.
    [[nodiscard]] static std::string serialize(const EntityTemplate& tmpl);

    /// Load a template from a file path. Caches the result.
    /// Returns Err if file cannot be read or parsed.
    [[nodiscard]] Result<const EntityTemplate*> load(const std::string& path);

    /// Get a previously loaded template. Returns nullptr if not cached.
    [[nodiscard]] const EntityTemplate* get(const std::string& path) const;

    /// Reload a template from disk, updating the cache.
    [[nodiscard]] Result<const EntityTemplate*> reload(const std::string& path);

    /// Remove a template from the cache.
    void unload(const std::string& path);

    /// Clear the entire cache.
    void clear();

    /// Number of cached templates.
    [[nodiscard]] size_t count() const;

    // -----------------------------------------------------------------------
    // Instantiation
    // -----------------------------------------------------------------------

    /// Instantiate a template into a scene.
    /// Creates all nodes and attaches components per the template spec.
    /// parent_id: where to attach the root nodes (kNodeIdInvalid = scene root).
    /// overrides: optional field overrides applied at spawn time.
    /// Returns SpawnResult with created node IDs and any warnings.
    [[nodiscard]] Result<SpawnResult> spawn(
        Scene* scene,
        const EntityTemplate& tmpl,
        NodeId parent_id = kNodeIdInvalid,
        const SpawnOverrides& overrides = {});

    /// Instantiate a template by path (loads if not cached).
    [[nodiscard]] Result<SpawnResult> spawn_by_path(
        Scene* scene,
        const std::string& path,
        NodeId parent_id = kNodeIdInvalid,
        const SpawnOverrides& overrides = {});

    /// Destroy all nodes created from a spawn result.
    static void despawn(Scene* scene, const SpawnResult& result);

    // -----------------------------------------------------------------------
    // Nesting depth limit (to prevent infinite recursion)
    // -----------------------------------------------------------------------

    static constexpr uint32_t kMaxNestingDepth = 16;

private:
    /// Internal: instantiate a single TemplateNode (and its children) recursively.
    Result<bool> spawn_node(
        Scene* scene,
        const TemplateNode& node,
        NodeId parent_id,
        SpawnResult& result,
        const SpawnOverrides& overrides,
        const std::string& node_prefix,
        uint32_t nesting_depth);

    /// Template cache keyed by file path.
    std::unordered_map<std::string, EntityTemplate> cache_;
};

} // namespace uz
