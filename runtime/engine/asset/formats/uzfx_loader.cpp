/// UzEngine UzfxLoader — Implementation.
///
/// Parses .uzfx JSON into UzfxDesc, then instantiates ParticleEmitter
/// with the described module stack.

#include "uzfx_loader.h"

#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/modules/emitter_module_stack.h"
#include "engine/vfx/modules/spawn_module.h"
#include "engine/vfx/modules/update_module.h"
#include "engine/vfx/modules/turbulence_module.h"
#include "engine/vfx/modules/decay_module.h"
#include "engine/vfx/modules/attractor_module.h"
#include "engine/vfx/modules/orbit_module.h"
#include "engine/vfx/modules/collision_module.h"
#include "engine/vfx/modules/vector_field_module.h"
#include "engine/vfx/modules/sprite_render_module.h"
#include "engine/vfx/modules/mesh_render_module.h"
#include "engine/vfx/modules/light_render_module.h"
#include "engine/vfx/modules/sub_emitter_module.h"
#include "engine/vfx/core/particle_budget.h"
#include "engine/rhi/rhi.h"
#include "log.h"

#include <json.hpp>

UZ_DEFINE_LOG_CATEGORY(VFXAsset, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// JSON helpers
// ─────────────────────────────────────────────────────────────────────────────

static float JFloat(const nlohmann::json& j, const char* key, float def) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<float>() : def;
}

static uint32_t JUint(const nlohmann::json& j, const char* key, uint32_t def) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number_unsigned()) ? it->get<uint32_t>() : def;
}

static bool JBool(const nlohmann::json& j, const char* key, bool def) {
    auto it = j.find(key);
    return (it != j.end() && it->is_boolean()) ? it->get<bool>() : def;
}

static std::string JString(const nlohmann::json& j, const char* key, const std::string& def = {}) {
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : def;
}

static Vec3 JVec3(const nlohmann::json& j, const char* key, const Vec3& def = {}) {
    auto it = j.find(key);
    if (it != j.end() && it->is_array() && it->size() >= 3) {
        return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
    }
    return def;
}

// ─────────────────────────────────────────────────────────────────────────────
// Load — parse .uzfx JSON
// ─────────────────────────────────────────────────────────────────────────────

UzfxDesc UzfxLoader::Load(const std::string& jsonData) {
    UzfxDesc desc;

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonData);
    } catch (const nlohmann::json::parse_error& e) {
        UZ_LOG(VFXAsset, Error, "UzfxLoader: JSON parse error: {}", e.what());
        return desc;
    }

    desc.name    = JString(root, "name", "unnamed");
    desc.version = JUint(root, "version", 1);

    // Emitter base params
    if (root.contains("emitter")) {
        const auto& em = root["emitter"];
        desc.maxParticles = JUint(em, "max_particles", desc.maxParticles);
        desc.emissionRate = JFloat(em, "emission_rate", desc.emissionRate);
        desc.lifetime     = JFloat(em, "lifetime", desc.lifetime);
        desc.looping      = JBool(em, "looping", desc.looping);
    }

    // Modules
    if (root.contains("modules")) {
        const auto& mods = root["modules"];

        // Spawn module
        if (mods.contains("spawn")) {
            const auto& s = mods["spawn"];
            desc.spawnModule.type       = JString(s, "type", "spawn");
            desc.spawnModule.paramsJson = s.dump();
        }

        // Update modules (array)
        if (mods.contains("update") && mods["update"].is_array()) {
            for (const auto& u : mods["update"]) {
                UzfxDesc::ModuleEntry entry;
                entry.type       = JString(u, "type", "update");
                entry.paramsJson = u.dump();
                desc.updateModules.push_back(std::move(entry));
            }
        }

        // Render module
        if (mods.contains("render")) {
            const auto& r = mods["render"];
            desc.renderModule.type       = JString(r, "type", "sprite");
            desc.renderModule.paramsJson = r.dump();
        }
    }

    UZ_LOG(VFXAsset, Info, "UzfxLoader: loaded '{}' (maxParticles={}, rate={}, modules: spawn={}, update={}, render={})",
           desc.name, desc.maxParticles, desc.emissionRate,
           desc.spawnModule.type.empty() ? "none" : desc.spawnModule.type,
           desc.updateModules.size(),
           desc.renderModule.type.empty() ? "none" : desc.renderModule.type);

    return desc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Instantiate — create live emitter from descriptor
// ─────────────────────────────────────────────────────────────────────────────

