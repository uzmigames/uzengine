// component_registry.cpp — ComponentRegistry implementation + built-in type registration.
//
// Adding a new node type: add a static ComponentProperty array and call
// ComponentRegistry::reg() in register_builtins(). Zero other files change.

#include "engine/scene/component_registry.h"
#include "engine/scene/component_data.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace uz {

// ============================================================================
// Static storage
// ============================================================================

NodeTypeDef ComponentRegistry::s_types[kMaxNodeTypes] = {};
uint32_t    ComponentRegistry::s_count = 0;

// ============================================================================
// Registry operations
// ============================================================================

uint16_t ComponentRegistry::reg(NodeTypeDef def) {
    // Idempotent: if already registered, return existing index.
    for (uint32_t i = 0; i < s_count; ++i) {
        if (std::strcmp(s_types[i].name, def.name) == 0)
            return s_types[i].type_index;
    }
    if (s_count >= kMaxNodeTypes) return 0xFFFF;

    def.type_index = static_cast<uint16_t>(s_count);
    s_types[s_count] = def;
    ++s_count;
    return def.type_index;
}

const NodeTypeDef* ComponentRegistry::find(const char* name) {
    if (!name) return nullptr;
    for (uint32_t i = 0; i < s_count; ++i) {
        if (std::strcmp(s_types[i].name, name) == 0)
            return &s_types[i];
    }
    return nullptr;
}

const NodeTypeDef* ComponentRegistry::find_by_index(uint16_t idx) {
    if (idx >= s_count) return nullptr;
    return &s_types[idx];
}

void ComponentRegistry::init_store(ComponentStore& store) {
    for (uint32_t ti = 0; ti < s_count; ++ti) {
        const NodeTypeDef& def = s_types[ti];
        for (uint32_t ci = 0; ci < def.comp_count; ++ci) {
            const NodeComponentDef& cd = def.comps[ci];
            // Ensure the pool exists. register_type is idempotent via stride check.
            if (cd.type_id < store.type_count && store.pools[cd.type_id].stride > 0)
                continue; // Already registered.
            store.register_type(static_cast<uint32_t>(cd.stride));
        }
    }
}

uint32_t ComponentRegistry::count() { return s_count; }

const NodeTypeDef& ComponentRegistry::get_at(uint32_t idx) {
    return s_types[idx < s_count ? idx : 0];
}

// ============================================================================
// Property parser helpers (file-scope, not exported)
// ============================================================================

static float pf(const char* v) { return std::strtof(v, nullptr); }

static void parse_f3(const char* v, float* out) {
    const char* p = v;
    for (int i = 0; i < 3; ++i) {
        while (*p == ' ' || *p == '\t') ++p;
        char* end = nullptr;
        out[i] = std::strtof(p, &end);
        if (!end || end == p) { out[i] = 0.0f; return; }
        p = end;
    }
}

// ============================================================================
// Built-in type: Node3D (no component — just a named transform)
// ============================================================================

static void register_node3d() {
    NodeTypeDef def{};
    std::strncpy(def.name, "Node3D", 64);
    def.editor_color[0] = 0.8f; def.editor_color[1] = 0.8f; def.editor_color[2] = 0.8f;
    def.comp_count = 0;
    ComponentRegistry::reg(def);
}

// ============================================================================
// Built-in type: StaticMesh
// ============================================================================

static ComponentProperty kStaticMeshProps[] = {
    {
        "mesh",
        [](void* d, const char* v) {
            auto* s = static_cast<StaticMeshData*>(d);
            std::strncpy(s->mesh_path, v, sizeof(s->mesh_path) - 1);
            s->mesh_path[sizeof(s->mesh_path) - 1] = '\0';
        },
        [](const void* d, std::string& out) {
            const auto* s = static_cast<const StaticMeshData*>(d);
            if (s->mesh_path[0] != '\0') {
                char buf[300];
                std::snprintf(buf, sizeof(buf), "mesh = %s\n", s->mesh_path);
                out += buf;
            }
        }
    },
    {
        "material",
        [](void* d, const char* v) {
            auto* s = static_cast<StaticMeshData*>(d);
            std::strncpy(s->material_path, v, sizeof(s->material_path) - 1);
            s->material_path[sizeof(s->material_path) - 1] = '\0';
        },
        [](const void* d, std::string& out) {
            const auto* s = static_cast<const StaticMeshData*>(d);
            if (s->material_path[0] != '\0') {
                char buf[300];
                std::snprintf(buf, sizeof(buf), "material = %s\n", s->material_path);
                out += buf;
            }
        }
    },
};

