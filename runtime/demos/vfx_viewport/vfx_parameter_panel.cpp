/// VFX Parameter Panel — Implementation
///
/// Builds the parameter tree from VFXEmitterConfig field pointers and handles
/// keyboard navigation + value adjustment. All feedback is via UZ_LOG console
/// output for simplicity (no GPU text rendering required).

#include "vfx_parameter_panel.h"
#include "vfx_viewport_vfx.h"
#include "input.h"
#include "uzengine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Enum Name Tables
// ─────────────────────────────────────────────────────────────────────────────

static const char* const kSpawnShapeNames[] = {
    "Point", "Sphere", "Box", "Ring", "Cone", "Cylinder", "Disc", "Line"
};
static constexpr uint32_t kSpawnShapeCount = 8;

static const char* const kBlendModeNames[] = {
    "AlphaBlend", "Additive", "PremulAlpha"
};
static constexpr uint32_t kBlendModeCount = 3;

/// Facing mode display names for the Rendering category combo box.
/// Must match the facingMode enum: 0=FaceCamera, 1=VelocityAligned,
/// 2=FaceCameraPlane, 3=CustomVector, 4=FaceCameraPosition.
/// @ref Latta 2004 "Inside Geometry Instancing" — camera-position billboard
/// @ref Valve 2006 "Rendering in Left 4 Dead" — axis-aligned billboard
static const char* const kFacingModeNames[] = {
    "Face Camera",         // 0: full spherical, tracks camera orientation
    "Velocity Aligned",    // 1: long axis follows velocity, trail-like
    "Face Camera Plane",   // 2: Y-locked, only rotates around world Z
    "Custom Vector",       // 3: long axis locked to a fixed world direction
    "Face Camera Position" // 4: faces camera EYE position, correct parallax at wide FOV
};
static constexpr uint32_t kFacingModeCount = 5;

static const char* const kLitModeNames[] = {
    "Unlit", "Lit"
};
static constexpr uint32_t kLitModeCount = 2;

static const char* const kBoolNames[] = {
    "OFF", "ON"
};
static constexpr uint32_t kBoolCount = 2;

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

