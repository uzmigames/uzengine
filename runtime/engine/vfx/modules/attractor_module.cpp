/// VFX Attractor Module implementation -- multi-attractor velocity force.

#include "attractor_module.h"

#include "engine/rhi/rhi.h"
#include "engine/assertions/engine_assert.h"
#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Shader code -- HLSL snippet for multi-attractor force
// ---------------------------------------------------------------------------

// The snippet iterates over all entries in AttractorsBuf (a StructuredBuffer
// bound by the compositor) up to cbAttractorCount. Each entry's falloffType
// field matches AttractorFalloff: 0=Constant, 1=Linear, 2=InverseSquare.
static constexpr const char* kAttractorShaderCode = R"hlsl(
// --- AttractorModule ---
{
    for (uint attIdx = 0u; attIdx < cbAttractorCount; ++attIdx)
    {
        AttractorParams att = AttractorsBuf[attIdx];

        float3 toAttractor = att.position - Positions[particleIdx];
        float dist = length(toAttractor);
        float3 norm = toAttractor / max(dist, 0.001f);

        float force = 0.0f;
        if (att.falloffType == 0u) {
            // Constant: uniform magnitude, independent of distance.
            force = att.strength;
        } else if (att.falloffType == 1u) {
            // Linear: force decays linearly to zero at att.radius.
            force = att.strength * saturate(1.0f - dist / max(att.radius, 0.001f));
        } else {
            // InverseSquare: physically based 1/d^2 falloff.
            // kAttractorMinRadiusSq prevents division by zero near the attractor.
            force = att.strength / max(dist * dist, kAttractorMinRadiusSq);
        }

        Velocities[particleIdx] += norm * force * cbDt;

        // Kill on arrival: expire particle when it reaches the attractor point.
        if (att.killOnArrival && dist < att.killDistance) {
            Ages[particleIdx] = Lifespans[particleIdx];
        }
    }
}
)hlsl";

// ---------------------------------------------------------------------------
// IVFXModule interface
// ---------------------------------------------------------------------------

/// Returns the module's unique name used for registration and serialization.
const char* AttractorModule::GetName() const noexcept {
    return "attractor";
}

/// Returns the HLSL snippet for multi-attractor force evaluation.
///
/// Expects the following resources (provided by the compositor):
/// - AttractorsBuf (StructuredBuffer<AttractorParams>) -- per-emitter attractor list
/// - cbAttractorCount (uint)      -- number of active attractors in AttractorsBuf
/// - kAttractorMinRadiusSq (float constant) -- kMinRadius^2 (prevents div-by-zero)
/// - cbDt (float)                 -- frame delta time
///
/// The snippet also accesses standard particle UAVs:
/// Positions[], Velocities[], Ages[], Lifespans[].
std::string_view AttractorModule::GetShaderCode() const {
    return kAttractorShaderCode;
}

/// Declares the five configurable attractor parameters for the primary attractor.
///
/// These parameters describe the primary attractor (slot 0 in AttractorsBuf).
/// Order determines the constant buffer slot assignment:
/// 0 = attractor_position, 1 = strength, 2 = radius,
/// 3 = falloff, 4 = kill_on_arrival.
std::vector<ParameterDef> AttractorModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(5);

    {
        ParameterDef p;
        p.name       = "attractor_position";
        p.type       = ParameterType::Vec3;
        p.defaultVec3 = attractorPosition;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "strength";
        p.type         = ParameterType::Float;
        p.defaultFloat = strength;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "radius";
        p.type         = ParameterType::Float;
        p.defaultFloat = radius;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name       = "falloff";
        p.type       = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(falloff);
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name        = "kill_on_arrival";
        p.type        = ParameterType::Bool;
        p.defaultBool = killOnArrival;
        params.push_back(p);
    }

    return params;
}

/// No-op on CPU side. The attractor compute shader evaluates direction,
/// falloff, and velocity integration entirely on the GPU each dispatch.
void AttractorModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- nothing to do on the CPU side.
}

// ---------------------------------------------------------------------------
// UploadAttractors — build and upload per-frame attractor data
// ---------------------------------------------------------------------------

/// Builds the primary attractor entry from the module's own fields and uploads
/// the full attractor list to the GPU structured buffer.
///
/// Clears the buffer, inserts the module's own primary attractor at slot 0,
/// then calls UploadToGPU(). This is the standard single-attractor path.
///
/// For multi-attractor use, populate the extra entries after this call returns
/// by calling attractorBuffer.AddAttractor() followed by attractorBuffer.UploadToGPU():
/// ```cpp
///   module.UploadAttractors(device);                   // slot 0 = primary
///   module.attractorBuffer.AddAttractor(secondary);    // slot 1
///   module.attractorBuffer.UploadToGPU(device);        // re-upload with extras
/// ```
///
/// # Parameters
/// - `device`: RHI device providing MapBuffer/UnmapBuffer. Must not be null.
void AttractorModule::UploadAttractors(IRHIDevice* device)
{
    UZ_CHECKF(device != nullptr, "AttractorModule::UploadAttractors: device is null");
    UZ_CHECKF(attractorBuffer.IsValid(),
              "AttractorModule::UploadAttractors: attractorBuffer not created");

    attractorBuffer.Clear();

    // Register the module's own fields as the primary (slot 0) attractor.
    AttractorData primary{};
    primary.positionX     = attractorPosition.x;
    primary.positionY     = attractorPosition.y;
    primary.positionZ     = attractorPosition.z;
    primary.strength      = strength;
    primary.radius        = radius;
    primary.falloffType   = static_cast<uint32_t>(falloff);
    primary.killOnArrival = killOnArrival ? 1u : 0u;
    primary.killDistance  = kKillDistance;

    attractorBuffer.AddAttractor(primary);

    attractorBuffer.UploadToGPU(device);
}

} // namespace uz
