/// VFX Orbit Module implementation -- circular orbital motion around a point.

#include "orbit_module.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Shader code -- HLSL snippet for orbital velocity application
// ---------------------------------------------------------------------------

/// HLSL snippet stitched by ModuleCompositor into the VFX update compute shader.
///
/// Constant buffer bindings expected by this snippet (uploaded by the compositor):
///   float3 cbOrbitCenter  -- world-space orbit center
///   float  cbOrbitSpeed   -- angular speed in radians per second
///   float  cbOrbitRadius  -- desired orbit radius
///   float3 cbOrbitAxis    -- normalised orbit axis
///   float  cbDt           -- frame delta time (seconds)
///
/// Guard threshold 1e-6 prevents NaN from normalise() on zero-length vectors.
/// All computation is scoped in a braced block to prevent name collisions
/// with other module snippets in the same compilation unit.
static constexpr const char* kOrbitShaderCode = R"hlsl(
{
    // --- OrbitModule ---
    // Vector from orbit center to particle position.
    float3 orbitToParticle = Positions[particleIdx] - cbOrbitCenter;

    // Normalise the orbit axis defensively (editor may supply non-unit vectors).
    float3 orbitAxisN = cbOrbitAxis;
    float  axisLen    = length(orbitAxisN);
    if (axisLen > 1e-6f) { orbitAxisN /= axisLen; }

    // Project toParticle onto the orbit plane (remove the axis component).
    float  axialDist = dot(orbitToParticle, orbitAxisN);
    float3 planeVec  = orbitToParticle - axialDist * orbitAxisN;

    float planeLen = length(planeVec);
    if (planeLen > 1e-6f)
    {
        float3 radialDir = planeVec / planeLen;

        // Tangential direction: perpendicular to both axis and radial direction.
        // This drives the circular motion.
        float3 tangent = cross(orbitAxisN, radialDir);

        // 1. Tangential impulse -- constant angular velocity.
        Velocities[particleIdx] += tangent * cbOrbitSpeed * cbDt;

        // 2. Radial spring -- pulls toward desired orbit radius.
        float radialForce = (cbOrbitRadius - planeLen) * cbOrbitSpeed;
        Velocities[particleIdx] += radialDir * radialForce * cbDt;
    }
}
)hlsl";

// ---------------------------------------------------------------------------
// IVFXModule interface
// ---------------------------------------------------------------------------

/// Returns the module's unique stable identifier used for registration
/// and serialized asset references.
const char* OrbitModule::GetName() const noexcept {
    return "orbit";
}

/// Returns the HLSL snippet that steers particles into a circular orbit.
/// Expects cbOrbitCenter (float3), cbOrbitSpeed (float), cbOrbitRadius (float),
/// cbOrbitAxis (float3), and cbDt (float) in the active constant buffer.
std::string_view OrbitModule::GetShaderCode() const {
    return kOrbitShaderCode;
}

/// Declares the four orbit parameters for editor UI and constant buffer layout.
/// Parameter order defines constant buffer field order -- do not reorder.
std::vector<ParameterDef> OrbitModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(4);

    {
        ParameterDef p;
        p.name       = "center";
        p.type       = ParameterType::Vec3;
        p.defaultVec3 = center;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "speed";
        p.type         = ParameterType::Float;
        p.defaultFloat = speed;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "orbit_radius";
        p.type         = ParameterType::Float;
        p.defaultFloat = orbitRadius;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name        = "axis";
        p.type        = ParameterType::Vec3;
        p.defaultVec3 = axis;
        params.push_back(p);
    }

    return params;
}

/// No-op on the CPU side. The orbit compute shader applies both the tangential
/// impulse and the radial spring force entirely on the GPU each dispatch.
void OrbitModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- nothing to do on the CPU side.
}

} // namespace uz