bool UzfxLoader::Instantiate(
    const UzfxDesc& desc,
    ParticleEmitter& emitter,
    IRHIDevice* device,
    ParticleBudget& budget)
{
    // Base params
    emitter.maxParticles = desc.maxParticles;
    emitter.emissionRate = desc.emissionRate;
    emitter.lifetime     = desc.lifetime;
    emitter.isEmitting   = true;

    // Spawn module
    if (!desc.spawnModule.type.empty()) {
        auto j = nlohmann::json::parse(desc.spawnModule.paramsJson, nullptr, false);
        if (!j.is_discarded()) {
            auto mod = std::make_unique<SpawnModule>();
            mod->rate        = JFloat(j, "rate", desc.emissionRate);
            mod->spreadAngle = JFloat(j, "spread_angle", 0.0f);
            mod->velocity    = JVec3(j, "velocity", {0.0f, 0.0f, 200.0f});
            mod->lifespanMin = JFloat(j, "lifespan_min", desc.lifetime * 0.5f);
            mod->lifespanMax = JFloat(j, "lifespan_max", desc.lifetime);
            emitter.moduleStack.AddSpawnModule(std::move(mod));
        }
    }

    // Update modules
    for (const auto& entry : desc.updateModules) {
        auto j = nlohmann::json::parse(entry.paramsJson, nullptr, false);
        if (j.is_discarded()) continue;

        if (entry.type == "update") {
            auto mod = std::make_unique<UpdateModule>();
            mod->gravity = JVec3(j, "gravity", {0.0f, 0.0f, -981.0f});
            mod->drag    = JFloat(j, "drag", 0.98f);
            emitter.moduleStack.AddUpdateModule(std::move(mod));
        } else if (entry.type == "turbulence") {
            auto mod = std::make_unique<TurbulenceModule>();
            mod->strength  = JFloat(j, "strength", 1.0f);
            mod->frequency = JFloat(j, "frequency", 1.0f);
            mod->octaves   = JUint(j, "octaves", 2);
            emitter.moduleStack.AddUpdateModule(std::move(mod));
        } else if (entry.type == "decay") {
            emitter.moduleStack.AddUpdateModule(std::make_unique<DecayModule>());
        } else if (entry.type == "attractor") {
            auto mod = std::make_unique<AttractorModule>();
            mod->attractorPosition = JVec3(j, "position", {});
            mod->strength          = JFloat(j, "strength", 10.0f);
            mod->radius            = JFloat(j, "radius", 5.0f);
            mod->killOnArrival     = JBool(j, "kill_on_arrival", false);
            emitter.moduleStack.AddUpdateModule(std::move(mod));
        } else if (entry.type == "orbit") {
            auto mod = std::make_unique<OrbitModule>();
            mod->center      = JVec3(j, "center", {});
            mod->speed       = JFloat(j, "speed", 1.0f);
            mod->orbitRadius = JFloat(j, "radius", 5.0f);
            mod->axis        = JVec3(j, "axis", {0.0f, 0.0f, 1.0f});
            emitter.moduleStack.AddUpdateModule(std::move(mod));
        } else if (entry.type == "collision") {
            CollisionModuleConfig cfg;
            cfg.bounceFactor        = JFloat(j, "bounce_factor", cfg.bounceFactor);
            cfg.depthBufferCollision = JBool(j, "depth_buffer_collision", cfg.depthBufferCollision);
            cfg.collisionOffset     = JFloat(j, "collision_offset", cfg.collisionOffset);
            emitter.moduleStack.AddUpdateModule(std::make_unique<CollisionModule>(cfg));
        } else if (entry.type == "vector_field") {
            auto mod = std::make_unique<VectorFieldModule>();
            mod->intensity = JFloat(j, "intensity", 1.0f);
            mod->tightness = JFloat(j, "tightness", 0.0f);
            mod->bounds    = JVec3(j, "bounds", {10.0f, 10.0f, 10.0f});
            emitter.moduleStack.AddUpdateModule(std::move(mod));
        } else {
            UZ_LOG(VFXAsset, Warning, "UzfxLoader: unknown update module type '{}'", entry.type);
        }
    }

    // Render module
    if (!desc.renderModule.type.empty()) {
        auto j = nlohmann::json::parse(desc.renderModule.paramsJson, nullptr, false);
        if (!j.is_discarded()) {
            if (desc.renderModule.type == "sprite") {
                auto mod = std::make_unique<SpriteRenderModule>();
                mod->spriteWidth  = JFloat(j, "width", 0.5f);
                mod->spriteHeight = JFloat(j, "height", 0.5f);
                emitter.moduleStack.SetRenderModule(std::move(mod));
            } else if (desc.renderModule.type == "mesh") {
                MeshRenderModuleConfig cfg;
                cfg.meshPath     = JString(j, "mesh_path", "");
                cfg.materialPath = JString(j, "material_path", "");
                cfg.meshScale    = JVec3(j, "scale", {1.0f, 1.0f, 1.0f});
                cfg.castShadow   = JBool(j, "cast_shadow", false);
                emitter.moduleStack.SetRenderModule(std::make_unique<MeshRenderModule>(cfg));
            } else if (desc.renderModule.type == "light") {
                LightRenderModuleConfig cfg;
                cfg.intensity  = JFloat(j, "intensity", 1.0f);
                cfg.radius     = JFloat(j, "radius", 5.0f);
                cfg.castShadow = JBool(j, "cast_shadow", false);
                emitter.moduleStack.SetRenderModule(std::make_unique<LightRenderModule>(cfg));
            }
        }
    }

    // Initialize GPU buffers
    if (!emitter.Initialize(device, budget)) {
        UZ_LOG(VFXAsset, Error, "UzfxLoader: failed to initialize emitter '{}'", desc.name);
        return false;
    }

    UZ_LOG(VFXAsset, Info, "UzfxLoader: instantiated '{}' (allocated={}, modules: spawn={}, update={}, render={})",
           desc.name, emitter.allocatedFromBudget,
           emitter.moduleStack.spawnModules.size(),
           emitter.moduleStack.updateModules.size(),
           emitter.moduleStack.renderModule ? "yes" : "none");

    return true;
}

} // namespace uz
