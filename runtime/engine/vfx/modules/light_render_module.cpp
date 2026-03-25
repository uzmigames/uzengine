/// LightRenderModule implementation -- post-compute point light injection.
///
/// This module has no compute shader contribution. Its implementation is
/// entirely CPU-side: `GetReadbackBufferSize()` sizes the readback allocation,
/// and `ProcessReadback()` converts GPU particle data into validated
/// `LightParticleData` entries for the light system.

#include "light_render_module.h"

#include "engine/logging/log.h"

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum value for `maxLights` accepted by `ParseConfig`.
/// Bounds the readback buffer size to 256 * 32 = 8 192 bytes.
constexpr uint32_t kMaxLightsUpperBound = 256u;

/// Minimum value for `maxLights`. An emitter with zero lights would be a no-op.
constexpr uint32_t kMaxLightsLowerBound = 1u;

/// Minimum allowed radius. Prevents division-by-zero in attenuation math.
constexpr float kMinRadius = 0.01f;

/// Maximum NaN-skip warnings emitted per `ProcessReadback` call.
/// Avoids log flooding when an emitter has many corrupt particles.
constexpr uint32_t kMaxNanWarningsPerCall = 4u;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns true if any component of `v` is NaN or infinity.
[[nodiscard]] bool IsNonFinite(const Vec3& v) noexcept {
    return !std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z);
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

/// Stores a copy of `config`. All validation is expected to have been performed
/// by `ParseConfig()` or by the caller before construction.
LightRenderModule::LightRenderModule(const LightRenderModuleConfig& config)
    : config_(config)
{
}

// ─────────────────────────────────────────────────────────────────────────────
// IVFXModule interface
// ─────────────────────────────────────────────────────────────────────────────

/// Returns "light_render" -- the stable registry and serialization key.
const char* LightRenderModule::GetName() const noexcept {
    return "light_render";
}

/// Returns an empty string view -- this module adds nothing to the compute shader.
///
/// The `ModuleCompositor` must skip empty snippets to avoid emitting spurious
/// blank lines or newlines into the generated HLSL.
std::string_view LightRenderModule::GetShaderCode() const {
    // Intentionally empty: light injection is CPU-side post-compute, not GPU.
    return {};
}

/// Returns no parameters -- LightRenderModule is configured via
/// `LightRenderModuleConfig`, not the constant-buffer parameter system.
///
/// The light system receives positions and colors directly from the readback
/// buffer; there are no per-particle shader constants to expose.
std::vector<ParameterDef> LightRenderModule::GetParameters() const {
    return {};
}

/// No-op. All work occurs in `ProcessReadback()` after the compute dispatch.
void LightRenderModule::Execute(const ExecuteContext& /*ctx*/) {
    // Post-compute readback module -- no pre-dispatch CPU work needed.
}

// ─────────────────────────────────────────────────────────────────────────────
// LightRenderModule-specific API
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the stored configuration by const reference.
const LightRenderModuleConfig& LightRenderModule::GetConfig() const noexcept {
    return config_;
}

/// Computes the readback buffer size as `maxLights * sizeof(LightParticleData)`.
///
/// This is called once at emitter creation time to allocate the readback resource.
/// The 32-byte stride matches the GPU-side particle attribute layout so the
/// buffer can be mapped and reinterpreted without any data transformation.
uint32_t LightRenderModule::GetReadbackBufferSize() const noexcept {
    return config_.maxLights * static_cast<uint32_t>(sizeof(LightParticleData));
}

