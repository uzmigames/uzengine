/// VFX Spawn Module implementation -- particle emission with shape-based positioning.

#include "spawn_module.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Shader code -- HLSL snippet for particle spawn initialization
// ---------------------------------------------------------------------------

static constexpr const char* kSpawnShaderCode = R"hlsl(
// --- SpawnModule ---
uint freeIdx = 0;
InterlockedAdd(FreeListHead[0], -1, freeIdx);
if (freeIdx > 0) {
    uint slotIdx = FreeList[freeIdx - 1];
    uint seed = VFXSeed(slotIdx, cbFrameNumber);
    // Position based on spawn shape
    float3 spawnPos = cbEmitterPosition;
    // [SHAPE_POINT]: no offset
    // [SHAPE_SPHERE]: spawnPos += RandomInSphere(seed, cbSpawnRadius);
    // [SHAPE_BOX]: spawnPos += float3(RandomFloat(seed,-cbBoxExtents.x,cbBoxExtents.x), RandomFloat(seed,-cbBoxExtents.y,cbBoxExtents.y), RandomFloat(seed,-cbBoxExtents.z,cbBoxExtents.z));
    Positions[slotIdx] = spawnPos;
    Velocities[slotIdx] = RandomInCone(seed, cbSpawnVelocity, cbSpreadAngle) + float3(RandomFloat(seed,-cbVelVariation,cbVelVariation), RandomFloat(seed,-cbVelVariation,cbVelVariation), RandomFloat(seed,-cbVelVariation,cbVelVariation));
    Ages[slotIdx] = 0.0f;
    Lifespans[slotIdx] = RandomFloat(seed, cbLifespanMin, cbLifespanMax);
    Sizes[slotIdx] = cbInitialSize;
    Colors[slotIdx] = cbInitialColor;
    Rotations[slotIdx] = RandomFloat(seed, 0.0f, 6.28318f);
}
)hlsl";

// ---------------------------------------------------------------------------
// IVFXModule interface
// ---------------------------------------------------------------------------

/// Returns the module's unique name for registration and serialization.
const char* SpawnModule::GetName() const noexcept {
    return "spawn";
}

/// Returns the HLSL snippet that the ModuleCompositor stitches into the
/// spawn compute kernel. The snippet expects standard UAV bindings
/// (Positions, Velocities, Ages, etc.) and spawn-specific cbuffer constants.
std::string_view SpawnModule::GetShaderCode() const {
    return kSpawnShaderCode;
}

/// Declares all configurable parameters for editor UI and serialization.
/// Parameter order defines the constant buffer layout consumed by the shader.
std::vector<ParameterDef> SpawnModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(7);

    {
        ParameterDef p;
        p.name = "rate";
        p.type = ParameterType::Float;
        p.defaultFloat = rate;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "lifespan_min";
        p.type = ParameterType::Float;
        p.defaultFloat = lifespanMin;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "lifespan_max";
        p.type = ParameterType::Float;
        p.defaultFloat = lifespanMax;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "velocity";
        p.type = ParameterType::Vec3;
        p.defaultVec3 = velocity;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "velocity_variation";
        p.type = ParameterType::Float;
        p.defaultFloat = velocityVariation;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "spread_angle";
        p.type = ParameterType::Float;
        p.defaultFloat = spreadAngle;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "spawn_radius";
        p.type = ParameterType::Float;
        p.defaultFloat = spawnRadius;
        params.push_back(p);
    }

    return params;
}

/// No-op on CPU side. The spawn compute shader handles particle allocation
/// and initialization entirely on the GPU via atomic free-list operations.
void SpawnModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- nothing to do on the CPU side.
}

} // namespace uz
