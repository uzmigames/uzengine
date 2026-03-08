#pragma once

// Scene — flat-array scene graph with parent-child hierarchy.
//
// The Scene owns a fixed-capacity flat array of SceneNode slots (max 65536).
// Node creation returns a NodeId (1-based index). Slot 0 is reserved as the
// implicit scene root sentinel and is never allocated to user nodes.
//
// Design rationale:
//   - Flat array = cache-friendly iteration, no pointer chasing.
//   - Fixed capacity = no heap allocations during gameplay (arena-friendly).
//   - Parent-child encoded via parent_id + inline children[] = O(1) parent,
//     O(children) iteration.
//   - World transforms cached in each node, updated by scene_update_world_transforms().
//
// Thread-safety: Scene is NOT thread-safe. All mutations must happen on the
// Game Logic thread (same thread that owns the scene).

#include "engine/scene/scene_node.h"
#include "engine/scene/component_store.h"
#include "engine/core/math.h"

#include <cstdint>

namespace uz {

// ============================================================================
// Scene capacity
// ============================================================================

/// Maximum number of nodes a Scene can hold (including the reserved slot 0).
inline constexpr uint32_t kSceneMaxNodes = 65536;

// ============================================================================
// Frustum — 6-plane frustum for visibility culling
// ============================================================================

/// A frustum defined by 6 planes (left, right, top, bottom, near, far).
/// Each plane is stored as (nx, ny, nz, d) where nx*x + ny*y + nz*z + d >= 0
/// means the point is on the inside.
struct Frustum {
    Vec4 planes[6];
};

// ============================================================================
// Scene
// ============================================================================

/// Editor-only state persisted in the [editor] section of .uzscene files.
/// This lets the editor camera position survive scene save/load cycles.
struct EditorState {
    float cam_pos_x   = 0.0f;
    float cam_pos_y   = 15.0f;
    float cam_pos_z   = -20.0f;
    float cam_yaw     = 0.0f;
    float cam_pitch   = -0.6f;
    float cam_speed   = 10.0f;
    bool  valid       = false;   ///< true if populated from a loaded file
};

struct Scene {
    char        name[kNodeNameMaxLen];   // Scene name
    uint32_t    version;                 // Format version (currently 1)
    SceneNode   nodes[kSceneMaxNodes];   // Flat node array (slot 0 reserved)
    uint32_t    node_count;              // Number of alive nodes (not counting slot 0)
    uint32_t    next_id;                 // Next ID to assign (monotonically increasing)

    /// Generic component storage (keyed by NodeId + ComponentTypeId).
    /// Must be initialized via scene_init() before use.
    ComponentStore components;

    /// Preferred render path: 0 = Forward, 1 = Deferred (default: 1).
    /// Renderer-agnostic integer to avoid including render headers.
    uint8_t render_path = 1;

    /// Post-process rendering settings (renderer-agnostic, plain floats).
    /// Parsed from [uzscene] section keys. Applied by scene_bridge.
    float   ao_radius           = 0.5f;
    float   ao_intensity        = 1.5f;
    bool    contact_shadows     = true;
    float   contact_shadow_length = 0.1f;
    bool    taa_enabled         = true;
    uint8_t exposure_mode       = 0;       ///< 0 = auto, 1 = manual
    float   exposure_value      = 1.0f;    ///< manual exposure multiplier
    bool    ssr_enabled         = true;
    float   ssr_max_roughness   = 0.5f;    ///< SSR max roughness threshold
    float   color_temperature   = 0.0f;    ///< white balance temperature (-1..1)
    float   color_tint          = 0.0f;    ///< white balance tint (-1..1)
    float   color_saturation    = 1.0f;    ///< saturation multiplier (0..2)
    float   color_contrast      = 1.0f;    ///< contrast multiplier (0..2)
    // Cinematic post-process
    float   chromatic_aberration = 0.0f;   ///< radial CA intensity (0=off)
    float   vignette_intensity  = 0.0f;    ///< vignette darkness (0=off)
    float   vignette_falloff    = 2.0f;    ///< vignette falloff power
    float   film_grain_strength = 0.0f;    ///< noise overlay (0=off)
    // Depth of Field
    float   dof_focus_distance  = 0.0f;    ///< 0=disabled
    float   dof_aperture        = 5.6f;    ///< f-stop
    float   dof_focal_length    = 50.0f;   ///< mm
    // Volumetric Fog
    float   fog_density         = 0.0f;    ///< 0=disabled
    float   fog_color[3]        = {0.5f, 0.6f, 0.7f};
    float   fog_height_falloff  = 0.5f;
    float   fog_base_height     = 0.0f;
    float   fog_max_opacity     = 0.95f;
    // Volumetric Clouds
    bool    clouds_enabled      = false;
    float   cloud_coverage      = 0.5f;    ///< sky coverage [0..1]
    float   cloud_density       = 0.15f;   ///< extinction coefficient multiplier
    float   cloud_base          = 800.0f;  ///< cloud layer bottom (meters)
    float   cloud_top           = 2500.0f; ///< cloud layer top (meters)
    float   cloud_wind_speed    = 8.0f;    ///< drift speed (m/s)
    float   cloud_wind_dir_x    = 1.0f;    ///< wind direction X (normalized)
    float   cloud_wind_dir_z    = 0.0f;    ///< wind direction Z (normalized)
    float   cloud_absorption    = 0.85f;   ///< single-scattering albedo
    float   cloud_ambient       = 0.5f;    ///< ambient sky contribution
    uint32_t cloud_quality      = 2;       ///< 0=LOW, 1=MEDIUM, 2=HIGH, 3=CINEMATIC
    bool    cloud_temporal      = true;    ///< temporal accumulation enabled

