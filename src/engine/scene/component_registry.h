#pragma once

// component_registry.h — String-keyed node type registry (Godot ClassDB pattern).
//
// Solves the "growing NodeType enum" problem: node types are registered at
// startup by name, not hardcoded in an enum. The scene loader and saver are
// fully data-driven via this registry. New types (including plugin types) are
// added by a single registry call — no modification of loader, saver, renderer,
// or any other system.
//
// Usage (registration):
//   ComponentRegistry::reg({
//       "SkyAtmosphere", {0.4f,0.7f,1.0f},
//       {{ detail::component_type_id<SkyAtmosphereData>(), sizeof(SkyAtmosphereData),
//          [](void* p){ new(p) SkyAtmosphereData{}; },
//          kSkyProps, std::size(kSkyProps) }}
//   });
//
// Usage (lookup in loader/renderer):
//   const NodeTypeDef* def = ComponentRegistry::find("SkyAtmosphere");
//   if (def) { /* use def->comps, def->type_index */ }
//
// Usage (component presence, preferred in renderer/logic):
//   if (component_has<SkyAtmosphereData>(&scene->components, id)) { ... }

#include <cstdint>
#include <cstddef>
#include <string>

#include "engine/scene/component_store.h"

namespace uz {

// ============================================================================
// PropertyDef — one key=value property of a component
// ============================================================================

/// Parse function: receives a pointer to the component data and the raw value
/// string from the .uzscene file (e.g. "0.08 0.22 0.65" for a float3 property).
using PropParseFn = void(*)(void* comp, const char* val);

/// Serialize function: appends "key = value\n" to the output string.
/// Set to nullptr to skip serialization for a property (e.g. computed fields).
using PropSerFn   = void(*)(const void* comp, std::string& out);

struct ComponentProperty {
    const char*  key;       // e.g. "fov", "mesh", "sky_zenith"
    PropParseFn  parse;     // parse from .uzscene value string → component field
    PropSerFn    serialize; // component field → "key = value\n" (nullptr = skip)
};

// ============================================================================
// NodeComponentDef — one component auto-attached when a node type is created
// ============================================================================

inline constexpr uint32_t kMaxPropsPerComponent = 16;

struct NodeComponentDef {
    ComponentTypeId          type_id;       // static ID from detail::component_type_id<T>()
    size_t                   stride;        // sizeof(ComponentData)
    void                   (*construct)(void*);  // in-place default construction
    const ComponentProperty* props;         // property descriptors
    uint32_t                 prop_count;    // number of properties
};

// ============================================================================
// NodeTypeDef — full descriptor for a node type
// ============================================================================

inline constexpr uint32_t kMaxComponentsPerType = 4;

struct NodeTypeDef {
    char             name[64];              // type name, e.g. "StaticMesh"
    uint16_t         type_index;            // auto-assigned index (stable after registration)
    bool             suppress_gizmo;        // true = no editor gizmo / no shadow for this type
    float            editor_color[3];       // RGB tint for editor tree / gizmo
    NodeComponentDef comps[kMaxComponentsPerType];
    uint32_t         comp_count;
};

// ============================================================================
// ComponentRegistry — global registry, Godot ClassDB pattern
// ============================================================================

inline constexpr uint32_t kMaxNodeTypes = 64;

class ComponentRegistry {
public:
    // -----------------------------------------------------------------------
    // Registration
    // -----------------------------------------------------------------------

    /// Register a node type. Assigns type_index automatically (0, 1, 2, …).
    /// If a type with the same name is already registered, this is a no-op.
    /// Returns the assigned type_index.
    static uint16_t reg(NodeTypeDef def);

    /// Register all built-in engine types. Called once during engine init.
    /// After this call, find("StaticMesh"), find("Camera"), etc. all work.
    static void register_builtins();

    // -----------------------------------------------------------------------
    // Lookup
    // -----------------------------------------------------------------------

    /// Find a type by name. Returns nullptr if not registered.
    static const NodeTypeDef* find(const char* name);

    /// Find a type by its auto-assigned type_index. Returns nullptr if out of range.
    static const NodeTypeDef* find_by_index(uint16_t idx);

    // -----------------------------------------------------------------------
    // ComponentStore integration
    // -----------------------------------------------------------------------

    /// Ensure every component type in the registry has a pool in the given store.
    /// Call this in scene_init() after component_store.init().
    static void init_store(ComponentStore& store);

    /// Ensure ScriptData pool exists in the store (cross-cutting component).
    static void init_script_pool(ComponentStore& store);

    // -----------------------------------------------------------------------
    // Iteration (for editor type picker, etc.)
    // -----------------------------------------------------------------------

    /// Total number of registered types.
    static uint32_t count();

    /// Get the Nth registered type (for iteration).
    static const NodeTypeDef& get_at(uint32_t idx);

private:
    static NodeTypeDef  s_types[kMaxNodeTypes];
    static uint32_t     s_count;
};

} // namespace uz
