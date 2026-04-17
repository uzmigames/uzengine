/// CVar bridge for ShowFlags — `r.Show.*` registration and per-frame sync.

#include "show_flags.h"
#include "show_flags_cvar.h"
#include "engine/core/cvar.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Static CVar pointers — one per ShowFlag, registered at file scope.
// ─────────────────────────────────────────────────────────────────────────────

static CVar* s_showCVars[static_cast<size_t>(ShowFlag::Count)] = {};

static void EnsureRegistered() {
    static bool s_registered = false;
    if (s_registered) return;
    s_registered = true;

    auto& reg = CVarRegistry::Get();

    auto reg_flag = [&](ShowFlag flag, const char* desc) {
        const auto idx = static_cast<size_t>(flag);
        std::string name = std::string("r.Show.") + ShowFlagName(flag);
        s_showCVars[idx] = reg.Register(name, 1.f, desc, 0.f, 1.f);
    };

    reg_flag(ShowFlag::Grid,             "Editor grid");
    reg_flag(ShowFlag::EditorOverlays,   "Editor overlays (gizmos, outlines, helpers)");
    reg_flag(ShowFlag::Stats,            "Stats HUD");
    reg_flag(ShowFlag::StaticMeshes,     "Static meshes");
    reg_flag(ShowFlag::SkeletalMeshes,   "Skeletal meshes");
    reg_flag(ShowFlag::Particles,        "Particles / VFX");
    reg_flag(ShowFlag::Decals,           "Deferred decals");
    reg_flag(ShowFlag::Translucency,     "Translucent pass");
    reg_flag(ShowFlag::DirectionalLight, "Directional light");
    reg_flag(ShowFlag::PointLights,      "Point lights");
    reg_flag(ShowFlag::SpotLights,       "Spot lights");
    reg_flag(ShowFlag::Shadows,          "Shadow mapping (CSM + local)");
    reg_flag(ShowFlag::ContactShadows,   "Screen-space contact shadows");
    reg_flag(ShowFlag::Atmosphere,       "Sky atmosphere");
    reg_flag(ShowFlag::Clouds,           "Volumetric clouds");
    reg_flag(ShowFlag::Fog,              "Height fog");
    reg_flag(ShowFlag::Volumetrics,      "Volumetric fog / light shafts");
    reg_flag(ShowFlag::AO,              "Ambient occlusion (GTAO)");
    reg_flag(ShowFlag::SSR,             "Screen-space reflections");
    reg_flag(ShowFlag::SSGI,            "Screen-space global illumination");
    reg_flag(ShowFlag::IBL,             "Image-based lighting");
    reg_flag(ShowFlag::PostProcess,     "Post-processing master toggle");
    reg_flag(ShowFlag::AntiAliasing,    "Anti-aliasing (TAA/FXAA)");
    reg_flag(ShowFlag::Bloom,           "Bloom");
    reg_flag(ShowFlag::MotionBlur,      "Motion blur");
    reg_flag(ShowFlag::DOF,             "Depth of field");
    reg_flag(ShowFlag::LensFlare,       "Lens flare");
    reg_flag(ShowFlag::Tonemap,         "Tonemap operator");
}

void RegisterShowFlagCVars() {
    EnsureRegistered();
}

void SyncShowFlagsFromCVars(ShowFlags& flags) {
    EnsureRegistered();
    const auto count = static_cast<size_t>(ShowFlag::Count);
    for (size_t i = 0; i < count; ++i) {
        if (s_showCVars[i]) {
            flags.Set(static_cast<ShowFlag>(i), s_showCVars[i]->value >= 0.5f);
        }
    }
}

void SyncShowFlagsToCVars(const ShowFlags& flags) {
    EnsureRegistered();
    const auto count = static_cast<size_t>(ShowFlag::Count);
    for (size_t i = 0; i < count; ++i) {
        if (s_showCVars[i]) {
            s_showCVars[i]->value = flags.Get(static_cast<ShowFlag>(i)) ? 1.f : 0.f;
        }
    }
}

} // namespace uz
