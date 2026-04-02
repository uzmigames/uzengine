/// VFX Viewport Demo — Preset & Multi-Emitter Management
///
/// Extracted from vfx_viewport_demo.cpp to keep files under 1500 lines.
/// Contains preset scanning/loading, multi-emitter init/shutdown/reset,
/// compound preset building, and mesh world matrix computation.

#include "vfx_viewport_demo.h"
#include "log.h"

#include <filesystem>
#include <fstream>

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// Preset Management
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RefreshPresetList(const std::string& dir) {
    // Export built-in presets to JSON files (skip any that already exist so
    // user-edited presets are not overwritten on every launch).
    int builtInCount = 0;
    const VFXPreset* builtIns = GetVFXPresets(builtInCount);
    int written = 0;
    for (int i = 0; i < builtInCount; ++i) {
        // Derive a safe filename: lowercase name, spaces → underscores.
        std::string stem(builtIns[i].name);
        for (char& ch : stem) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (ch == ' ') ch = '_';
        }
        const std::string path = dir + stem + ".json";

        // Only write if the file does not already exist.
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            if (VFXPresetSerializer::Save(builtIns[i].config,
                                          builtIns[i].spawnRate,
                                          builtIns[i].name,
                                          path)) {
                ++written;
            }
        }
    }
    if (written > 0) {
        UZ_LOG(App, Info, "VFX: Wrote {} built-in preset(s) to '{}'", written, dir.c_str());
    }

    // Rescan the directory to populate m_presetFiles and m_presetNames.
    m_presetFiles = VFXPresetSerializer::ScanDirectory(dir);
    m_presetNames.clear();
    m_presetNames.reserve(m_presetFiles.size());
    for (const auto& p : m_presetFiles) {
        m_presetNames.push_back(std::filesystem::path(p).stem().string());
    }

    UZ_LOG(App, Info, "VFX: {} preset(s) available in '{}'",
           static_cast<int>(m_presetFiles.size()), dir.c_str());

    // Keep the panel's Presets browser in sync. This is a no-op before Init
    // (panel has no categories yet) because SetPresetList is also called
    // explicitly after the first RefreshPresetList() call in OnInit.
    m_paramPanel.SetPresetList(m_presetNames);
    m_editorUI.SetPresetNames(m_presetNames);
}