    /// Path of the .uzscene file this scene was loaded from.
    /// Empty if the scene was created programmatically (not from a file).
    char source_path[512];

    /// Editor camera state loaded from the [editor] section. Populated by
    /// scene_load_file / scene_load_from_string when an [editor] section exists.
    EditorState editor_state;
};

// ============================================================================
// Scene lifecycle
// ============================================================================

/// Initialize a Scene to its default empty state.
/// Zeroes all node slots and sets next_id to 1.
void scene_init(Scene* scene, const char* name = "Untitled");

/// Clear all nodes from the scene, resetting to empty.
void scene_clear(Scene* scene);

// ============================================================================
// Node creation / destruction
// ============================================================================

/// Create a new node in the scene.
/// Returns the new node's ID, or kNodeIdInvalid if the scene is full.
/// type_name is stored on the node for editor display and serialization.
/// Component data is attached separately via component_add<T>().
/// If parent_id is kNodeIdInvalid (0), the node is a root-level child.
[[nodiscard]] NodeId scene_create_node(Scene* scene, const char* name,
                                       const char* type_name = "Node3D",
                                       NodeId parent_id = kNodeIdInvalid);

/// Destroy a node and all its descendants recursively.
/// Removes the node from its parent's child list.
/// Does nothing if id is invalid or the node is not alive.
void scene_destroy_node(Scene* scene, NodeId id);

// ============================================================================
// Hierarchy manipulation
// ============================================================================

/// Re-parent a node. Removes it from its old parent's child list and adds it
/// to the new parent's child list. Set new_parent to kNodeIdInvalid for root.
/// Returns true on success, false if the node or new parent is invalid, or if
/// new_parent is a descendant of id (would create a cycle).
bool scene_set_parent(Scene* scene, NodeId id, NodeId new_parent);

// ============================================================================
// Node access
// ============================================================================

/// Get a mutable pointer to a node by ID. Returns nullptr if the ID is invalid
/// or the node is not alive.
[[nodiscard]] SceneNode* scene_get_node(Scene* scene, NodeId id);

/// Get a const pointer to a node by ID.
[[nodiscard]] const SceneNode* scene_get_node(const Scene* scene, NodeId id);

/// Find the first node with the given name. Returns kNodeIdInvalid if not found.
/// Linear scan over all alive nodes.
[[nodiscard]] NodeId scene_find_by_name(const Scene* scene, const char* name);

// ============================================================================
// Child iteration
// ============================================================================

/// Callback type for scene_iter_children. Return true to continue, false to stop.
using NodeVisitFn = bool(*)(Scene* scene, SceneNode* node, void* user_data);

/// Iterate over the direct children of a node.
/// Calls `fn` for each child. If fn returns false, iteration stops early.
/// Pass parent_id = kNodeIdInvalid to iterate root-level nodes.
void scene_iter_children(Scene* scene, NodeId parent_id, NodeVisitFn fn, void* user_data = nullptr);

// ============================================================================
// Transform system
// ============================================================================

/// Get the world transform matrix for a node by traversing the parent chain.
/// This is the "on-demand" version that walks up to the root every call.
/// For batch updates, use scene_update_world_transforms() instead.
[[nodiscard]] Mat4 scene_get_world_transform(const Scene* scene, NodeId id);

/// Update all world transforms in the scene.
/// Traverses the node tree and computes world_transform = parent_world * local_to_mat4().
/// Should be called once per frame before rendering.
void scene_update_world_transforms(Scene* scene);

// ============================================================================
// Visibility culling
// ============================================================================

/// Collect node IDs whose world-space origin is inside the frustum.
/// This is a simple point-in-frustum test (no AABB). Writes to out_nodes
/// up to max_count entries. Returns the number of nodes written.
[[nodiscard]] uint32_t scene_collect_visible(const Scene* scene, const Frustum& frustum, NodeId* out_nodes, uint32_t max_count);

// ============================================================================
// Utility
// ============================================================================

/// Check if `descendant` is a descendant of `ancestor` (at any depth).
/// Returns false if either ID is invalid.
[[nodiscard]] bool scene_is_descendant_of(const Scene* scene, NodeId descendant, NodeId ancestor);

} // namespace uz
