/// VfxSystemInstance -- runtime compound VFX instance implementation.

#include "vfx_system_instance.h"

#include "vfx_system.h"
#include "engine/logging/log.h"
#include "engine/vfx/components/emitter_presets.h"
#include "engine/vfx/components/particle_emitter.h"

#include <string_view>
#include <utility>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Preset Resolution — map "preset:<Name>" strings to emitter preset functions
// ─────────────────────────────────────────────────────────────────────────────

using PresetFn = void (*)(ParticleEmitter&);

/// Resolves a `"preset:<Name>"` path to the matching `ApplyPreset_<Name>`
/// function pointer, or `nullptr` if the name is unknown.
///
/// Only presets that are intended to be referenced by compound systems are
/// listed here. Expand the table as new compound-capable presets are added.
[[nodiscard]] PresetFn ResolvePreset(std::string_view name) {
    // Fireball compound children
    if (name == "FireballHead")   return &ApplyPreset_FireballHead;
    if (name == "FireballSparks") return &ApplyPreset_FireballSparks;
    if (name == "FireballEmbers") return &ApplyPreset_FireballEmbers;
    if (name == "FireballSmoke")  return &ApplyPreset_FireballSmoke;
    // Expose a few common standalone presets too so authors can glue them together
    if (name == "Fire")           return &ApplyPreset_Fire;
    if (name == "Smoke")          return &ApplyPreset_Smoke;
    if (name == "Sparks")         return &ApplyPreset_Sparks;
    return nullptr;
}

constexpr std::string_view kPresetPrefix = "preset:";

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// VfxSystemInstance::Init
// ─────────────────────────────────────────────────────────────────────────────