static void register_static_mesh() {
    NodeTypeDef def{};
    std::strncpy(def.name, "StaticMesh", 64);
    def.editor_color[0] = 0.2f; def.editor_color[1] = 0.8f; def.editor_color[2] = 0.9f;
    def.comps[0] = {
        detail::component_type_id<StaticMeshData>(), sizeof(StaticMeshData),
        [](void* p) { new(p) StaticMeshData{}; },
        kStaticMeshProps, 2
    };
    def.comp_count = 1;
    ComponentRegistry::reg(def);
}

// ============================================================================
// Built-in type: DirectionalLight
// ============================================================================

static ComponentProperty kDirLightProps[] = {
    {
        "light_color",
        [](void* d, const char* v) {
            auto* l = static_cast<DirectionalLightData*>(d);
            float f[3] = {1,1,1};
            parse_f3(v, f);
            l->color = {f[0], f[1], f[2], 0.0f};
        },
        [](const void* d, std::string& out) {
            const auto* l = static_cast<const DirectionalLightData*>(d);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "light_color = %.6f %.6f %.6f\n",
                          static_cast<double>(l->color.x),
                          static_cast<double>(l->color.y),
                          static_cast<double>(l->color.z));
            out += buf;
        }
    },
    {
        "light_intensity",
        [](void* d, const char* v) {
            static_cast<DirectionalLightData*>(d)->intensity = pf(v);
        },
        [](const void* d, std::string& out) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "light_intensity = %.6f\n",
                          static_cast<double>(static_cast<const DirectionalLightData*>(d)->intensity));
            out += buf;
        }
    },
};

static void register_directional_light() {
    NodeTypeDef def{};
    std::strncpy(def.name, "DirectionalLight", 64);
    def.editor_color[0] = 0.9f; def.editor_color[1] = 0.7f; def.editor_color[2] = 0.2f;
    def.comps[0] = {
        detail::component_type_id<DirectionalLightData>(), sizeof(DirectionalLightData),
        [](void* p) { new(p) DirectionalLightData{}; },
        kDirLightProps, 2
    };
    def.comp_count = 1;
    ComponentRegistry::reg(def);
}

// ============================================================================
// Built-in type: PointLight
// ============================================================================

static ComponentProperty kPointLightProps[] = {
    {
        "light_color",
        [](void* d, const char* v) {
            auto* l = static_cast<PointLightData*>(d);
            float f[3] = {1,1,1};
            parse_f3(v, f);
            l->color = {f[0], f[1], f[2], 0.0f};
        },
        [](const void* d, std::string& out) {
            const auto* l = static_cast<const PointLightData*>(d);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "light_color = %.6f %.6f %.6f\n",
                          static_cast<double>(l->color.x),
                          static_cast<double>(l->color.y),
                          static_cast<double>(l->color.z));
            out += buf;
        }
    },
    {
        "light_intensity",
        [](void* d, const char* v) { static_cast<PointLightData*>(d)->intensity = pf(v); },
        [](const void* d, std::string& out) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "light_intensity = %.6f\n",
                          static_cast<double>(static_cast<const PointLightData*>(d)->intensity));
            out += buf;
        }
    },
    {
        "light_radius",
        [](void* d, const char* v) { static_cast<PointLightData*>(d)->radius = pf(v); },
        [](const void* d, std::string& out) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "light_radius = %.6f\n",
                          static_cast<double>(static_cast<const PointLightData*>(d)->radius));
            out += buf;
        }
    },
};

static void register_point_light() {
    NodeTypeDef def{};
    std::strncpy(def.name, "PointLight", 64);
    def.editor_color[0] = 0.9f; def.editor_color[1] = 0.7f; def.editor_color[2] = 0.2f;
    def.comps[0] = {
        detail::component_type_id<PointLightData>(), sizeof(PointLightData),
        [](void* p) { new(p) PointLightData{}; },
        kPointLightProps, 3
    };
    def.comp_count = 1;
    ComponentRegistry::reg(def);
}

// ============================================================================
// Built-in type: SpotLight
// ============================================================================

