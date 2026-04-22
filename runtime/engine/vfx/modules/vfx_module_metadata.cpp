/// VFX module metadata catalog — static table implementation.
///
/// Phase 11.3.0. Adding a new built-in module requires:
///   1. `ModuleRegistry::Register<T>("name")` in register_builtin_modules.cpp
///   2. A row in `kCatalog` below
/// Keeping these two lists in lockstep is the author's responsibility; the
/// unit test `VfxModuleMetadata.EveryBuiltinIsCategorized` enforces it.

#include "vfx_module_metadata.h"

#include "engine/vfx/editor/vfx_editor_icons.h"

#include <cstring>

namespace uz {

namespace {

// ─── Stage bitmask shortcuts ────────────────────────────────────────────────

constexpr uint32_t kParticleOnly =
    VfxStageBits(VfxStage::ParticleSpawn, VfxStage::ParticleUpdate);

constexpr uint32_t kParticleUpdateOnly =
    VfxStageBits(VfxStage::ParticleUpdate);

constexpr uint32_t kParticleSpawnOnly =
    VfxStageBits(VfxStage::ParticleSpawn);

constexpr uint32_t kEmitterLifecycle =
    VfxStageBits(VfxStage::EmitterSpawn, VfxStage::EmitterUpdate);

constexpr uint32_t kRenderOnly =
    VfxStageBits(VfxStage::Render);

constexpr uint32_t kAnyStage =
    VfxStageBits(VfxStage::SystemSpawn, VfxStage::SystemUpdate,
                 VfxStage::EmitterSpawn, VfxStage::EmitterUpdate,
                 VfxStage::ParticleSpawn, VfxStage::ParticleUpdate,
                 VfxStage::Render);

// ─── The catalog table ───────────────────────────────────────────────────────
// Every row mirrors a `registry.Register<T>("<name>")` call in
// register_builtin_modules.cpp. If you add a row here without an
// accompanying Register call, Create() will return nullptr; if you add a
// Register call without a row here, the picker tile grid will hide it.

// Phase 12.3.0 — every entry now carries an `iconGlyph` drawn from
// `vfx_editor_icons.h` so the picker + stack panel can render a stable
// visual symbol per module. When in doubt a category fallback is used
// (`VFX_ICON_CAT_*`). Added as the fifth initialiser in each row.
constexpr VfxModuleCatalogEntry kCatalog[] = {
    // ── Simulation ─────────────────────────────────────────────────────────
    {"spawn",          "Simulation",
     "Emission-rate and burst-schedule driver.",          kEmitterLifecycle,
     VFX_ICON_CAT_SPAWN},
    {"update",         "Simulation",
     "Per-particle gravity, damping, ageing.",            kParticleUpdateOnly,
     VFX_ICON_CAT_LIFETIME},
    {"decay",          "Simulation",
     "Age-based particle recycling.",                     kParticleUpdateOnly,
     VFX_ICON_CAT_LIFETIME},
    {"turbulence",     "Simulation",
     "Noise-based velocity perturbation.",                kParticleUpdateOnly,
     VFX_ICON_CAT_VELOCITY},
    {"constraint",     "Simulation",
     "Position/velocity boundary constraints.",           kParticleUpdateOnly,
     VFX_ICON_CAT_LOCATION},

    // ── Forces ─────────────────────────────────────────────────────────────
    {"attractor",      "Forces",
     "Point-attractor / repulsor with falloff.",          kParticleUpdateOnly,
     VFX_ICON_CAT_FORCE},
    {"vector_field",   "Forces",
     "Samples a 3D vector field per particle.",           kParticleUpdateOnly,
     VFX_ICON_CAT_FORCE},
    {"orbit",          "Forces",
     "Orbital motion around an axis.",                    kParticleUpdateOnly,
     VFX_ICON_CAT_ROTATION},
    {"pin",            "Forces",
     "Pins particles to a moving target point.",          kParticleUpdateOnly,
     VFX_ICON_CAT_LOCATION},

    // ── Collision ──────────────────────────────────────────────────────────
    {"collision",             "Collision",
     "Depth-buffer collision detection + response.",      kParticleUpdateOnly,
     VFX_ICON_CAT_LOCATION},
    {"distance_constraint",   "Collision",
     "Distance constraint between particles.",            kParticleUpdateOnly,
     VFX_ICON_CAT_SIZE},

    // ── Render ─────────────────────────────────────────────────────────────
    {"sprite_render",  "Render",
     "Billboard sprite with size/colour curves.",         kRenderOnly,
     VFX_ICON_CAT_RENDER},
    {"light_render",   "Render",
     "Transient point-light particle emitters.",          kRenderOnly,
     VFX_ICON_CAT_COLOR},
    {"mesh_render",    "Render",
     "Instanced static-mesh particle renderer.",          kRenderOnly,
     VFX_ICON_CAT_SIZE},

    // ── Data interfaces ────────────────────────────────────────────────────
    {"skeletal_spawn",         "DI",
     "Spawn particles along skeletal mesh surface.",      kParticleSpawnOnly,
     VFX_ICON_CAT_SPAWN},
    {"audio_spectrum",         "DI",
     "Drive particle params from audio FFT bands.",       kParticleUpdateOnly,
     VFX_ICON_CAT_EVENT},
    {"neighbor_interaction",   "DI",
     "Per-particle neighbour query (flocking).",          kParticleUpdateOnly,
     VFX_ICON_CAT_EVENT},
    {"render_target_di",       "DI",
     "Read a render-target texture per particle.",        kParticleUpdateOnly,
     VFX_ICON_CAT_EVENT},
    {"async_gpu_trace",        "DI",
     "Async world trace feeding collision queries.",      kParticleUpdateOnly,
     VFX_ICON_CAT_EVENT},

    // ── Dynamic material params ────────────────────────────────────────────
    {"dynamic_params", "Dynamic",
     "Writes particle-local dynamic material params.",    kParticleUpdateOnly,
     VFX_ICON_CAT_COLOR},

    // ── Events ─────────────────────────────────────────────────────────────
    {"sub_emitter",    "Events",
     "Spawns a child effect on particle events.",         kParticleUpdateOnly,
     VFX_ICON_CAT_EVENT},
};

constexpr size_t kCatalogCount = sizeof(kCatalog) / sizeof(kCatalog[0]);

bool CStrEq(const char* a, const char* b) noexcept {
    if (a == nullptr || b == nullptr) return false;
    return std::strcmp(a, b) == 0;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Lookups
// ─────────────────────────────────────────────────────────────────────────────

const VfxModuleCatalogEntry* VfxModuleMetadata::Find(const char* name) noexcept {
    if (name == nullptr) return nullptr;
    for (size_t i = 0; i < kCatalogCount; ++i) {
        if (CStrEq(kCatalog[i].name, name)) return &kCatalog[i];
    }
    return nullptr;
}

std::string VfxModuleMetadata::GetCategory(const std::string& name) {
    const auto* e = Find(name);
    return (e != nullptr) ? std::string(e->category) : std::string("Custom");
}

std::string VfxModuleMetadata::GetDescription(const std::string& name) {
    const auto* e = Find(name);
    return (e != nullptr) ? std::string(e->description) : std::string();
}

const char* VfxModuleMetadata::GetIconGlyph(const std::string& name) noexcept {
    const auto* e = Find(name);
    if (e != nullptr && e->iconGlyph != nullptr && e->iconGlyph[0] != '\0') {
        return e->iconGlyph;
    }
    return VFX_ICON_CAT_GENERIC;
}

size_t VfxModuleMetadata::CountByCategoryAndStage(
    const std::string& category, VfxStage stage) noexcept {
    const bool allCats = (category == "All");
    size_t n = 0;
    for (size_t i = 0; i < kCatalogCount; ++i) {
        if (!allCats && !CStrEq(kCatalog[i].category, category.c_str())) {
            continue;
        }
        if (!kCatalog[i].IsValidInStage(stage)) continue;
        ++n;
    }
    return n;
}

bool VfxModuleMetadata::IsValidInStage(const std::string& name,
                                        VfxStage stage) noexcept {
    const auto* e = Find(name);
    // Unknown module — permissive (custom modules have no catalog metadata).
    if (e == nullptr) return true;
    return e->IsValidInStage(stage);
}

const VfxModuleCatalogEntry* VfxModuleMetadata::Table(size_t& outCount) noexcept {
    outCount = kCatalogCount;
    return kCatalog;
}

std::vector<std::string> VfxModuleMetadata::Categories() {
    std::vector<std::string> out;
    out.reserve(8);
    out.emplace_back("All");
    for (size_t i = 0; i < kCatalogCount; ++i) {
        const char* c = kCatalog[i].category;
        bool already = false;
        for (const auto& x : out) {
            if (x == c) { already = true; break; }
        }
        if (!already) out.emplace_back(c);
    }
    return out;
}

std::vector<std::string> VfxModuleMetadata::FilterByCategoryAndStage(
    const std::string& category, VfxStage stage) {
    std::vector<std::string> out;
    out.reserve(kCatalogCount);
    const bool allCats = (category == "All");
    for (size_t i = 0; i < kCatalogCount; ++i) {
        const auto& e = kCatalog[i];
        if (!allCats && category != e.category) continue;
        if (!e.IsValidInStage(stage)) continue;
        out.emplace_back(e.name);
    }
    return out;
}

} // namespace uz
