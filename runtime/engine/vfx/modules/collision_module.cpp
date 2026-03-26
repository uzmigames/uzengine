/// VFX Collision Module implementation -- depth-buffer particle collision.

#include "collision_module.h"

#include "engine/logging/log.h"

#include <algorithm>
#include <cctype>
#include <json.hpp>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// CollisionResponse string conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Maps each CollisionResponse to its canonical lowercase string name.
/// "unknown" is returned for any value outside the defined enum range.
const char* CollisionResponseToString(CollisionResponse response) noexcept {
    switch (response) {
        case CollisionResponse::Kill:   return "kill";
        case CollisionResponse::Bounce: return "bounce";
        case CollisionResponse::Slide:  return "slide";
    }
    return "unknown";
}

/// Parses a collision response name case-insensitively.
/// Falls back to Bounce and logs a warning for unrecognized strings so that
/// a typo in a VFX asset never silently silences all collision response.
CollisionResponse CollisionResponseFromString(const std::string& str) {
    // Normalize to lowercase for case-insensitive matching.
    std::string lower;
    lower.reserve(str.size());
    for (const char c : str) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (lower == "kill")   { return CollisionResponse::Kill;   }
    if (lower == "bounce") { return CollisionResponse::Bounce; }
    if (lower == "slide")  { return CollisionResponse::Slide;  }

    UZ_LOG(VFX, Warning,
           "CollisionResponseFromString: unrecognized value '{}', defaulting to 'bounce'",
           str);
    return CollisionResponse::Bounce;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader code -- HLSL snippet for depth-buffer collision
// ─────────────────────────────────────────────────────────────────────────────

// The snippet is included by ModuleCompositor inside the particle update compute
// shader body. It relies on:
//   - Positions[], Velocities[], Ages[], Lifespans[] (SoA particle UAVs)
//   - cbViewProj, cbInvViewProj, cbScreenDimensions (per-frame camera constants)
//   - cbCollisionEnabled, cbCollisionResponse, cbBounceFactor,
//     cbMinBounceSpeed, cbCollisionOffset (collision constants from CollisionModule)
//   - DepthBuffer (Texture2D<float>), LinearClampSampler
//   - ScreenSpaceCollision(), EstimateSurfaceNormal(), ReflectVelocity(),
//     ProjectOntoPlane() from vfx_collision.hlsli
//
// Response enum values are baked into the constant buffer as uint:
//   0 = Kill, 1 = Bounce, 2 = Slide.

static constexpr const char* kCollisionShaderCode = R"hlsl(
// --- CollisionModule ---
if (cbCollisionEnabled != 0u) {
    float3 particlePos = Positions[particleIdx];
    float3 particleVel = Velocities[particleIdx];

    float3 surfaceNormal = float3(0.0, 1.0, 0.0); // fallback: world up
    bool hit = ScreenSpaceCollision(
        particlePos,
        cbViewProj,
        DepthBuffer,
        LinearClampSampler,
        cbScreenDimensions,
        cbCollisionOffset,
        surfaceNormal
    );

    if (hit) {
        // cbCollisionResponse: 0=Kill, 1=Bounce, 2=Slide
        if (cbCollisionResponse == 0u) {
            // Kill: set age to lifespan so DecayModule recycles this particle.
            Ages[particleIdx] = Lifespans[particleIdx];
        } else if (cbCollisionResponse == 1u) {
            // Bounce: reflect velocity off the surface normal with energy loss.
            float speed = length(particleVel);
            if (speed < cbMinBounceSpeed) {
                // Particle is too slow to bounce meaningfully -- kill it instead
                // of leaving it jittering on the surface indefinitely.
                Ages[particleIdx] = Lifespans[particleIdx];
            } else {
                Velocities[particleIdx] = ReflectVelocity(particleVel, surfaceNormal, cbBounceFactor);
                // Nudge the particle above the surface to prevent re-collision
                // on the very next frame.
                Positions[particleIdx] = particlePos + surfaceNormal * cbCollisionOffset;
            }
        } else {
            // Slide: project velocity onto the surface plane so the particle
            // glides along the surface without penetrating it.
            Velocities[particleIdx] = ProjectOntoPlane(particleVel, surfaceNormal);
            Positions[particleIdx] = particlePos + surfaceNormal * cbCollisionOffset;
        }
    }
}
)hlsl";