void VFXParameterPanel::Init(VFXEmitterConfig* config, float* spawnRate,
                              uint32_t* burstCount, bool* burstMode,
                              bool* subEmitter, bool* frustumCull) {
    BuildCategories(config, spawnRate, burstCount, burstMode, subEmitter, frustumCull);
    m_selectedCategory = 0;
    m_selectedParam = 0;
    m_visible = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildCategories — wires pointers from VFXEmitterConfig into param entries
// ─────────────────────────────────────────────────────────────────────────────

void VFXParameterPanel::BuildCategories(VFXEmitterConfig* cfg, float* spawnRate,
                                         uint32_t* burstCount, bool* burstMode,
                                         bool* subEmitter, bool* frustumCull) {
    m_categories.clear();
    m_categories.reserve(8);

    // ── Category 0: Spawn ────────────────────────────────────────────────
    {
        VFXParamCategory cat;
        cat.name = "Spawn";
        cat.expanded = true;
        cat.params = {
            {"Spawn Rate",    VFXParamEntry::Type::Float, spawnRate, nullptr, 1.0f, 5000.0f, 10.0f},
            {"Burst Count",   VFXParamEntry::Type::Uint, nullptr, burstCount, 1.0f, 5000.0f, 0.0f, 50},
            {"Spawn Radius",  VFXParamEntry::Type::Float, &cfg->spawnRadius, nullptr, 0.0f, 10.0f, 0.05f},
            {"Shape",         VFXParamEntry::Type::Enum, nullptr, &cfg->spawnShape, 0.0f, 7.0f, 0.0f, 1, kSpawnShapeNames, kSpawnShapeCount, true},
            {"Surface Only",  VFXParamEntry::Type::Enum, nullptr, &cfg->spawnOnSurface, 0.0f, 1.0f, 0.0f, 1, kBoolNames, kBoolCount},
            {"Shape P1",      VFXParamEntry::Type::Float, &cfg->shapeParam1, nullptr, -20.0f, 20.0f, 0.1f},
            {"Shape P2",      VFXParamEntry::Type::Float, &cfg->shapeParam2, nullptr, -20.0f, 20.0f, 0.1f},
            {"Shape P3",      VFXParamEntry::Type::Float, &cfg->shapeParam3, nullptr, -20.0f, 20.0f, 0.1f},
        };
        m_categories.push_back(std::move(cat));
    }

    // ── Category 1: Particle ─────────────────────────────────────────────
    {
        VFXParamCategory cat;
        cat.name = "Particle";
        cat.expanded = false;
        cat.params = {
            {"Min Lifetime",   VFXParamEntry::Type::Float, &cfg->minLifetime, nullptr, 0.1f, 30.0f, 0.1f},
            {"Max Lifetime",   VFXParamEntry::Type::Float, &cfg->maxLifetime, nullptr, 0.1f, 30.0f, 0.1f},
            {"Initial Size",   VFXParamEntry::Type::Float, &cfg->initialSize, nullptr, 0.001f, 5.0f, 0.005f},
            {"Vel Variation",  VFXParamEntry::Type::Float, &cfg->velocityVariation, nullptr, 0.0f, 20.0f, 0.1f},
            {"Spread Angle",   VFXParamEntry::Type::Float, &cfg->spreadAngle, nullptr, 0.0f, 3.14159f, 0.01f},
            {"Velocity Z",     VFXParamEntry::Type::Float, &cfg->spawnVelocity[2], nullptr, -50.0f, 50.0f, 0.5f},
        };
        m_categories.push_back(std::move(cat));
    }

    // ── Category 2: Color ────────────────────────────────────────────────
    {
        VFXParamCategory cat;
        cat.name = "Color";
        cat.expanded = false;
        cat.params = {
            {"Start R", VFXParamEntry::Type::Float, &cfg->colorLifeStartR, nullptr, 0.0f, 1.0f, 0.01f},
            {"Start G", VFXParamEntry::Type::Float, &cfg->colorLifeStartG, nullptr, 0.0f, 1.0f, 0.01f},
            {"Start B", VFXParamEntry::Type::Float, &cfg->colorLifeStartB, nullptr, 0.0f, 1.0f, 0.01f},
            {"Start A", VFXParamEntry::Type::Float, &cfg->colorLifeStartA, nullptr, 0.0f, 1.0f, 0.01f},
            {"End R",   VFXParamEntry::Type::Float, &cfg->colorLifeEndR, nullptr, 0.0f, 1.0f, 0.01f},
            {"End G",   VFXParamEntry::Type::Float, &cfg->colorLifeEndG, nullptr, 0.0f, 1.0f, 0.01f},
            {"End B",   VFXParamEntry::Type::Float, &cfg->colorLifeEndB, nullptr, 0.0f, 1.0f, 0.01f},
            {"End A",   VFXParamEntry::Type::Float, &cfg->colorLifeEndA, nullptr, 0.0f, 1.0f, 0.01f},
        };
        m_categories.push_back(std::move(cat));
    }

    // ── Category 3: Size Over Lifetime ───────────────────────────────────
    {
        VFXParamCategory cat;
        cat.name = "Size Over Life";
        cat.expanded = false;
        cat.params = {
            {"Size Start", VFXParamEntry::Type::Float, &cfg->sizeOverLifeStart, nullptr, 0.0f, 5.0f, 0.05f},
            {"Size Peak",  VFXParamEntry::Type::Float, &cfg->sizeOverLifePeak, nullptr, 0.0f, 5.0f, 0.05f},
            {"Size End",   VFXParamEntry::Type::Float, &cfg->sizeOverLifeEnd, nullptr, 0.0f, 5.0f, 0.05f},
            {"Peak Time",  VFXParamEntry::Type::Float, &cfg->sizeOverLifePeakTime, nullptr, 0.0f, 1.0f, 0.01f},
        };
        m_categories.push_back(std::move(cat));
    }

    // ── Category 4: Physics ──────────────────────────────────────────────
    {
        VFXParamCategory cat;
        cat.name = "Physics";
        cat.expanded = false;
        cat.params = {
            {"Gravity Z",        VFXParamEntry::Type::Float, &cfg->gravity[2], nullptr, -100.0f, 100.0f, 0.5f},
            {"Damping",          VFXParamEntry::Type::Float, &cfg->damping, nullptr, 0.0f, 2.0f, 0.01f},
            {"Curl Strength",    VFXParamEntry::Type::Float, &cfg->curlStrength, nullptr, 0.0f, 10.0f, 0.1f},
            {"Curl Frequency",   VFXParamEntry::Type::Float, &cfg->curlFrequency, nullptr, 0.01f, 5.0f, 0.05f},
            {"Curl Speed",       VFXParamEntry::Type::Float, &cfg->curlSpeed, nullptr, 0.0f, 5.0f, 0.05f},
            {"Ground Collision", VFXParamEntry::Type::Enum, nullptr, &cfg->groundCollision, 0.0f, 1.0f, 0.0f, 1, kBoolNames, kBoolCount},
            {"Restitution",      VFXParamEntry::Type::Float, &cfg->restitution, nullptr, 0.0f, 1.0f, 0.05f},
            // ── Orbit (Task 4.9.0) ─────────────────────────────────────────────
            // @ref Christensen 2018 "Fibonacci Sphere Sampling" — golden angle for uniform spread
            {"Orbit",            VFXParamEntry::Type::Enum, nullptr, &cfg->orbitEnabled, 0.0f, 1.0f, 0.0f, 1, kBoolNames, kBoolCount},
            {"Orbit Radius",     VFXParamEntry::Type::Float, &cfg->orbitRadius, nullptr, 0.0f, 1000.0f, 5.0f},
            {"Orbit Speed",      VFXParamEntry::Type::Float, &cfg->orbitSpeed, nullptr, -10.0f, 10.0f, 0.05f},
            {"Orbit Axis X",     VFXParamEntry::Type::Float, &cfg->orbitAxisX, nullptr, -1.0f, 1.0f, 0.05f},
            {"Orbit Axis Y",     VFXParamEntry::Type::Float, &cfg->orbitAxisY, nullptr, -1.0f, 1.0f, 0.05f},
            {"Orbit Axis Z",     VFXParamEntry::Type::Float, &cfg->orbitAxisZ, nullptr, -1.0f, 1.0f, 0.05f},
        };
        m_categories.push_back(std::move(cat));
    }

    // ── Category 5: Forces ───────────────────────────────────────────────
    {
        VFXParamCategory cat;
        cat.name = "Forces";
        cat.expanded = false;
        cat.params = {
            {"Vortex Swirl",    VFXParamEntry::Type::Float, &cfg->vortexTangentialStrength, nullptr, -20.0f, 20.0f, 0.5f},
            {"Vortex Radial",   VFXParamEntry::Type::Float, &cfg->vortexRadialStrength, nullptr, -20.0f, 20.0f, 0.5f},
            {"Vortex Radius",   VFXParamEntry::Type::Float, &cfg->vortexMaxRadius, nullptr, 0.1f, 50.0f, 0.5f},
            {"Wind Strength",   VFXParamEntry::Type::Float, &cfg->windStrength, nullptr, 0.0f, 50.0f, 0.5f},
            {"Wind Dir X",      VFXParamEntry::Type::Float, &cfg->windDir[0], nullptr, -1.0f, 1.0f, 0.05f},
            {"Wind Dir Y",      VFXParamEntry::Type::Float, &cfg->windDir[1], nullptr, -1.0f, 1.0f, 0.05f},
            {"Wind Dir Z",      VFXParamEntry::Type::Float, &cfg->windDir[2], nullptr, -1.0f, 1.0f, 0.05f},
        };
        m_categories.push_back(std::move(cat));
    }

    // ── Category 6: Rendering ────────────────────────────────────────────
    {
        VFXParamCategory cat;
        cat.name = "Rendering";
        cat.expanded = false;
        cat.params = {
            {"Blend Mode", VFXParamEntry::Type::Enum, nullptr, &cfg->blendMode, 0.0f, 2.0f, 0.0f, 1, kBlendModeNames, kBlendModeCount},
            {"Emissive",   VFXParamEntry::Type::Float, &cfg->emissiveIntensity, nullptr, 0.0f, 20.0f, 0.1f},
            {"Soft Fade",  VFXParamEntry::Type::Float, &cfg->softFadeDistance, nullptr, 0.0f, 5.0f, 0.05f},
            {"Alpha Erosion", VFXParamEntry::Type::Float, nullptr, nullptr, 0.0f, 1.0f, 0.01f},
            {"Facing Mode",    VFXParamEntry::Type::Enum, nullptr, &cfg->facingMode, 0.0f, 4.0f, 0.0f, 1, kFacingModeNames, kFacingModeCount},
            {"Custom Dir X",   VFXParamEntry::Type::Float, &cfg->customFacingX, nullptr, -1.0f, 1.0f, 0.05f},
            {"Custom Dir Y",   VFXParamEntry::Type::Float, &cfg->customFacingY, nullptr, -1.0f, 1.0f, 0.05f},
            {"Custom Dir Z",   VFXParamEntry::Type::Float, &cfg->customFacingZ, nullptr, -1.0f, 1.0f, 0.05f},
            {"Stretch",    VFXParamEntry::Type::Float, &cfg->stretchFactor, nullptr, 0.0f, 5.0f, 0.1f},
            {"Lit Mode",   VFXParamEntry::Type::Enum, nullptr, &cfg->litMode, 0.0f, 1.0f, 0.0f, 1, kLitModeNames, kLitModeCount},
        };
        m_categories.push_back(std::move(cat));
    }

    // ── Category 7: System ───────────────────────────────────────────────
    // burstMode and subEmitter are bool*, not uint32_t*, so we need to handle
    // them specially. We cast bool* to uint32_t* indirectly via a wrapper.
    // Since bool is 1 byte and uint32_t is 4 bytes, we cannot safely cast.
    // Instead, we use a separate approach: store these as floats that round to
    // 0 or 1, or use the Bool type which reads the uintPtr.
    //
    // For safety, we treat system bools as read-only display + manual toggle
    // in HandleInput with special-case logic.
    {
        VFXParamCategory cat;
        cat.name = "System";
        cat.expanded = false;
        // System params use special handling — see HandleInput for bool toggles.
        // We store nullptr for both float/uint pointers and handle them via index.
        cat.params = {
            {"Burst Mode",     VFXParamEntry::Type::Bool, nullptr, nullptr, 0.0f, 1.0f},
            {"Sub Emitter",    VFXParamEntry::Type::Bool, nullptr, nullptr, 0.0f, 1.0f},
            {"Frustum Cull",   VFXParamEntry::Type::Bool, nullptr, nullptr, 0.0f, 1.0f},
            {"Active Particles", VFXParamEntry::Type::Uint, nullptr, nullptr, 0.0f, 0.0f},
        };
        m_categories.push_back(std::move(cat));
    }

    // Store the bool pointers separately for the System category special handling.
    // They are captured in Init() and accessed via m_burstMode, m_subEmitter, m_frustumCull.
    m_burstMode = burstMode;
    m_subEmitter = subEmitter;
    m_frustumCull = frustumCull;

    // ── Category 8: Presets ──────────────────────────────────────────────
    // This category holds no VFXParamEntry rows — the preset list is stored
    // separately in m_presetNames and rendered via LogCurrentCategory override.
    // Navigation (Up/Down) changes m_selectedPreset; Enter triggers m_onPresetLoad.
    {
        VFXParamCategory cat;
        cat.name = "Presets";
        cat.expanded = false;
        // No params — managed via m_presetNames vector.
        m_categories.push_back(std::move(cat));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Toggle / SetVisible
// ─────────────────────────────────────────────────────────────────────────────

void VFXParameterPanel::Toggle() {
    m_visible = !m_visible;
    if (m_visible) {
        UZ_LOG(App, Info, "──── VFX Parameter Panel OPENED ────");
        UZ_LOG(App, Info, "  Up/Down: navigate   Left/Right: adjust   Tab: next category");
        UZ_LOG(App, Info, "  Enter: expand/collapse   R: restart   F10/Esc: close");
        LogCurrentCategory();
    } else {
        UZ_LOG(App, Info, "──── VFX Parameter Panel CLOSED ────");
    }
}

void VFXParameterPanel::SetPresetList(const std::vector<std::string>& names) {
    m_presetNames = names;
    // Clamp selected preset cursor to valid range after list change.
    if (!m_presetNames.empty()) {
        m_selectedPreset = std::min(m_selectedPreset, static_cast<int>(m_presetNames.size()) - 1);
        m_selectedPreset = std::max(m_selectedPreset, 0);
    } else {
        m_selectedPreset = 0;
    }
}

void VFXParameterPanel::SetPresetLoadCallback(std::function<void(int)> cb) {
    m_onPresetLoad = std::move(cb);
}

void VFXParameterPanel::SetVisible(bool visible) {
    if (visible != m_visible) {
        m_visible = visible;
        if (m_visible) {
            UZ_LOG(App, Info, "──── VFX Parameter Panel OPENED ────");
            LogCurrentCategory();
        } else {
            UZ_LOG(App, Info, "──── VFX Parameter Panel CLOSED ────");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// HandleInput — keyboard navigation and value adjustment
// ─────────────────────────────────────────────────────────────────────────────

bool VFXParameterPanel::HandleInput(const InputSystem* input, bool& needsRestart) {
    if (!m_visible) {
        return false;
    }

    if (m_categories.empty()) {
        return false;
    }

    const auto& cat = m_categories[static_cast<size_t>(m_selectedCategory)];

    // Close panel
    if (input->IsKeyJustPressed(Key::Escape) || input->IsKeyJustPressed(Key::F10)) {
        m_visible = false;
        UZ_LOG(App, Info, "──── VFX Parameter Panel CLOSED ────");
        return true;
    }

    // Tab / Shift+Tab: switch category
    if (input->IsKeyJustPressed(Key::Tab)) {
        const bool shift = input->IsKeyDown(Key::LeftShift) || input->IsKeyDown(Key::RightShift);
        const int catCount = static_cast<int>(m_categories.size());
        if (shift) {
            m_selectedCategory = (m_selectedCategory + catCount - 1) % catCount;
        } else {
            m_selectedCategory = (m_selectedCategory + 1) % catCount;
        }
        m_selectedParam = 0;
        LogCurrentCategory();
        return true;
    }

    // Enter: toggle expand/collapse
    if (input->IsKeyJustPressed(Key::Enter)) {
        m_categories[static_cast<size_t>(m_selectedCategory)].expanded =
            !m_categories[static_cast<size_t>(m_selectedCategory)].expanded;
        LogCurrentCategory();
        return true;
    }

    // R: restart emission
    if (input->IsKeyJustPressed(Key::R)) {
        needsRestart = true;
        UZ_LOG(App, Info, "[VFX Panel] Restarting emission...");
        return true;
    }

    // ── Presets category special handling (index 8) ──────────────────────
    // Up/Down moves the preset row cursor; Enter fires the load callback.
    // The footer hint (Shift+S / F6 / Del) is always shown in LogCurrentCategory.
    if (m_selectedCategory == 8) {
        if (cat.expanded) {
            const int presetCount = static_cast<int>(m_presetNames.size());
            if (presetCount > 0) {
                if (input->IsKeyJustPressed(Key::Up)) {
                    m_selectedPreset = (m_selectedPreset + presetCount - 1) % presetCount;
                    UZ_LOG(App, Info, "  >> [{}] {}", m_selectedPreset + 1,
                           m_presetNames[static_cast<size_t>(m_selectedPreset)].c_str());
                    return true;
                }
                if (input->IsKeyJustPressed(Key::Down)) {
                    m_selectedPreset = (m_selectedPreset + 1) % presetCount;
                    UZ_LOG(App, Info, "  >> [{}] {}", m_selectedPreset + 1,
                           m_presetNames[static_cast<size_t>(m_selectedPreset)].c_str());
                    return true;
                }
                if (input->IsKeyJustPressed(Key::Enter)) {
                    if (m_onPresetLoad) {
                        UZ_LOG(App, Info, "[Presets] Loading '{}'...",
                               m_presetNames[static_cast<size_t>(m_selectedPreset)].c_str());
                        m_onPresetLoad(m_selectedPreset);
                        needsRestart = true;
                    }
                    return true;
                }
            } else {
                if (input->IsKeyJustPressed(Key::Up) ||
                    input->IsKeyJustPressed(Key::Down) ||
                    input->IsKeyJustPressed(Key::Enter)) {
                    UZ_LOG(App, Info, "[Presets] No presets found. Press Shift+S to save one.");
                    return true;
                }
            }
        }
        // Consume Left/Right (no adjustable values in Presets category).
        if (input->IsKeyJustPressed(Key::Left) || input->IsKeyJustPressed(Key::Right)) {
            return true;
        }
    }

    // Up/Down: navigate within expanded category
    if (cat.expanded && !cat.params.empty()) {
        const int paramCount = static_cast<int>(cat.params.size());

        if (input->IsKeyJustPressed(Key::Up)) {
            m_selectedParam = (m_selectedParam + paramCount - 1) % paramCount;
            LogCurrentParam();
            return true;
        }
        if (input->IsKeyJustPressed(Key::Down)) {
            m_selectedParam = (m_selectedParam + 1) % paramCount;
            LogCurrentParam();
            return true;
        }

        // Left/Right: adjust value
        if (input->IsKeyJustPressed(Key::Left) || input->IsKeyJustPressed(Key::Right)) {
            const float direction = input->IsKeyJustPressed(Key::Right) ? 1.0f : -1.0f;
            auto& entry = m_categories[static_cast<size_t>(m_selectedCategory)]
                              .params[static_cast<size_t>(m_selectedParam)];

            // Special handling for System category bools
            if (m_selectedCategory == 7) {
                HandleSystemParamAdjust(m_selectedParam, direction, needsRestart);
                LogCurrentParam();
                return true;
            }

            switch (entry.type) {
                case VFXParamEntry::Type::Float:
                    AdjustFloat(entry, direction);
                    break;
                case VFXParamEntry::Type::Uint:
                    AdjustUint(entry, static_cast<int>(direction));
                    break;
                case VFXParamEntry::Type::Bool:
                    ToggleBool(entry);
                    break;
                case VFXParamEntry::Type::Enum:
                    CycleEnum(entry, static_cast<int>(direction));
                    break;
            }

            if (entry.needsRestart) {
                needsRestart = true;
            }

            // Renormalize wind direction after editing wind dir components
            if (m_selectedCategory == 5 && m_selectedParam >= 4 && m_selectedParam <= 6) {
                RenormalizeWindDir();
            }

            LogCurrentParam();
            return true;
        }
    }

    // Consume all other keys while panel is visible to prevent pass-through.
    // Only consume keys that are explicitly handled above or navigation-related.
    // Let mouse/camera input pass through.
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// System category special handling (bool* parameters)
// ─────────────────────────────────────────────────────────────────────────────

void VFXParameterPanel::HandleSystemParamAdjust(int paramIndex, float /*direction*/,
                                                  bool& needsRestart) {
    switch (paramIndex) {
        case 0: // Burst Mode
            if (m_burstMode) {
                *m_burstMode = !*m_burstMode;
                needsRestart = true;
            }
            break;
        case 1: // Sub Emitter
            if (m_subEmitter) {
                *m_subEmitter = !*m_subEmitter;
            }
            break;
        case 2: // Frustum Cull
            if (m_frustumCull) {
                *m_frustumCull = !*m_frustumCull;
            }
            break;
        case 3: // Active Particles (read-only)
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Wind direction renormalization
// ─────────────────────────────────────────────────────────────────────────────

void VFXParameterPanel::RenormalizeWindDir() {
    // Forces category is index 5, windDir params are indices 4,5,6
    auto& forces = m_categories[5];
    if (forces.params.size() < 7) return;

    float* dx = forces.params[4].floatPtr;
    float* dy = forces.params[5].floatPtr;
    float* dz = forces.params[6].floatPtr;
    if (!dx || !dy || !dz) return;

    const float len = std::sqrt((*dx) * (*dx) + (*dy) * (*dy) + (*dz) * (*dz));
    if (len > 0.0001f) {
        *dx /= len;
        *dy /= len;
        *dz /= len;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Value adjustment helpers
// ─────────────────────────────────────────────────────────────────────────────

void VFXParameterPanel::AdjustFloat(VFXParamEntry& entry, float direction) {
    if (!entry.floatPtr) return;
    *entry.floatPtr += entry.step * direction;
    *entry.floatPtr = std::clamp(*entry.floatPtr, entry.minVal, entry.maxVal);
}

void VFXParameterPanel::AdjustUint(VFXParamEntry& entry, int direction) {
    if (!entry.uintPtr) return;
    const int64_t current = static_cast<int64_t>(*entry.uintPtr);
    const int64_t newVal = current + static_cast<int64_t>(entry.uintStep) * direction;
    const int64_t clamped = std::clamp(newVal, static_cast<int64_t>(entry.minVal),
                                        static_cast<int64_t>(entry.maxVal));
    *entry.uintPtr = static_cast<uint32_t>(clamped);
}

void VFXParameterPanel::ToggleBool(VFXParamEntry& entry) {
    if (entry.uintPtr) {
        *entry.uintPtr = (*entry.uintPtr == 0) ? 1u : 0u;
    }
}

void VFXParameterPanel::CycleEnum(VFXParamEntry& entry, int direction) {
    if (!entry.uintPtr || entry.enumCount == 0) return;
    const int current = static_cast<int>(*entry.uintPtr);
    const int count = static_cast<int>(entry.enumCount);
    *entry.uintPtr = static_cast<uint32_t>((current + direction + count) % count);
}

// ─────────────────────────────────────────────────────────────────────────────
// Logging helpers
// ─────────────────────────────────────────────────────────────────────────────

void VFXParameterPanel::FormatParamValue(const VFXParamEntry& entry,
                                          char* buf, uint32_t bufSize) const {
    // Special handling for System category (index 7)
    if (&entry >= m_categories[7].params.data() &&
        &entry < m_categories[7].params.data() + m_categories[7].params.size()) {

        const auto idx = static_cast<size_t>(&entry - m_categories[7].params.data());
        switch (idx) {
            case 0: // Burst Mode
                std::snprintf(buf, bufSize, "%s", (m_burstMode && *m_burstMode) ? "ON" : "OFF");
                return;
            case 1: // Sub Emitter
                std::snprintf(buf, bufSize, "%s", (m_subEmitter && *m_subEmitter) ? "ON" : "OFF");
                return;
            case 2: // Frustum Cull
                std::snprintf(buf, bufSize, "%s", (m_frustumCull && *m_frustumCull) ? "ON" : "OFF");
                return;
            case 3: // Active Particles
                std::snprintf(buf, bufSize, "N/A");
                return;
            default:
                break;
        }
    }

    switch (entry.type) {
        case VFXParamEntry::Type::Float:
            if (entry.floatPtr) {
                std::snprintf(buf, bufSize, "%.2f  [%.2f, %.2f]", *entry.floatPtr, entry.minVal, entry.maxVal);
            } else {
                std::snprintf(buf, bufSize, "N/A");
            }
            break;

        case VFXParamEntry::Type::Uint:
            if (entry.uintPtr) {
                std::snprintf(buf, bufSize, "%u  [%.0f, %.0f]", *entry.uintPtr, entry.minVal, entry.maxVal);
            } else {
                std::snprintf(buf, bufSize, "N/A");
            }
            break;

        case VFXParamEntry::Type::Bool:
            if (entry.uintPtr) {
                std::snprintf(buf, bufSize, "%s", (*entry.uintPtr != 0) ? "ON" : "OFF");
            } else {
                std::snprintf(buf, bufSize, "N/A");
            }
            break;

        case VFXParamEntry::Type::Enum:
            if (entry.uintPtr && entry.enumNames && *entry.uintPtr < entry.enumCount) {
                std::snprintf(buf, bufSize, "%s (%u/%u)", entry.enumNames[*entry.uintPtr],
                              *entry.uintPtr + 1, entry.enumCount);
            } else {
                std::snprintf(buf, bufSize, "N/A");
            }
            break;
    }
}

void VFXParameterPanel::LogCurrentCategory() const {
    if (m_categories.empty()) return;

    const auto& cat = m_categories[static_cast<size_t>(m_selectedCategory)];
    UZ_LOG(App, Info, "");
    UZ_LOG(App, Info, "── [{}] {} {} ──",
           m_selectedCategory + 1, cat.name, cat.expanded ? "(expanded)" : "(collapsed)");

    if (!cat.expanded) {
        UZ_LOG(App, Info, "  Press Enter to expand");
        return;
    }

    // ── Presets category: show preset name list + footer ─────────────────
    if (m_selectedCategory == 8) {
        if (m_presetNames.empty()) {
            UZ_LOG(App, Info, "  (no presets found — press Shift+S to save the current emitter)");
        } else {
            for (int i = 0; i < static_cast<int>(m_presetNames.size()); ++i) {
                const char* marker = (i == m_selectedPreset) ? " >> " : "    ";
                UZ_LOG(App, Info, "{}[{}] {}", marker, i + 1, m_presetNames[static_cast<size_t>(i)].c_str());
            }
        }
        UZ_LOG(App, Info, "  {} preset(s)  [Enter: load]  [Shift+S: save]  [Del: delete]  [F6: refresh]",
               static_cast<int>(m_presetNames.size()));
        return;
    }

    for (size_t i = 0; i < cat.params.size(); ++i) {
        const auto& p = cat.params[i];
        char valueBuf[64] = {};
        FormatParamValue(p, valueBuf, sizeof(valueBuf));
        const char* marker = (static_cast<int>(i) == m_selectedParam) ? " >> " : "    ";
        UZ_LOG(App, Info, "{}{}: {}{}", marker, p.name, valueBuf,
               p.needsRestart ? "  [R to restart]" : "");
    }
}

void VFXParameterPanel::LogCurrentParam() const {
    if (m_categories.empty()) return;

    const auto& cat = m_categories[static_cast<size_t>(m_selectedCategory)];
    if (!cat.expanded || cat.params.empty()) return;

    const auto& p = cat.params[static_cast<size_t>(m_selectedParam)];
    char valueBuf[64] = {};
    FormatParamValue(p, valueBuf, sizeof(valueBuf));
    UZ_LOG(App, Info, "[{}/{}] {} = {}{}", cat.name, p.name, p.name, valueBuf,
           p.needsRestart ? "  [R to restart]" : "");
}

} // namespace uz