/// Reads validated light entries from a completed GPU->CPU readback buffer.
///
/// Steps:
/// 1. Guard against null input or zero alive count (return empty vector).
/// 2. Warn if the supplied buffer is smaller than the declared readback size --
///    this indicates a mismatch between buffer allocation and module config.
/// 3. Clamp the read count to `min(aliveCount, maxLights, safe_entry_count)`.
/// 4. Iterate entries, skipping any whose position contains a non-finite value.
/// 5. Return the surviving entries.
std::vector<LightParticleData> LightRenderModule::ProcessReadback(
    const void*  readbackData,
    uint32_t     dataSize,
    uint32_t     aliveCount) const
{
    if (readbackData == nullptr || aliveCount == 0) {
        return {};
    }

    const uint32_t expectedSize = GetReadbackBufferSize();
    if (dataSize < expectedSize) {
        UZ_LOG(VFX, Warning,
               "LightRenderModule: readback buffer too small "
               "(got {} B, expected {} B) -- some lights may be skipped",
               dataSize, expectedSize);
    }

    // How many complete entries fit in the supplied buffer?
    const uint32_t safeEntryCount =
        dataSize / static_cast<uint32_t>(sizeof(LightParticleData));

    // Read at most maxLights entries, never exceeding alive particles or buffer.
    const uint32_t readCount =
        std::min({ aliveCount, config_.maxLights, safeEntryCount });

    std::vector<LightParticleData> result;
    result.reserve(readCount);

    const auto* entries =
        static_cast<const LightParticleData*>(readbackData);

    uint32_t nanWarnings = 0;

    for (uint32_t i = 0; i < readCount; ++i) {
        LightParticleData entry{};
        // Use memcpy to avoid potential alignment UB when reading from a raw buffer.
        std::memcpy(&entry, entries + i, sizeof(LightParticleData));

        if (IsNonFinite(entry.position)) {
            if (nanWarnings < kMaxNanWarningsPerCall) {
                UZ_LOG(VFX, Warning,
                       "LightRenderModule: particle[{}] has non-finite position "
                       "({}, {}, {}) -- skipping light",
                       i, entry.position.x, entry.position.y, entry.position.z);
                ++nanWarnings;
            }
            continue;
        }

        result.push_back(entry);
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON deserialization
// ─────────────────────────────────────────────────────────────────────────────

/// Parses a `LightRenderModuleConfig` from a JSON object.
///
/// Each field is independently validated so that one bad value does not
/// invalidate the entire config. Invalid fields fall back to the default.
LightRenderModuleConfig LightRenderModule::ParseConfig(const nlohmann::json& j) {
    LightRenderModuleConfig cfg; // Start from defaults.

    // -- radius ---------------------------------------------------------------
    if (j.contains("radius")) {
        const auto& val = j["radius"];
        if (val.is_number()) {
            const float r = val.get<float>();
            if (r > 0.0f) {
                cfg.radius = std::max(r, kMinRadius);
            } else {
                UZ_LOG(VFX, Warning,
                       "LightRenderModule: 'radius' must be > 0 (got {}), "
                       "using default {}",
                       r, cfg.radius);
            }
        } else {
            UZ_LOG(VFX, Warning,
                   "LightRenderModule: 'radius' is not a number, using default {}",
                   cfg.radius);
        }
    }

    // -- intensity ------------------------------------------------------------
    if (j.contains("intensity")) {
        const auto& val = j["intensity"];
        if (val.is_number()) {
            const float inten = val.get<float>();
            if (inten >= 0.0f) {
                cfg.intensity = inten;
            } else {
                UZ_LOG(VFX, Warning,
                       "LightRenderModule: 'intensity' must be >= 0 (got {}), "
                       "using default {}",
                       inten, cfg.intensity);
            }
        } else {
            UZ_LOG(VFX, Warning,
                   "LightRenderModule: 'intensity' is not a number, using default {}",
                   cfg.intensity);
        }
    }

    // -- cast_shadow ----------------------------------------------------------
    if (j.contains("cast_shadow")) {
        const auto& val = j["cast_shadow"];
        if (val.is_boolean()) {
            cfg.castShadow = val.get<bool>();
        } else {
            UZ_LOG(VFX, Warning,
                   "LightRenderModule: 'cast_shadow' is not a boolean, "
                   "using default {}",
                   cfg.castShadow);
        }
    }

    // -- max_lights -----------------------------------------------------------
    if (j.contains("max_lights")) {
        const auto& val = j["max_lights"];
        if (val.is_number_integer() || val.is_number_unsigned()) {
            const int64_t raw = val.get<int64_t>();
            if (raw < static_cast<int64_t>(kMaxLightsLowerBound)) {
                UZ_LOG(VFX, Warning,
                       "LightRenderModule: 'max_lights' must be >= {} (got {}), "
                       "clamping to minimum",
                       kMaxLightsLowerBound, raw);
                cfg.maxLights = kMaxLightsLowerBound;
            } else if (raw > static_cast<int64_t>(kMaxLightsUpperBound)) {
                UZ_LOG(VFX, Warning,
                       "LightRenderModule: 'max_lights' must be <= {} (got {}), "
                       "clamping to maximum",
                       kMaxLightsUpperBound, raw);
                cfg.maxLights = kMaxLightsUpperBound;
            } else {
                cfg.maxLights = static_cast<uint32_t>(raw);
            }
        } else {
            UZ_LOG(VFX, Warning,
                   "LightRenderModule: 'max_lights' is not an integer, "
                   "using default {}",
                   cfg.maxLights);
        }
    }

    return cfg;
}

} // namespace uz