static ComponentProperty kSpotLightProps[] = {
    {
        "light_color",
        [](void* d, const char* v) {
            auto* l = static_cast<SpotLightData*>(d);
            float f[3] = {1,1,1};
            parse_f3(v, f);
            l->color = {f[0], f[1], f[2], 0.0f};
        },
        [](const void* d, std::string& out) {
            const auto* l = static_cast<const SpotLightData*>(d);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "light_color = %.6f %.6f %.6f\n",
                          static_cast<double>(l->color.x),
                          static_cast<double>(l->color.y),
                          static_cast<double>(l->color.z));
            out += buf;
        }
    },
    {
        "light_intensity",
        [](void* d, const char* v) { static_cast<SpotLightData*>(d)->intensity = pf(v); },
        [](const void* d, std::string& out) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "light_intensity = %.6f\n",
                          static_cast<double>(static_cast<const SpotLightData*>(d)->intensity));
            out += buf;
        }
    },
    {
        "light_radius",
        [](void* d, const char* v) { static_cast<SpotLightData*>(d)->radius = pf(v); },
        [](const void* d, std::string& out) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "light_radius = %.6f\n",
                          static_cast<double>(static_cast<const SpotLightData*>(d)->radius));
            out += buf;
        }
    },
    {
        "inner_angle",
        [](void* d, const char* v) { static_cast<SpotLightData*>(d)->inner_angle = pf(v); },
        [](const void* d, std::string& out) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "inner_angle = %.6f\n",
                          static_cast<double>(static_cast<const SpotLightData*>(d)->inner_angle));
            out += buf;
        }
    },
    {
        "outer_angle",
        [](void* d, const char* v) { static_cast<SpotLightData*>(d)->outer_angle = pf(v); },
        [](const void* d, std::string& out) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "outer_angle = %.6f\n",
                          static_cast<double>(static_cast<const SpotLightData*>(d)->outer_angle));
            out += buf;
        }
    },
    {
        "light_direction",
        [](void* d, const char* v) {
            auto* l = static_cast<SpotLightData*>(d);
            float f[3] = {0,-1,0};
            parse_f3(v, f);
            l->direction = {f[0], f[1], f[2], 0.0f};
        },
        [](const void* d, std::string& out) {
            const auto* l = static_cast<const SpotLightData*>(d);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "light_direction = %.6f %.6f %.6f\n",
                          static_cast<double>(l->direction.x),
                          static_cast<double>(l->direction.y),
                          static_cast<double>(l->direction.z));
            out += buf;
        }
    },
};

static void register_spot_light() {
    NodeTypeDef def{};
    std::strncpy(def.name, "SpotLight", 64);
    def.editor_color[0] = 0.9f; def.editor_color[1] = 0.7f; def.editor_color[2] = 0.3f;
    def.comps[0] = {
        detail::component_type_id<SpotLightData>(), sizeof(SpotLightData),
        [](void* p) { new(p) SpotLightData{}; },
        kSpotLightProps, 6
    };
    def.comp_count = 1;
    ComponentRegistry::reg(def);
}

// ============================================================================
// Built-in type: Camera
// ============================================================================

static ComponentProperty kCameraProps[] = {
    {
        "fov",
        [](void* d, const char* v) { static_cast<CameraData*>(d)->fov_y      = pf(v); },
        [](const void* d, std::string& out) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "fov = %.6f\n",
                          static_cast<double>(static_cast<const CameraData*>(d)->fov_y));
            out += buf;
        }
    },
    {
        "near",
        [](void* d, const char* v) { static_cast<CameraData*>(d)->near_plane = pf(v); },
        [](const void* d, std::string& out) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "near = %.6f\n",
                          static_cast<double>(static_cast<const CameraData*>(d)->near_plane));
            out += buf;
        }
    },
    {
        "far",
        [](void* d, const char* v) { static_cast<CameraData*>(d)->far_plane  = pf(v); },
        [](const void* d, std::string& out) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "far = %.6f\n",
                          static_cast<double>(static_cast<const CameraData*>(d)->far_plane));
            out += buf;
        }
    },
};

static void register_camera() {
    NodeTypeDef def{};
    std::strncpy(def.name, "Camera", 64);
    def.editor_color[0] = 0.9f; def.editor_color[1] = 0.9f; def.editor_color[2] = 0.2f;
    def.comps[0] = {
        detail::component_type_id<CameraData>(), sizeof(CameraData),
        [](void* p) { new(p) CameraData{}; },
        kCameraProps, 3
    };
    def.comp_count = 1;
    ComponentRegistry::reg(def);
}

// ============================================================================
// Built-in type: AudioSource / AudioListener (reserved, no component data yet)
// ============================================================================

static void register_audio_types() {
    {
        NodeTypeDef def{};
        std::strncpy(def.name, "AudioSource", 64);
        def.editor_color[0] = 0.6f; def.editor_color[1] = 0.4f; def.editor_color[2] = 0.9f;
        def.comp_count = 0;
        ComponentRegistry::reg(def);
    }
    {
        NodeTypeDef def{};
        std::strncpy(def.name, "AudioListener", 64);
        def.editor_color[0] = 0.6f; def.editor_color[1] = 0.4f; def.editor_color[2] = 0.9f;
        def.comp_count = 0;
        ComponentRegistry::reg(def);
    }
}