void VFXViewportDemo::LoadPreset(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_presetFiles.size())) {
        UZ_LOG(App, Warning, "VFX: LoadPreset({}) — index out of range ({} presets)",
               idx, static_cast<int>(m_presetFiles.size()));
        return;
    }

    auto& ae = ActiveEmitter();
    std::string name;
    float spawnRate = ae.spawnRate;
    if (!VFXPresetSerializer::Load(m_presetFiles[idx], ae.config, spawnRate, name)) {
        UZ_LOG(App, Warning, "VFX: Failed to load preset '{}'", m_presetFiles[idx].c_str());
        return;
    }
    ae.spawnRate     = spawnRate;
    m_vfxPresetIndex = idx;
    ae.burstPending  = true;
    UZ_LOG(App, Info, "VFX: Loaded preset '{}'", name.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Emitter Helpers
// ─────────────────────────────────────────────────────────────────────────────

VFXEmitterInstance& VFXViewportDemo::ActiveEmitter() {
    if (m_activeEmitterIndex < 0 || m_activeEmitterIndex >= static_cast<int>(m_emitters.size())) {
        m_activeEmitterIndex = 0;
    }
    return m_emitters[m_activeEmitterIndex];
}

const VFXEmitterInstance& VFXViewportDemo::ActiveEmitter() const {
    const int idx = (m_activeEmitterIndex >= 0 &&
                     m_activeEmitterIndex < static_cast<int>(m_emitters.size()))
                        ? m_activeEmitterIndex : 0;
    return m_emitters[idx];
}

void VFXViewportDemo::InitAllEmitters(IRHIDevice* device, ShaderManager* shaders,
                                       IRHICommandList* cmd) {
    for (auto& e : m_emitters) {
        if (!e.system->Init(device, shaders, e.poolSize)) {
            UZ_LOG(App, Error, "VFX: Failed to init emitter '{}'", e.name.c_str());
            e.enabled = false;
        } else {
            e.system->UploadInitialFreeList(cmd);
        }
    }
}

void VFXViewportDemo::ShutdownAllEmitters(IRHIDevice* device) {
    for (auto& e : m_emitters) {
        e.system->Shutdown(device);
    }
    m_emitters.clear();
}

void VFXViewportDemo::ResetAllEmitters(IRHICommandList* cmd) {
    for (auto& e : m_emitters) {
        e.system->UploadInitialFreeList(cmd);
        e.burstPending = true;
        e.spawnAccum = 0.0f;
        // Reset lifecycle counters so the emitter starts fresh.
        // @ref GDC 2019 "Niagara: Building the Future of FX in Unreal Engine" — emitter lifetime
        e.emitterAge  = 0.0f;
        e.currentLoop = 0;
        e.enabled     = true;  // re-enable in case it had auto-disabled.
    }
}

void VFXViewportDemo::BuildCompoundPreset(VFXCompoundPreset preset) {
    m_emitters.clear();
    m_activeEmitterIndex = 0;

    switch (preset) {
    case VFXCompoundPreset::Single: {
        // Single emitter — backward-compatible with legacy behavior.
        // Uses the first built-in preset config and full 50K pool.
        auto& e = m_emitters.emplace_back();
        e.name = "Default";
        int presetCount = 0;
        const VFXPreset* presets = GetVFXPresets(presetCount);
        e.config = presets[0].config;
        e.spawnRate = presets[0].spawnRate;
        e.burstCount = 500;
        e.poolSize = 50000;
        break;
    }

    case VFXCompoundPreset::Fire: {
        // Flame core: warm orange upward, medium size, additive glow
        {
            auto& flame = m_emitters.emplace_back();
            flame.name = "Flame";
            flame.config = {};
            flame.config.spawnVelocity[0] = 0.0f;
            flame.config.spawnVelocity[1] = 0.0f;
            flame.config.spawnVelocity[2] = 4.0f;  // Z-up
            flame.config.velocityVariation = 1.0f;
            flame.config.spreadAngle = 0.4f;
            flame.config.minLifetime = 0.5f;
            flame.config.maxLifetime = 1.5f;
            flame.config.initialSize = 0.08f;
            flame.config.initialColor[0] = 1.0f;
            flame.config.initialColor[1] = 0.5f;
            flame.config.initialColor[2] = 0.1f;
            flame.config.initialColor[3] = 0.9f;
            flame.config.gravity[0] = 0.0f;
            flame.config.gravity[1] = 0.0f;
            flame.config.gravity[2] = 2.0f; // buoyancy
            flame.config.damping = 0.3f;
            flame.config.emissiveIntensity = 3.0f;
            flame.config.blendMode = 1; // Additive
            flame.config.sizeOverLifeStart = 0.5f;
            flame.config.sizeOverLifePeak = 1.0f;
            flame.config.sizeOverLifeEnd = 0.0f;
            flame.config.sizeOverLifePeakTime = 0.3f;
            flame.config.colorLifeStartR = 1.0f;
            flame.config.colorLifeStartG = 0.7f;
            flame.config.colorLifeStartB = 0.2f;
            flame.config.colorLifeStartA = 1.0f;
            flame.config.colorLifeEndR = 1.0f;
            flame.config.colorLifeEndG = 0.2f;
            flame.config.colorLifeEndB = 0.0f;
            flame.config.colorLifeEndA = 0.0f;
            flame.config.softFadeDistance = 0.3f;
            flame.config.particleLightEnabled = 1;
            flame.config.particleLightRadius = 800.0f;
            flame.config.particleLightIntensity = 3.0f;
            flame.burstCount = 200;
            flame.spawnRate = 400.0f;
            flame.burstMode = false; // continuous
            flame.burstPending = false;
            flame.poolSize = 10000;
        }

        // Smoke: gray, slower, bigger, alpha blend, curl noise
        {
            auto& smoke = m_emitters.emplace_back();
            smoke.name = "Smoke";
            smoke.config = {};
            smoke.config.spawnVelocity[0] = 0.0f;
            smoke.config.spawnVelocity[1] = 0.0f;
            smoke.config.spawnVelocity[2] = 1.5f;
            smoke.config.velocityVariation = 0.5f;
            smoke.config.spreadAngle = 0.6f;
            smoke.config.minLifetime = 1.5f;
            smoke.config.maxLifetime = 3.5f;
            smoke.config.initialSize = 0.15f;
            smoke.config.initialColor[0] = 0.4f;
            smoke.config.initialColor[1] = 0.4f;
            smoke.config.initialColor[2] = 0.4f;
            smoke.config.initialColor[3] = 0.5f;
            smoke.config.gravity[0] = 0.0f;
            smoke.config.gravity[1] = 0.0f;
            smoke.config.gravity[2] = 0.5f; // slight buoyancy
            smoke.config.damping = 0.5f;
            smoke.config.emissiveIntensity = 0.0f;
            smoke.config.blendMode = 0; // AlphaBlend
            smoke.config.curlStrength = 2.0f;
            smoke.config.curlFrequency = 0.3f;
            smoke.config.curlSpeed = 0.15f;
            smoke.config.sizeOverLifeStart = 0.3f;
            smoke.config.sizeOverLifePeak = 1.0f;
            smoke.config.sizeOverLifeEnd = 1.2f;
            smoke.config.sizeOverLifePeakTime = 0.5f;
            smoke.config.colorLifeStartR = 0.5f;
            smoke.config.colorLifeStartG = 0.5f;
            smoke.config.colorLifeStartB = 0.5f;
            smoke.config.colorLifeStartA = 0.6f;
            smoke.config.colorLifeEndR = 0.3f;
            smoke.config.colorLifeEndG = 0.3f;
            smoke.config.colorLifeEndB = 0.3f;
            smoke.config.colorLifeEndA = 0.0f;
            smoke.config.softFadeDistance = 0.8f;
            smoke.positionOffset = {0.0f, 0.0f, 1.0f}; // above flame
            smoke.burstCount = 100;
            smoke.spawnRate = 150.0f;
            smoke.burstMode = false;
            smoke.burstPending = false;
            smoke.poolSize = 10000;
        }

        // Sparks: bright, fast, velocity-aligned stretch
        {
            auto& sparks = m_emitters.emplace_back();
            sparks.name = "Sparks";
            sparks.config = {};
            sparks.config.spawnVelocity[0] = 0.0f;
            sparks.config.spawnVelocity[1] = 0.0f;
            sparks.config.spawnVelocity[2] = 8.0f;
            sparks.config.velocityVariation = 5.0f;
            sparks.config.spreadAngle = 1.5f;
            sparks.config.minLifetime = 0.3f;
            sparks.config.maxLifetime = 0.8f;
            sparks.config.initialSize = 0.02f;
            sparks.config.spawnRadius = 0.05f;
            sparks.config.initialColor[0] = 1.0f;
            sparks.config.initialColor[1] = 0.8f;
            sparks.config.initialColor[2] = 0.3f;
            sparks.config.initialColor[3] = 1.0f;
            sparks.config.gravity[0] = 0.0f;
            sparks.config.gravity[1] = 0.0f;
            sparks.config.gravity[2] = -5.0f; // gravity pulls sparks down
            sparks.config.damping = 0.05f;
            sparks.config.emissiveIntensity = 4.0f;
            sparks.config.blendMode = 1; // Additive
            sparks.config.facingMode = 1; // VelocityAligned
            sparks.config.stretchFactor = 0.6f;
            sparks.config.sizeOverLifeStart = 1.0f;
            sparks.config.sizeOverLifePeak = 0.8f;
            sparks.config.sizeOverLifeEnd = 0.0f;
            sparks.config.sizeOverLifePeakTime = 0.2f;
            sparks.config.colorLifeStartR = 1.0f;
            sparks.config.colorLifeStartG = 0.9f;
            sparks.config.colorLifeStartB = 0.5f;
            sparks.config.colorLifeStartA = 1.0f;
            sparks.config.colorLifeEndR = 1.0f;
            sparks.config.colorLifeEndG = 0.3f;
            sparks.config.colorLifeEndB = 0.0f;
            sparks.config.colorLifeEndA = 0.0f;
            sparks.config.softFadeDistance = 0.1f;
            sparks.burstCount = 50;
            sparks.spawnRate = 80.0f;
            sparks.burstMode = false;
            sparks.burstPending = false;
            sparks.poolSize = 5000;
        }
        break;
    }

    case VFXCompoundPreset::Explosion: {
        // Flash: brief bright burst at center
        {
            auto& flash = m_emitters.emplace_back();
            flash.name = "Flash";
            flash.config = {};
            flash.config.spawnVelocity[0] = 0.0f;
            flash.config.spawnVelocity[1] = 0.0f;
            flash.config.spawnVelocity[2] = 0.5f;
            flash.config.velocityVariation = 0.3f;
            flash.config.spreadAngle = 3.14159f; // full sphere
            flash.config.minLifetime = 0.1f;
            flash.config.maxLifetime = 0.3f;
            flash.config.initialSize = 0.3f;
            flash.config.initialColor[0] = 1.0f;
            flash.config.initialColor[1] = 0.9f;
            flash.config.initialColor[2] = 0.7f;
            flash.config.initialColor[3] = 1.0f;
            flash.config.gravity[0] = 0.0f;
            flash.config.gravity[1] = 0.0f;
            flash.config.gravity[2] = 0.0f;
            flash.config.damping = 0.0f;
            flash.config.emissiveIntensity = 8.0f;
            flash.config.blendMode = 1; // Additive
            flash.config.sizeOverLifeStart = 0.5f;
            flash.config.sizeOverLifePeak = 1.0f;
            flash.config.sizeOverLifeEnd = 2.0f;
            flash.config.sizeOverLifePeakTime = 0.3f;
            flash.config.colorLifeStartR = 1.0f;
            flash.config.colorLifeStartG = 1.0f;
            flash.config.colorLifeStartB = 0.9f;
            flash.config.colorLifeStartA = 1.0f;
            flash.config.colorLifeEndR = 1.0f;
            flash.config.colorLifeEndG = 0.5f;
            flash.config.colorLifeEndB = 0.1f;
            flash.config.colorLifeEndA = 0.0f;
            flash.config.particleLightEnabled = 1;
            flash.config.particleLightRadius = 1200.0f;
            flash.config.particleLightIntensity = 5.0f;
            flash.burstCount = 80;
            flash.burstMode = true;
            flash.burstPending = true;
            flash.poolSize = 5000;
        }

        // Debris: heavy chunks flying outward
        {
            auto& debris = m_emitters.emplace_back();
            debris.name = "Debris";
            debris.config = {};
            debris.config.spawnVelocity[0] = 0.0f;
            debris.config.spawnVelocity[1] = 0.0f;
            debris.config.spawnVelocity[2] = 10.0f;
            debris.config.velocityVariation = 6.0f;
            debris.config.spreadAngle = 2.0f;
            debris.config.minLifetime = 0.5f;
            debris.config.maxLifetime = 2.0f;
            debris.config.initialSize = 0.04f;
            debris.config.initialColor[0] = 0.8f;
            debris.config.initialColor[1] = 0.4f;
            debris.config.initialColor[2] = 0.2f;
            debris.config.initialColor[3] = 1.0f;
            debris.config.gravity[0] = 0.0f;
            debris.config.gravity[1] = 0.0f;
            debris.config.gravity[2] = -9.81f;
            debris.config.damping = 0.1f;
            debris.config.emissiveIntensity = 1.0f;
            debris.config.blendMode = 0; // AlphaBlend
            debris.config.facingMode = 1; // VelocityAligned
            debris.config.stretchFactor = 0.4f;
            debris.config.groundCollision = 1;
            debris.config.restitution = 0.4f;
            debris.config.sizeOverLifeStart = 1.0f;
            debris.config.sizeOverLifePeak = 0.8f;
            debris.config.sizeOverLifeEnd = 0.2f;
            debris.config.sizeOverLifePeakTime = 0.1f;
            debris.burstCount = 120;
            debris.burstMode = true;
            debris.burstPending = true;
            debris.poolSize = 8000;
        }

        // Shockwave smoke: expanding outward ring
        {
            auto& shock = m_emitters.emplace_back();
            shock.name = "Shockwave";
            shock.config = {};
            shock.config.spawnVelocity[0] = 0.0f;
            shock.config.spawnVelocity[1] = 0.0f;
            shock.config.spawnVelocity[2] = 2.0f;
            shock.config.velocityVariation = 1.0f;
            shock.config.spreadAngle = 3.14159f;
            shock.config.minLifetime = 1.0f;
            shock.config.maxLifetime = 3.0f;
            shock.config.initialSize = 0.1f;
            shock.config.spawnShape = 3; // Ring
            shock.config.shapeParam1 = 0.5f; // ring width
            shock.config.spawnRadius = 0.5f;
            shock.config.initialColor[0] = 0.5f;
            shock.config.initialColor[1] = 0.45f;
            shock.config.initialColor[2] = 0.4f;
            shock.config.initialColor[3] = 0.6f;
            shock.config.gravity[0] = 0.0f;
            shock.config.gravity[1] = 0.0f;
            shock.config.gravity[2] = 0.3f;
            shock.config.damping = 0.4f;
            shock.config.emissiveIntensity = 0.0f;
            shock.config.blendMode = 0; // AlphaBlend
            shock.config.curlStrength = 1.5f;
            shock.config.curlFrequency = 0.4f;
            shock.config.curlSpeed = 0.2f;
            shock.config.sizeOverLifeStart = 0.5f;
            shock.config.sizeOverLifePeak = 1.0f;
            shock.config.sizeOverLifeEnd = 1.5f;
            shock.config.sizeOverLifePeakTime = 0.4f;
            shock.config.colorLifeStartR = 0.6f;
            shock.config.colorLifeStartG = 0.55f;
            shock.config.colorLifeStartB = 0.5f;
            shock.config.colorLifeStartA = 0.7f;
            shock.config.colorLifeEndR = 0.3f;
            shock.config.colorLifeEndG = 0.3f;
            shock.config.colorLifeEndB = 0.3f;
            shock.config.colorLifeEndA = 0.0f;
            shock.config.softFadeDistance = 0.6f;
            shock.burstCount = 150;
            shock.burstMode = true;
            shock.burstPending = true;
            shock.poolSize = 10000;
        }
        break;
    }

    case VFXCompoundPreset::Magic: {
        // Core glow: concentrated bright orb
        {
            auto& core = m_emitters.emplace_back();
            core.name = "Core Glow";
            core.config = {};
            core.config.spawnVelocity[0] = 0.0f;
            core.config.spawnVelocity[1] = 0.0f;
            core.config.spawnVelocity[2] = 0.5f;
            core.config.velocityVariation = 0.3f;
            core.config.spreadAngle = 3.14159f;
            core.config.minLifetime = 0.8f;
            core.config.maxLifetime = 1.5f;
            core.config.initialSize = 0.06f;
            core.config.spawnRadius = 0.15f;
            core.config.initialColor[0] = 0.3f;
            core.config.initialColor[1] = 0.5f;
            core.config.initialColor[2] = 1.0f;
            core.config.initialColor[3] = 0.8f;
            core.config.gravity[0] = 0.0f;
            core.config.gravity[1] = 0.0f;
            core.config.gravity[2] = 0.0f;
            core.config.damping = 0.8f;
            core.config.emissiveIntensity = 2.5f;
            core.config.blendMode = 1; // Additive
            core.config.sizeOverLifeStart = 0.5f;
            core.config.sizeOverLifePeak = 1.0f;
            core.config.sizeOverLifeEnd = 0.0f;
            core.config.sizeOverLifePeakTime = 0.3f;
            core.config.colorLifeStartR = 0.4f;
            core.config.colorLifeStartG = 0.6f;
            core.config.colorLifeStartB = 1.0f;
            core.config.colorLifeStartA = 1.0f;
            core.config.colorLifeEndR = 0.2f;
            core.config.colorLifeEndG = 0.3f;
            core.config.colorLifeEndB = 0.8f;
            core.config.colorLifeEndA = 0.0f;
            core.config.softFadeDistance = 0.2f;
            core.burstCount = 150;
            core.spawnRate = 200.0f;
            core.burstMode = false;
            core.burstPending = false;
            core.poolSize = 8000;
        }

        // Orbiting particles: swirling ring around core
        {
            auto& orbit = m_emitters.emplace_back();
            orbit.name = "Orbiting";
            orbit.config = {};
            orbit.config.spawnVelocity[0] = 0.0f;
            orbit.config.spawnVelocity[1] = 0.0f;
            orbit.config.spawnVelocity[2] = 1.0f;
            orbit.config.velocityVariation = 0.5f;
            orbit.config.spreadAngle = 0.8f;
            orbit.config.minLifetime = 1.0f;
            orbit.config.maxLifetime = 2.5f;
            orbit.config.initialSize = 0.03f;
            orbit.config.spawnRadius = 0.8f;
            orbit.config.spawnShape = 3; // Ring
            orbit.config.shapeParam1 = 0.2f; // ring width
            orbit.config.initialColor[0] = 0.5f;
            orbit.config.initialColor[1] = 0.8f;
            orbit.config.initialColor[2] = 1.0f;
            orbit.config.initialColor[3] = 1.0f;
            orbit.config.gravity[0] = 0.0f;
            orbit.config.gravity[1] = 0.0f;
            orbit.config.gravity[2] = 0.0f;
            orbit.config.damping = 0.2f;
            orbit.config.emissiveIntensity = 3.0f;
            orbit.config.blendMode = 1; // Additive
            // Vortex makes particles orbit the core
            orbit.config.vortexAxisDir[0] = 0.0f;
            orbit.config.vortexAxisDir[1] = 0.0f;
            orbit.config.vortexAxisDir[2] = 1.0f;
            orbit.config.vortexTangentialStrength = 6.0f;
            orbit.config.vortexRadialStrength = 1.5f; // inward pull
            orbit.config.vortexMaxRadius = 2.0f;
            orbit.config.sizeOverLifeStart = 1.0f;
            orbit.config.sizeOverLifePeak = 0.8f;
            orbit.config.sizeOverLifeEnd = 0.0f;
            orbit.config.sizeOverLifePeakTime = 0.2f;
            orbit.config.colorLifeStartR = 0.6f;
            orbit.config.colorLifeStartG = 0.9f;
            orbit.config.colorLifeStartB = 1.0f;
            orbit.config.colorLifeStartA = 1.0f;
            orbit.config.colorLifeEndR = 0.2f;
            orbit.config.colorLifeEndG = 0.4f;
            orbit.config.colorLifeEndB = 1.0f;
            orbit.config.colorLifeEndA = 0.0f;
            orbit.config.softFadeDistance = 0.15f;
            orbit.burstCount = 80;
            orbit.spawnRate = 100.0f;
            orbit.burstMode = false;
            orbit.burstPending = false;
            orbit.poolSize = 8000;
        }

        // Dust: ambient sparkle motes
        {
            auto& dust = m_emitters.emplace_back();
            dust.name = "Dust";
            dust.config = {};
            dust.config.spawnVelocity[0] = 0.0f;
            dust.config.spawnVelocity[1] = 0.0f;
            dust.config.spawnVelocity[2] = 0.3f;
            dust.config.velocityVariation = 0.2f;
            dust.config.spreadAngle = 3.14159f;
            dust.config.minLifetime = 2.0f;
            dust.config.maxLifetime = 4.0f;
            dust.config.initialSize = 0.01f;
            dust.config.spawnRadius = 2.0f;
            dust.config.initialColor[0] = 0.6f;
            dust.config.initialColor[1] = 0.8f;
            dust.config.initialColor[2] = 1.0f;
            dust.config.initialColor[3] = 0.5f;
            dust.config.gravity[0] = 0.0f;
            dust.config.gravity[1] = 0.0f;
            dust.config.gravity[2] = 0.1f; // slow float upward
            dust.config.damping = 0.3f;
            dust.config.emissiveIntensity = 1.0f;
            dust.config.blendMode = 1; // Additive
            dust.config.curlStrength = 0.5f;
            dust.config.curlFrequency = 0.2f;
            dust.config.curlSpeed = 0.1f;
            dust.config.sizeOverLifeStart = 0.0f;
            dust.config.sizeOverLifePeak = 1.0f;
            dust.config.sizeOverLifeEnd = 0.0f;
            dust.config.sizeOverLifePeakTime = 0.5f;
            dust.config.colorLifeStartR = 0.5f;
            dust.config.colorLifeStartG = 0.7f;
            dust.config.colorLifeStartB = 1.0f;
            dust.config.colorLifeStartA = 0.0f;
            dust.config.colorLifeEndR = 0.3f;
            dust.config.colorLifeEndG = 0.5f;
            dust.config.colorLifeEndB = 0.8f;
            dust.config.colorLifeEndA = 0.0f;
            dust.config.softFadeDistance = 0.2f;
            dust.burstCount = 60;
            dust.spawnRate = 40.0f;
            dust.burstMode = false;
            dust.burstPending = false;
            dust.poolSize = 5000;
        }
        break;
    }

    default:
        break;
    }

    m_compoundPreset = preset;
    UZ_LOG(App, Info, "VFX: Built compound preset '{}' with {} emitter(s)",
           static_cast<int>(preset), static_cast<int>(m_emitters.size()));
}

// ─────────────────────────────────────────────────────────────────────────────

} // namespace uz
