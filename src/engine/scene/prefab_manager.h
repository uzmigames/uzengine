#pragma once

// prefab_manager.h -- Prefab system: load, save, instantiate, and edit prefabs.
//
// A prefab is any .uzscene file referenced via a node_instance in another scene.
// PrefabManager provides:
//   - Prefab edit mode (open/save/close with main scene caching)
//   - Instantiation of prefabs into a scene
//   - Override tracking (instance properties that differ from prefab defaults)
//   - Override revert (reset instance properties to prefab defaults)
//
// Thread safety: PrefabManager is NOT thread-safe. All calls must happen on
// the Game Logic thread (same thread that owns the Scene).

#include "engine/scene/scene.h"
#include "engine/scene/scene_loader.h"
#include "engine/scene/scene_saver.h"
#include "engine/core/result.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

// ============================================================================
// Editor mode state
// ============================================================================

enum class EditorMode : uint8_t {
    SCENE_EDIT  = 0,
    PREFAB_EDIT = 1,
};

// ============================================================================
// PrefabOverride -- a single property override on a prefab instance
// ============================================================================

struct PrefabOverride {
    std::string key;             // Property key, e.g. "position", "rotation"
    std::string instance_value;  // Serialized instance value
    std::string prefab_default;  // Serialized prefab default value
};

// ============================================================================
// PrefabInstanceData -- component data for prefab instance nodes
// ============================================================================

/// Stored on nodes that are instances of a prefab. Tracks the source prefab
/// path and any property overrides relative to the prefab defaults.
struct PrefabInstanceData {
    char prefab_source[512] = "";

    /// Property overrides: keys are property names (e.g. "position", "rotation"),
    /// values are the serialized override values.
    /// Stored as parallel arrays to keep the struct simple.
    static constexpr uint32_t kMaxOverrides = 64;
    char override_keys[kMaxOverrides][64]   = {};
    char override_values[kMaxOverrides][256] = {};
    uint32_t override_count = 0;

    /// Add or update an override. Returns true on success.
    bool set_override(const char* key, const char* value);

    /// Remove an override by key. Returns true if found and removed.
    bool remove_override(const char* key);

    /// Remove all overrides.
    void clear_overrides();

    /// Find override index by key. Returns kMaxOverrides if not found.
    uint32_t find_override(const char* key) const;
};

// ============================================================================
// PrefabManager
// ============================================================================

class PrefabManager {
public:
    PrefabManager() = default;

    /// Set the scene pointer that the manager operates on.
    /// Must be called before any other operations.
    void set_scene(Scene* scene);

    /// Get the current editor mode.
    [[nodiscard]] EditorMode editor_mode() const { return mode_; }

    /// Get the active prefab path (empty if not in prefab edit mode).
    [[nodiscard]] const std::string& active_prefab_path() const { return active_prefab_path_; }

    // ----------------------------------------------------------------
    // Prefab edit mode
    // ----------------------------------------------------------------

    /// Enter prefab edit mode. Caches the main scene, clears the scene graph,
    /// and loads the prefab file.
    /// Returns the root node ID of the loaded prefab, or Err on failure.
    [[nodiscard]] Result<NodeId> open_prefab(const char* path);

    /// Save the current prefab scene to the given file path.
    [[nodiscard]] Result<bool> save_prefab(const char* path);

    /// Exit prefab edit mode. Restores the cached main scene.
    [[nodiscard]] Result<bool> close_prefab();

    // ----------------------------------------------------------------
    // Prefab instantiation
    // ----------------------------------------------------------------

    /// Instantiate a prefab into the current scene at the given parent node.
    /// The instance is a copy of the prefab's root node subtree with a
    /// PrefabInstanceData component tracking the source.
    /// Returns the root node ID of the instance, or Err on failure.
    [[nodiscard]] Result<NodeId> instantiate_prefab(
        const char* prefab_path,
        const char* instance_name,
        NodeId parent_id = kNodeIdInvalid);

    /// Reload all instances of a prefab in the current scene.
    /// Re-reads the prefab file and updates instance subtrees while
    /// preserving overrides.
    [[nodiscard]] Result<uint32_t> reload_prefab_instances(const char* prefab_path);

    // ----------------------------------------------------------------
    // Override system
    // ----------------------------------------------------------------

    /// Get all property overrides for a prefab instance node.
    /// Returns a list of PrefabOverride entries.
    [[nodiscard]] Result<std::vector<PrefabOverride>> get_prefab_overrides(NodeId instance_id);

    /// Revert a single property override on a prefab instance.
    /// The property is reset to its prefab default value.
    [[nodiscard]] Result<bool> revert_prefab_override(NodeId instance_id, const char* key);

    /// Revert all property overrides on a prefab instance.
    [[nodiscard]] Result<bool> revert_all_prefab_overrides(NodeId instance_id);

    // ----------------------------------------------------------------
    // Query
    // ----------------------------------------------------------------

    /// Check if a node is a prefab instance (has PrefabInstanceData).
    [[nodiscard]] bool is_prefab_instance(NodeId id) const;

    /// Get the prefab source path for an instance node.
    /// Returns empty string if not a prefab instance.
    [[nodiscard]] std::string get_prefab_source(NodeId id) const;

    /// Count instances of a given prefab path in the current scene.
    [[nodiscard]] uint32_t count_instances(const char* prefab_path) const;

private:
    Scene*      scene_           = nullptr;
    EditorMode  mode_            = EditorMode::SCENE_EDIT;
    std::string active_prefab_path_;

    /// Cached main scene state (serialized as .uzscene text).
    std::string cached_main_scene_;

    /// Helper: serialize a node's transform properties to key=value strings.
    static std::string serialize_float3(float x, float y, float z);
    static std::string serialize_float4(float x, float y, float z, float w);

    /// Helper: compare a node's properties against prefab defaults.
    void compute_overrides(
        const SceneNode* instance_node,
        const SceneNode* prefab_node,
        std::vector<PrefabOverride>& out) const;

    /// Helper: apply prefab default properties to an instance node.
    void apply_prefab_defaults(SceneNode* instance_node, const SceneNode* prefab_node);
};

} // namespace uz
