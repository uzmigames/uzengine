/// VFX Vector Field Module implementation -- 3D texture-driven velocity perturbation.
///
/// @ref Stam 1999 "Stable Fluids" -- vector field advection fundamentals
/// @ref Bridson 2007 "Fluid Simulation for Computer Graphics" -- advection,
///      boundary conditions, and velocity field sampling.

#include "vector_field_module.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Shader code -- HLSL snippet for 3D vector field velocity perturbation
// ---------------------------------------------------------------------------

// The snippet maps each particle's world-space position into the unit UVW
// space of the 3D texture. It decodes the stored direction (encoded as
// (dir * 0.5 + 0.5) in the texture) back to [-1, 1], scales by intensity,
// then either adds it as an acceleration force or blends it with the existing
// velocity depending on the tightness constant.
//
// Shader constants consumed (provided by the ModuleCompositor):
//   cbVectorFieldSrvIndex (uint)   -- bindless index into the 3D texture SRV heap
//   cbVectorFieldIntensity (float) -- force magnitude multiplier
//   cbVectorFieldTightness (float) -- 0=additive, 1=full velocity replace
//   cbVectorFieldBoundsX/Y/Z(float)-- world-space half-extents of the field box
//   cbVectorFieldOffsetX/Y/Z(float)-- world-space centre of the field box
//   cbVectorFieldTimeScale (float) -- UVW scroll speed for animated fields
//   cbTime (float)                 -- elapsed time (seconds)
//   cbDt (float)                   -- frame delta time (seconds)
//
// Standard particle UAVs accessed:
//   Positions[]  -- world-space particle positions (float3)
//   Velocities[] -- particle velocities (float3, read and written)
static constexpr const char* kVectorFieldShaderCode = R"hlsl(
// --- VectorFieldModule ---
{
    // Compute particle position relative to the field volume centre.
    float3 relPos = Positions[particleIdx] - float3(cbVectorFieldOffsetX,
                                                    cbVectorFieldOffsetY,
                                                    cbVectorFieldOffsetZ);

    // Map to normalised [0, 1] UVW: divide by half-extents to get [-1, 1],
    // then remap to [0, 1].
    float3 halfExtents = float3(cbVectorFieldBoundsX,
                                cbVectorFieldBoundsY,
                                cbVectorFieldBoundsZ);
    float3 uvw = (relPos / max(halfExtents, float3(0.001f, 0.001f, 0.001f)))
                 * 0.5f + 0.5f;

    // Only perturb particles whose UVW lands inside the [0, 1]^3 volume.
    // (Bounds check uses pre-scroll UVW to respect the world-space box.)
    if (all(uvw >= 0.0f) && all(uvw <= 1.0f))
    {
        // Animated vector field: scroll UVW by time offset (wrapping via frac).
        // When cbVectorFieldTimeScale == 0, this is a no-op (static field).
        if (cbVectorFieldTimeScale != 0.0f)
        {
            uvw = frac(uvw + float3(0.0f, 0.0f, cbVectorFieldTimeScale * cbTime));
        }
        // Sample the 3D field texture (Texture3D bound in bindless SRV heap).
        // The field encodes direction as (dir * 0.5 + 0.5); decode to [-1, 1].
        Texture3D<float4> fieldTex =
            ResourceDescriptorHeap[cbVectorFieldSrvIndex];
        float3 fieldEncoded = fieldTex.SampleLevel(linearSampler, uvw, 0).xyz;
        float3 fieldDir = fieldEncoded * 2.0f - 1.0f;

        // Apply intensity scaling.
        float3 fieldVel = fieldDir * cbVectorFieldIntensity;

        // Blend between additive force and full velocity replacement.
        // tightness == 0 → additive acceleration: vel += field * dt
        // tightness > 0  → lerp(vel, field, tightness * dt)
        if (cbVectorFieldTightness > 0.0f)
        {
            float blendFactor = saturate(cbVectorFieldTightness * cbDt);
            Velocities[particleIdx] = lerp(Velocities[particleIdx],
                                           fieldVel,
                                           blendFactor);
        }
        else
        {
            // Pure additive mode: treat field as a per-second acceleration.
            Velocities[particleIdx] += fieldVel * cbDt;
        }
    }
}
)hlsl";

// ---------------------------------------------------------------------------
// IVFXModule interface
// ---------------------------------------------------------------------------

/// Returns the module's unique name used for registration and serialization.
const char* VectorFieldModule::GetName() const noexcept {
    return "vector_field";
}

/// Returns the HLSL snippet for 3D vector field velocity perturbation.
///
/// The snippet expects the following constants bound by the ModuleCompositor:
/// - cbVectorFieldSrvIndex  (uint)   — bindless SRV index of the Texture3D.
/// - cbVectorFieldIntensity (float)  — force magnitude multiplier.
/// - cbVectorFieldTightness (float)  — blend factor (0=additive, 1=replace).
/// - cbVectorFieldBoundsX/Y/Z (float)— half-extents of the field box.
/// - cbVectorFieldOffsetX/Y/Z (float)— centre of the field box.
/// - cbDt                   (float)  — frame delta time in seconds.
///
/// Standard particle UAVs accessed: Positions[], Velocities[].
std::string_view VectorFieldModule::GetShaderCode() const {
    return kVectorFieldShaderCode;
}

/// Declares the five configurable parameters for the vector field module.
///
/// Parameter order defines the constant buffer slot assignment:
///   0 = field_texture  (Int  — bindless SRV index, UINT32_MAX when invalid)
///   1 = intensity      (Float)
///   2 = tightness      (Float)
///   3 = bounds         (Vec3)
///   4 = field_offset   (Vec3)
///   5 = time_scale     (Float — UVW scroll speed for animated fields)
std::vector<ParameterDef> VectorFieldModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(6);

    // field_texture: stored as a uint32 SRV index in the bindless heap.
    // UINT32_MAX (-1 when cast to int32) signals "no texture bound".
    {
        ParameterDef p;
        p.name       = "field_texture";
        p.type       = ParameterType::Int;
        p.defaultInt = fieldTexture.IsValid()
                           ? static_cast<int32_t>(fieldTexture.index)
                           : static_cast<int32_t>(~uint32_t{0});
        params.push_back(p);
    }

    {
        ParameterDef p;
        p.name         = "intensity";
        p.type         = ParameterType::Float;
        p.defaultFloat = intensity;
        params.push_back(p);
    }

    {
        ParameterDef p;
        p.name         = "tightness";
        p.type         = ParameterType::Float;
        p.defaultFloat = tightness;
        params.push_back(p);
    }

    {
        ParameterDef p;
        p.name        = "bounds";
        p.type        = ParameterType::Vec3;
        p.defaultVec3 = bounds;
        params.push_back(p);
    }

    {
        ParameterDef p;
        p.name        = "field_offset";
        p.type        = ParameterType::Vec3;
        p.defaultVec3 = fieldOffset;
        params.push_back(p);
    }

    {
        ParameterDef p;
        p.name         = "time_scale";
        p.type         = ParameterType::Float;
        p.defaultFloat = timeScale;
        params.push_back(p);
    }

    return params;
}

/// No-op on CPU side. The vector field compute shader evaluates UVW mapping,
/// texture sampling, and velocity integration entirely on the GPU each dispatch.
void VectorFieldModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- nothing to do on the CPU side.
}

} // namespace uz