// ============================================================================
// Built-in type: SkyAtmosphere
// ============================================================================

static ComponentProperty kSkyProps[] = {
    {
        "sky_zenith",
        [](void* d, const char* v) { parse_f3(v, static_cast<SkyAtmosphereData*>(d)->sky_zenith); },
        [](const void* d, std::string& out) {
            const auto* s = static_cast<const SkyAtmosphereData*>(d);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "sky_zenith = %.6f %.6f %.6f\n",
                          static_cast<double>(s->sky_zenith[0]),
                          static_cast<double>(s->sky_zenith[1]),
                          static_cast<double>(s->sky_zenith[2]));
            out += buf;
        }
    },
    {
        "sky_horizon",
        [](void* d, const char* v) { parse_f3(v, static_cast<SkyAtmosphereData*>(d)->sky_horizon); },
        [](const void* d, std::string& out) {
            const auto* s = static_cast<const SkyAtmosphereData*>(d);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "sky_horizon = %.6f %.6f %.6f\n",
                          static_cast<double>(s->sky_horizon[0]),
                          static_cast<double>(s->sky_horizon[1]),
                          static_cast<double>(s->sky_horizon[2]));
            out += buf;
        }
    },
    {
        "sky_haze",
        [](void* d, const char* v) { parse_f3(v, static_cast<SkyAtmosphereData*>(d)->sky_haze); },
        [](const void* d, std::string& out) {
            const auto* s = static_cast<const SkyAtmosphereData*>(d);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "sky_haze = %.6f %.6f %.6f\n",
                          static_cast<double>(s->sky_haze[0]),
                          static_cast<double>(s->sky_haze[1]),
                          static_cast<double>(s->sky_haze[2]));
            out += buf;
        }
    },
    {
        "sky_ground",
        [](void* d, const char* v) { parse_f3(v, static_cast<SkyAtmosphereData*>(d)->sky_ground); },
        [](const void* d, std::string& out) {
            const auto* s = static_cast<const SkyAtmosphereData*>(d);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "sky_ground = %.6f %.6f %.6f\n",
                          static_cast<double>(s->sky_ground[0]),
                          static_cast<double>(s->sky_ground[1]),
                          static_cast<double>(s->sky_ground[2]));
            out += buf;
        }
    },
    {
        "fog_density",
        [](void* d, const char* v) { static_cast<SkyAtmosphereData*>(d)->fog_density = pf(v); },
        [](const void* d, std::string& out) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "fog_density = %.6f\n",
                          static_cast<double>(static_cast<const SkyAtmosphereData*>(d)->fog_density));
            out += buf;
        }
    },
};

static void register_sky_atmosphere() {
    NodeTypeDef def{};
    std::strncpy(def.name, "SkyAtmosphere", 64);
    def.editor_color[0] = 0.4f; def.editor_color[1] = 0.7f; def.editor_color[2] = 1.0f;
    def.comps[0] = {
        detail::component_type_id<SkyAtmosphereData>(), sizeof(SkyAtmosphereData),
        [](void* p) { new(p) SkyAtmosphereData{}; },
        kSkyProps, 5
    };
    def.comp_count = 1;
    ComponentRegistry::reg(def);
}

// ============================================================================
// register_builtins — called once at engine startup
// ============================================================================
//
// Registration ORDER determines the type_index assigned to each type.
// The GizmoRegistry in editor_component.cpp uses these indices.
// Stable order (do NOT reorder without updating gizmo registrations):
//   0: Node3D          5: Camera
//   1: StaticMesh      6: AudioSource
//   2: DirectionalLight 7: AudioListener
//   3: PointLight      8: SkyAtmosphere
//   4: SpotLight

void ComponentRegistry::register_builtins() {
    if (s_count > 0) return;  // Already initialized.

    register_node3d();           // index 0
    register_static_mesh();      // index 1
    register_directional_light();// index 2
    register_point_light();      // index 3
    register_spot_light();       // index 4
    register_camera();           // index 5
    register_audio_types();      // index 6, 7
    register_sky_atmosphere();   // index 8

    // Ensure ScriptData has a type ID allocated (cross-cutting — any node can have a script).
    (void)detail::component_type_id<ScriptData>();
}

void ComponentRegistry::init_script_pool(ComponentStore& store) {
    component_store_register<ScriptData>(&store);
}

} // namespace uz