// ─────────────────────────────────────────────────────────────────────────────
// CollisionModule
// ─────────────────────────────────────────────────────────────────────────────

/// Default-constructs with `CollisionModuleConfig` defaults -- required for
/// `ModuleRegistry::Register<CollisionModule>()`.
CollisionModule::CollisionModule()
    : CollisionModule(CollisionModuleConfig{})
{}

/// Stores the configuration. The shader code string is static so it requires
/// no per-instance allocation.
CollisionModule::CollisionModule(const CollisionModuleConfig& config)
    : config_(config)
{}

// -- IVFXModule interface -----------------------------------------------------

/// Returns the unique registration name. Stable -- changing this breaks saved assets.
const char* CollisionModule::GetName() const noexcept {
    return "collision";
}

/// Returns the HLSL snippet. Points to a static constexpr buffer; caller must
/// not store the view beyond the program's lifetime.
std::string_view CollisionModule::GetShaderCode() const {
    return kCollisionShaderCode;
}

/// Declares the five configurable collision parameters for editor UI and
/// constant buffer serialization. The order here determines the cbuffer layout.
std::vector<ParameterDef> CollisionModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(5);

    {
        ParameterDef p;
        p.name       = "response";
        p.type       = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(config_.response);
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "bounce_factor";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.bounceFactor;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name        = "depth_buffer_collision";
        p.type        = ParameterType::Bool;
        p.defaultBool = config_.depthBufferCollision;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "collision_offset";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.collisionOffset;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "min_bounce_speed";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.minBounceSpeed;
        params.push_back(p);
    }

    return params;
}

/// No CPU-side work required. The ModuleCompositor is responsible for uploading
/// the collision constant buffer values derived from `config_` each frame.
void CollisionModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- nothing to do on the CPU side.
}

// -- Configuration access -----------------------------------------------------

/// Returns the configuration stored at construction time.
const CollisionModuleConfig& CollisionModule::GetConfig() const noexcept {
    return config_;
}

// -- JSON deserialization -----------------------------------------------------

/// Parses a CollisionModuleConfig from a json object.
/// All fields are optional -- absent keys retain their default values.
CollisionModuleConfig CollisionModule::ParseConfig(const nlohmann::json& j) {
    CollisionModuleConfig cfg;

    if (!j.is_object()) {
        UZ_LOG(VFX, Warning,
               "CollisionModule::ParseConfig: expected a JSON object, got {}; "
               "using defaults",
               j.type_name());
        return cfg;
    }

    if (j.contains("response") && j["response"].is_string()) {
        cfg.response = CollisionResponseFromString(j["response"].get<std::string>());
    }

    if (j.contains("bounce_factor") && j["bounce_factor"].is_number()) {
        const float raw = j["bounce_factor"].get<float>();
        cfg.bounceFactor = std::clamp(raw, 0.0f, 1.0f);
        if (raw != cfg.bounceFactor) {
            UZ_LOG(VFX, Warning,
                   "CollisionModule::ParseConfig: bounce_factor {} clamped to [0,1]",
                   raw);
        }
    }

    if (j.contains("depth_buffer_collision") && j["depth_buffer_collision"].is_boolean()) {
        cfg.depthBufferCollision = j["depth_buffer_collision"].get<bool>();
    }

    if (j.contains("collision_offset") && j["collision_offset"].is_number()) {
        const float raw = j["collision_offset"].get<float>();
        if (raw < 0.0f) {
            UZ_LOG(VFX, Warning,
                   "CollisionModule::ParseConfig: collision_offset {} is negative; "
                   "clamping to 0.0",
                   raw);
        }
        cfg.collisionOffset = std::max(0.0f, raw);
    }

    if (j.contains("min_bounce_speed") && j["min_bounce_speed"].is_number()) {
        const float raw = j["min_bounce_speed"].get<float>();
        if (raw < 0.0f) {
            UZ_LOG(VFX, Warning,
                   "CollisionModule::ParseConfig: min_bounce_speed {} is negative; "
                   "clamping to 0.0",
                   raw);
        }
        cfg.minBounceSpeed = std::max(0.0f, raw);
    }

    return cfg;
}

} // namespace uz
