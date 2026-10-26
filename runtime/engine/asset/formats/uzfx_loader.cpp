/// UzEngine UzfxLoader — Implementation.
///
/// Parses .uzvfx JSON into UzfxDesc (multi-emitter system), then
/// instantiates ParticleEmitter instances with configured module stacks.

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
    // Element types are checked too: get<float>() on a non-number throws
    // nlohmann::type_error, which would escape the loader on a crafted asset.
    if (it != j.end() && it->is_array() && it->size() >= 3 &&
        (*it)[0].is_number() && (*it)[1].is_number() && (*it)[2].is_number()) {
        return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
    }
    return def;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse modules section from a JSON object
// ─────────────────────────────────────────────────────────────────────────────

static void ParseModules(const nlohmann::json& mods, UzfxEmitterDesc& emitter) {
    // Spawn module
    if (mods.contains("spawn")) {
        const auto& s = mods["spawn"];
        emitter.spawnModule.type       = JString(s, "type", "spawn");
        emitter.spawnModule.paramsJson = s.dump();
    }

    // Update modules (array)
    if (mods.contains("update") && mods["update"].is_array()) {
        for (const auto& u : mods["update"]) {
            UzfxModuleEntry entry;
            entry.type       = JString(u, "type", "update");
            entry.paramsJson = u.dump();
            emitter.updateModules.push_back(std::move(entry));
        }
    }

    // Render module
    if (mods.contains("render")) {
        const auto& r = mods["render"];
        emitter.renderModule.type       = JString(r, "type", "sprite");
        emitter.renderModule.paramsJson = r.dump();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse a single emitter JSON object
// ─────────────────────────────────────────────────────────────────────────────

static UzfxEmitterDesc ParseEmitter(const nlohmann::json& j) {
    UzfxEmitterDesc em;
    em.name          = JString(j, "name", "unnamed");
    em.maxParticles  = JUint(j, "max_particles", em.maxParticles);
    em.emissionRate  = JFloat(j, "emission_rate", em.emissionRate);
    em.lifetime      = JFloat(j, "lifetime", em.lifetime);
    em.looping       = JBool(j, "looping", em.looping);

    if (j.contains("modules")) {
        ParseModules(j["modules"], em);
    }

    return em;
}

// ─────────────────────────────────────────────────────────────────────────────
// Load — parse .uzvfx JSON (all 3 formats)
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

    // ── v2: multi-emitter system ────────────────────────────────────────
    if (root.contains("emitters") && root["emitters"].is_array()) {
        for (const auto& emJ : root["emitters"]) {
            desc.emitters.push_back(ParseEmitter(emJ));
        }
    }
    // ── v1: single emitter with "emitter" + "modules" sections ──────────
    else if (root.contains("emitter")) {
        UzfxEmitterDesc em;
        const auto& emJ = root["emitter"];
        em.name          = desc.name;
        em.maxParticles  = JUint(emJ, "max_particles", em.maxParticles);
        em.emissionRate  = JFloat(emJ, "emission_rate", em.emissionRate);
        em.lifetime      = JFloat(emJ, "lifetime", em.lifetime);
        em.looping       = JBool(emJ, "looping", em.looping);

        if (root.contains("modules")) {
            ParseModules(root["modules"], em);
        }

        desc.emitters.push_back(std::move(em));
    }
    // ── v0: flat format (legacy preset editor) ──────────────────────────
    else {
        UzfxEmitterDesc em;
        em.name         = desc.name;
        em.emissionRate = JFloat(root, "spawnRate", em.emissionRate);
        em.lifetime     = JFloat(root, "maxLifetime", em.lifetime);
        em.maxParticles = JUint(root, "maxParticles", em.maxParticles);
        desc.emitters.push_back(std::move(em));
    }

    UZ_LOG(VFXAsset, Info, "UzfxLoader: loaded '{}' ({} emitter{})",
           desc.name, desc.emitters.size(),
           desc.emitters.size() == 1 ? "" : "s");

    for (size_t i = 0; i < desc.emitters.size(); ++i) {
        const auto& em = desc.emitters[i];
        UZ_LOG(VFXAsset, Info, "  [{}] '{}': particles={}, rate={}, spawn={}, update={}, render={}",
               i, em.name, em.maxParticles, em.emissionRate,
               em.spawnModule.type.empty() ? "none" : em.spawnModule.type,
               em.updateModules.size(),
               em.renderModule.type.empty() ? "none" : em.renderModule.type);
    }

    return desc;
}

// ─────────────────────────────────────────────────────────────────────────────
// InstantiateEmitter — create live emitter from one emitter descriptor
// ─────────────────────────────────────────────────────────────────────────────

bool UzfxLoader::InstantiateEmitter(
    const UzfxEmitterDesc& desc,
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
            // "restitution" is canonical; "bounce_factor" accepted for backward compat.
            cfg.restitution          = JFloat(j, "restitution",
                                              JFloat(j, "bounce_factor", cfg.restitution));
            cfg.depthBufferCollision = JBool(j, "depth_buffer_collision", cfg.depthBufferCollision);
            cfg.collisionOffset     = JFloat(j, "collision_offset", cfg.collisionOffset);
            cfg.emitCollisionEvent  = JBool(j, "emit_collision_event", cfg.emitCollisionEvent);
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
                mod->texturePath  = JString(j, "texture", "");
                mod->flipbookRows = JUint(j, "flipbook_rows", 1);
                mod->flipbookCols = JUint(j, "flipbook_cols", 1);
                mod->flipbookTotalFrames = mod->flipbookRows * mod->flipbookCols;
                mod->flipbookFPS  = JFloat(j, "flipbook_fps", 30.0f);
                emitter.moduleStack.SetRenderModule(std::move(mod));
            } else if (desc.renderModule.type == "mesh") {
                MeshRenderModuleConfig cfg;
                cfg.meshPath     = JString(j, "mesh_path", "");
                cfg.materialPath = JString(j, "material_path", "");
                cfg.meshScale    = JVec3(j, "scale", {1.0f, 1.0f, 1.0f});
                cfg.castShadow   = JBool(j, "cast_shadow", false);

                // Rotation mode: per-particle orientation strategy. Empty /
                // missing falls back to the struct default (VelocityAligned)
                // — the production loader has historically defaulted to that
                // behaviour even when the asset omitted the field.
                if (const std::string rotStr = JString(j, "rotation_mode", "");
                    !rotStr.empty()) {
                    cfg.rotationMode = MeshRotationModeFromString(rotStr);
                }

                // Sort mode: depth-ordering strategy for transparent meshes.
                // Empty / missing keeps the default `None` (no sort), which
                // preserves byte-identical behaviour for legacy `.uzvfx`
                // assets that have never seen this field.
                if (const std::string sortStr = JString(j, "sort_mode", "");
                    !sortStr.empty()) {
                    cfg.sortMode = MeshSortModeFromString(sortStr);
                }

                // Custom-attribute sort key — only meaningful when
                // sort_mode == CustomAttribute. Validation of the
                // (sortMode, customSortAttribute) pair lives in
                // `MeshRenderModule::ApplyJsonConfig`/`ParseConfig`, not
                // here, so the loader can keep its branches small.
                cfg.customSortAttribute = JString(j, "custom_sort_attribute", "");
                if (cfg.sortMode == MeshSortMode::CustomAttribute &&
                    cfg.customSortAttribute.empty()) {
                    UZ_LOG(VFXAsset, Warning,
                           "UzfxLoader: mesh.sort_mode='CustomAttribute' but "
                           "custom_sort_attribute is empty — falling back to None");
                    cfg.sortMode = MeshSortMode::None;
                }

                // Per-emitter mesh instance buffer ceiling. Out-of-range
                // values are silently clamped by `ParseConfig`; here we
                // only forward the JSON value to the constructor so the
                // production loader matches the serializer / `.uzfx`
                // editor path.
                if (const auto maxInstancesU = JUint(j, "max_instances", cfg.maxInstances);
                    maxInstancesU > 0) {
                    cfg.maxInstances = maxInstancesU;
                }

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

    UZ_LOG(VFXAsset, Info, "UzfxLoader: instantiated '{}' (allocated={}, spawn={}, update={}, render={})",
           desc.name, emitter.allocatedFromBudget,
           emitter.moduleStack.spawnModules.size(),
           emitter.moduleStack.updateModules.size(),
           emitter.moduleStack.renderModule ? "yes" : "none");

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Legacy Instantiate — uses first emitter from system desc
// ─────────────────────────────────────────────────────────────────────────────

bool UzfxLoader::Instantiate(
    const UzfxDesc& desc,
    ParticleEmitter& outEmitter,
    IRHIDevice* device,
    ParticleBudget& budget)
{
    if (desc.emitters.empty()) {
        UZ_LOG(VFXAsset, Error, "UzfxLoader::Instantiate: no emitters in system '{}'", desc.name);
        return false;
    }
    return InstantiateEmitter(desc.emitters[0], outEmitter, device, budget);
}

} // namespace uz