bool VfxSystemInstance::Init(const VfxSystem& asset, IRHIDevice* device,
                              ParticleBudget& budget) {
    if (!device) {
        UZ_LOG(VFX, Error, "VfxSystemInstance::Init: device is null");
        return false;
    }

    // Reset any prior state (defensive — re-Init is not expected but must not leak).
    m_emitters.clear();
    m_localSpace.clear();

    size_t attempted = 0;
    for (const auto& ref : asset.emitters) {
        if (!ref.enabled) continue;
        ++attempted;

        // Only the "preset:<Name>" prefix is supported here. Full .uzfx
        // loading is handled by the single-emitter sync path and can be
        // routed through this class later by adding a branch.
        if (ref.emitterPath.compare(0, kPresetPrefix.size(), kPresetPrefix) != 0) {
            UZ_LOG(VFX, Warning,
                   "VfxSystemInstance::Init: emitter '{}' path '{}' is not a "
                   "supported preset reference (expected 'preset:<Name>') — skipping",
                   ref.name, ref.emitterPath);
            continue;
        }

        const std::string_view presetName(ref.emitterPath.data() + kPresetPrefix.size(),
                                          ref.emitterPath.size() - kPresetPrefix.size());
        PresetFn fn = ResolvePreset(presetName);
        if (!fn) {
            UZ_LOG(VFX, Warning,
                   "VfxSystemInstance::Init: unknown preset '{}' for emitter '{}' — skipping",
                   presetName, ref.name);
            continue;
        }

        auto child = std::make_unique<ParticleEmitter>();
        fn(*child);
        child->graphPath = ref.emitterPath;
        child->isEmitting = m_emitting;
        child->worldPosition = m_worldPosition;

        if (!child->Initialize(device, budget)) {
            UZ_LOG(VFX, Error,
                   "VfxSystemInstance::Init: ParticleEmitter::Initialize failed for '{}'",
                   ref.name);
            continue;
        }
        // Match the inline-emitter path in scene_vfx_sync.cpp: defer initial
        // free-list upload until the render command list is available.
        child->needsInitialUpload = true;

        m_emitters.push_back(std::move(child));
        m_localSpace.push_back(ref.localSpace);
    }

    if (m_emitters.empty()) {
        UZ_LOG(VFX, Error,
               "VfxSystemInstance::Init: no child emitters initialised "
               "(attempted={}, asset='{}')",
               attempted, asset.name);
        return false;
    }

    // Apply asset-declared default parameters to the System scope (7.2.0).
    // Game code can still override later via Set*Parameter.
    for (const auto& p : asset.parameters) {
        switch (p.componentCount) {
            case 1:
                m_attributes.SetFloat(VfxAttributeScope::System, p.name, p.value.x);
                break;
            case 3:
                m_attributes.SetVec3(VfxAttributeScope::System, p.name,
                                     Vec3{p.value.x, p.value.y, p.value.z});
                break;
            case 4:
                m_attributes.SetVec4(VfxAttributeScope::System, p.name, p.value);
                break;
            default:
                // Defensive — componentCount is validated at load time.
                m_attributes.SetVec4(VfxAttributeScope::System, p.name, p.value);
                break;
        }
    }

    UZ_LOG(VFX, Info,
           "VfxSystemInstance::Init: asset='{}' -> {} child emitters, {} default params",
           asset.name, m_emitters.size(), asset.parameters.size());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxSystemInstance::Destroy
// ─────────────────────────────────────────────────────────────────────────────

void VfxSystemInstance::Destroy(IRHIDevice* device, ParticleBudget& budget) {
    if (!device) {
        // Still drop the emitters to avoid leaks — Shutdown is a no-op without device
        // but the unique_ptr destructor will not free GPU handles if Shutdown was skipped.
        m_emitters.clear();
        m_localSpace.clear();
        return;
    }
    for (auto& child : m_emitters) {
        if (child) {
            child->Shutdown(device, budget);
        }
    }
    m_emitters.clear();
    m_localSpace.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxSystemInstance::Tick
// ─────────────────────────────────────────────────────────────────────────────

void VfxSystemInstance::Tick(float dt) noexcept {
    if (dt < 0.0f) dt = 0.0f;
    for (auto& child : m_emitters) {
        if (!child) continue;
        child->isEmitting = m_emitting;
        child->timeSinceRegistration += dt;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxSystemInstance::SetWorldTransform
// ─────────────────────────────────────────────────────────────────────────────

void VfxSystemInstance::SetWorldTransform(const Vec3& position) noexcept {
    m_worldPosition = position;
    const size_t n = m_emitters.size();
    for (size_t i = 0; i < n; ++i) {
        auto& child = m_emitters[i];
        if (!child) continue;
        if (i < m_localSpace.size() && m_localSpace[i]) {
            child->worldPosition = position;
        } else {
            // World-space children stay anchored at world origin.
            child->worldPosition = Vec3{0.0f, 0.0f, 0.0f};
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxSystemInstance::GetEmitters
// ─────────────────────────────────────────────────────────────────────────────

std::vector<ParticleEmitter*> VfxSystemInstance::GetEmitters() const {
    std::vector<ParticleEmitter*> out;
    out.reserve(m_emitters.size());
    for (const auto& child : m_emitters) {
        out.push_back(child.get());
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxSystemInstance -- named user parameters (System scope)
// ─────────────────────────────────────────────────────────────────────────────

void VfxSystemInstance::SetFloatParameter(const std::string& name, float value) {
    m_attributes.SetFloat(VfxAttributeScope::System, name, value);
}

void VfxSystemInstance::SetVec3Parameter(const std::string& name, const Vec3& value) {
    m_attributes.SetVec3(VfxAttributeScope::System, name, value);
}

void VfxSystemInstance::SetVec4Parameter(const std::string& name, const Vec4& value) {
    m_attributes.SetVec4(VfxAttributeScope::System, name, value);
}

float VfxSystemInstance::GetFloatParameter(const std::string& name,
                                           float defaultValue) const {
    return m_attributes.GetFloat(VfxAttributeScope::System, name, defaultValue);
}

Vec3 VfxSystemInstance::GetVec3Parameter(const std::string& name,
                                         const Vec3& defaultValue) const {
    return m_attributes.GetVec3(VfxAttributeScope::System, name, defaultValue);
}

Vec4 VfxSystemInstance::GetVec4Parameter(const std::string& name,
                                         const Vec4& defaultValue) const {
    return m_attributes.GetVec4(VfxAttributeScope::System, name, defaultValue);
}

std::vector<std::string> VfxSystemInstance::GetParameterNames() const {
    return m_attributes.GetNames(VfxAttributeScope::System);
}

bool VfxSystemInstance::HasParameter(const std::string& name) const {
    return m_attributes.Has(VfxAttributeScope::System, name);
}

bool VfxSystemInstance::RemoveParameter(const std::string& name) {
    return m_attributes.Remove(VfxAttributeScope::System, name);
}

void VfxSystemInstance::ClearParameters() {
    m_attributes.Clear(VfxAttributeScope::System);
}

} // namespace uz
