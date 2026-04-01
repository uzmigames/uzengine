/// UzEngine Quality Preset Manager — Implementation.

#include "quality_presets.h"
#include "cvar.h"
#include "log.h"

#include <algorithm>
#include <cctype>

UZ_DECLARE_LOG_CATEGORY(Engine);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

const char* QualityLevelName(QualityLevel level) noexcept {
    switch (level) {
        case QualityLevel::Low:       return "Low";
        case QualityLevel::Medium:    return "Medium";
        case QualityLevel::High:      return "High";
        case QualityLevel::Ultra:     return "Ultra";
        case QualityLevel::Cinematic: return "Cinematic";
        default:                      return "Unknown";
    }
}

QualityLevel ParseQualityLevel(const std::string& str) noexcept {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "low"    || lower == "0") return QualityLevel::Low;
    if (lower == "medium" || lower == "1") return QualityLevel::Medium;
    if (lower == "high"   || lower == "2") return QualityLevel::High;
    if (lower == "ultra"  || lower == "3") return QualityLevel::Ultra;
    if (lower == "cinematic" || lower == "4") return QualityLevel::Cinematic;
    return QualityLevel::Ultra; // default
}

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────

QualityPresetManager& QualityPresetManager::Get() {
    static QualityPresetManager instance;
    return instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Built-in Presets
// ─────────────────────────────────────────────────────────────────────────────

void QualityPresetManager::RegisterBuiltinPresets() {
    m_groups.clear();

    // ── Shadows ─────────────────────────────────────────────────────────
    //                                          Low     Med     High    Ultra   Cine
    m_groups.push_back({kGroupShadows, 3, {
        {"r.Shadow.MaxResolution",            { 512,    1024,   2048,   4096,   4096  }},
        {"r.Shadow.CSM.MaxCascades",          { 1,      2,      4,      4,      4     }},
        {"r.Shadow.DistanceScale",            { 0.6f,   0.8f,   1.0f,   1.0f,   1.0f  }},
        {"r.Shadow.RadiusThreshold",          { 0.06f,  0.04f,  0.03f,  0.01f,  0.01f }},
        {"r.Shadow.CSM.TransitionScale",      { 0.0f,   0.5f,   1.0f,   1.0f,   1.0f  }},
        {"r.Shadow.PCF.Taps",                 { 4,      8,      16,     16,     32    }},
        {"r.Shadow.ContactShadows",           { 0,      0,      1,      1,      1     }},
        {"r.DistanceFieldShadowing",          { 0,      0,      0,      1,      1     }},
    }});

    // ── Textures ────────────────────────────────────────────────────────
    m_groups.push_back({kGroupTextures, 3, {
        {"r.Texture.MipBias",                 { 2.0f,   1.0f,   0.0f,   0.0f,  -1.0f  }},
        {"r.Texture.MaxAniso",                { 2,      4,      8,      16,     16     }},
    }});

    // ── Post-Processing ─────────────────────────────────────────────────
    m_groups.push_back({kGroupPostProcess, 3, {
        {"r.TAA",                             { 0,      1,      1,      1,      1     }},
        {"r.Bloom",                           { 0,      1,      1,      1,      1     }},
        {"r.Bloom.Quality",                   { 0,      1,      2,      3,      4     }},
        {"r.GTAO",                            { 0,      0,      1,      1,      1     }},
        {"r.GTAO.Quality",                    { 0,      0,      1,      2,      3     }},
        {"r.SSR",                             { 0,      0,      0,      1,      1     }},
        {"r.MotionBlur",                      { 0,      0,      1,      1,      1     }},
        {"r.DOF",                             { 0,      0,      0,      1,      1     }},
        {"r.LensFlare",                       { 0,      0,      1,      1,      1     }},
        {"r.ChromaticAberration",             { 0,      0,      0,      1,      1     }},
        {"r.FilmGrain",                       { 0,      0,      0,      1,      1     }},
    }});

    // ── Effects ─────────────────────────────────────────────────────────
    m_groups.push_back({kGroupEffects, 3, {
        {"r.VFX.MaxParticles",                { 4096,   16384,  65536,  65536,  262144}},
        {"r.VolumetricClouds",                { 0,      0,      1,      1,      1     }},
        {"r.Cloud.PrimarySteps",              { 16,     24,     48,     64,     96    }},
        {"r.HeightFog",                       { 1,      1,      1,      1,      1     }},
    }});

    // ── View Distance ───────────────────────────────────────────────────
    m_groups.push_back({kGroupViewDistance, 3, {
        {"r.ViewDistanceScale",               { 0.4f,   0.6f,   0.8f,   1.0f,   1.4f  }},
        {"r.LOD.DistanceScale",               { 0.6f,   0.8f,   1.0f,   1.0f,   1.0f  }},
    }});

    // ── Global Illumination ─────────────────────────────────────────────
    m_groups.push_back({kGroupGI, 3, {
        {"r.SSGI",                            { 0,      0,      0,      1,      1     }},
        {"r.RadianceCache",                   { 0,      0,      0,      1,      1     }},
        {"r.SkyLight.SH",                     { 1,      1,      1,      1,      1     }},
    }});

    // ── Screen / Upscaling ──────────────────────────────────────────────
    m_groups.push_back({kGroupScreen, 3, {
        {"r.ScreenPercentage",                { 50,     67,     100,    100,    100   }},
    }});
}

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

void QualityPresetManager::Init() {
    if (m_initialized) return;
    RegisterBuiltinPresets();

    // Register quality CVars so they can be set via --cvar or Lua
    auto& cvars = CVarRegistry::Get();
    cvars.Register("quality", -1.f, "Overall quality preset (0=Low, 1=Med, 2=High, 3=Ultra, 4=Cine, -1=custom)", -1.f, 4.f);
    cvars.Register("quality.shadows",      3.f, "Shadow quality level (0-4)", 0.f, 4.f);
    cvars.Register("quality.textures",     3.f, "Texture quality level (0-4)", 0.f, 4.f);
    cvars.Register("quality.postprocess",  3.f, "Post-process quality level (0-4)", 0.f, 4.f);
    cvars.Register("quality.effects",      3.f, "Effects quality level (0-4)", 0.f, 4.f);
    cvars.Register("quality.view_distance", 3.f, "View distance quality level (0-4)", 0.f, 4.f);
    cvars.Register("quality.gi",           3.f, "GI quality level (0-4)", 0.f, 4.f);
    cvars.Register("quality.screen",       3.f, "Screen/upscaling quality level (0-4)", 0.f, 4.f);

    m_initialized = true;
    UZ_LOG(Engine, Info, "QualityPresetManager: initialized with {} groups, {} levels",
           m_groups.size(), kLevelCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply
// ─────────────────────────────────────────────────────────────────────────────

void QualityPresetManager::ApplyGroup(Group& group, int level) {
    level = std::clamp(level, 0, kLevelCount - 1);
    auto& cvars = CVarRegistry::Get();

    for (const auto& preset : group.presets) {
        float value = preset.values[level];
        cvars.Set(preset.cvarName, value);
    }

    group.currentLevel = level;
}

void QualityPresetManager::ApplyPreset(QualityLevel level) {
    if (!m_initialized) Init();

    const int lvl = static_cast<int>(level);
    for (auto& group : m_groups) {
        ApplyGroup(group, lvl);
    }

    UZ_LOG(Engine, Info, "QualityPresetManager: applied preset '{}' (level {}) to all {} groups",
           QualityLevelName(level), lvl, m_groups.size());
}

void QualityPresetManager::ApplyGroupPreset(const char* groupName, int level) {
    if (!m_initialized) Init();

    auto* group = FindGroup(groupName);
    if (!group) {
        UZ_LOG(Engine, Warning, "QualityPresetManager: unknown group '{}'", groupName);
        return;
    }

    ApplyGroup(*group, level);
    UZ_LOG(Engine, Info, "QualityPresetManager: group '{}' set to level {}",
           groupName, level);
}

// ─────────────────────────────────────────────────────────────────────────────
// Query
// ─────────────────────────────────────────────────────────────────────────────

int QualityPresetManager::GetGroupLevel(const char* groupName) const {
    const auto* group = FindGroup(groupName);
    return group ? group->currentLevel : -1;
}

QualityLevel QualityPresetManager::GetOverallLevel() const {
    int minLevel = kLevelCount - 1;
    for (const auto& group : m_groups) {
        minLevel = std::min(minLevel, group.currentLevel);
    }
    return static_cast<QualityLevel>(std::clamp(minLevel, 0, kLevelCount - 1));
}

// ─────────────────────────────────────────────────────────────────────────────
// Find
// ─────────────────────────────────────────────────────────────────────────────

QualityPresetManager::Group* QualityPresetManager::FindGroup(const char* name) {
    for (auto& g : m_groups) {
        if (std::string(g.name) == name) return &g;
    }
    return nullptr;
}

const QualityPresetManager::Group* QualityPresetManager::FindGroup(const char* name) const {
    for (const auto& g : m_groups) {
        if (std::string(g.name) == name) return &g;
    }
    return nullptr;
}

} // namespace uz
