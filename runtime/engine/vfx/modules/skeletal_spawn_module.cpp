/// VFX Skeletal Spawn Module implementation -- spawn particles from bone positions.

#include "skeletal_spawn_module.h"

#include "engine/logging/log.h"

#include <algorithm>
#include <json.hpp>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Shader code -- HLSL snippet for bone-based spawn position
// ---------------------------------------------------------------------------

// The snippet is included by ModuleCompositor inside the particle spawn compute
// shader body. It replaces the default emitter position with a bone position
// plus a random offset within spawnRadius.
//
// Bone selection uses a Knuth multiplicative hash (2654435761 = golden ratio *
// 2^32) on the particle seed to distribute particles across bones. For small
// bone counts (<200), the modulo bias is under 0.1% and visually undetectable.

static constexpr const char* kSkeletalSpawnShaderCode = R"hlsl(
// --- SkeletalSpawnModule ---
#ifdef USE_SKELETAL_SPAWN
{
    if (cbBoneCount > 0u) {
        // Select a bone using a hash of the particle seed for pseudo-random
        // distribution. The constant 2654435761 is the golden ratio scaled to
        // 32-bit integer range, which produces excellent avalanche behavior.
        uint boneIdx = (particleSeed * 2654435761u) % cbBoneCount;
        float4x4 boneMat = g_boneTransforms[boneIdx];

        // Extract translation from the 4th column (column-major float4x4).
        float3 bonePos = float3(boneMat[0][3], boneMat[1][3], boneMat[2][3]);

        // Random offset within a sphere of radius cbSpawnRadius.
        float3 offset = (RandomVec3(particleSeed) * 2.0 - 1.0) * cbSpawnRadius;
        float3 spawnPos = bonePos + offset;

        // Override the particle's initial position.
        Positions[newIndex] = float4(spawnPos, 0.0);
    }
}
#endif
)hlsl";

// ---------------------------------------------------------------------------
// SkeletalSpawnModule
// ---------------------------------------------------------------------------

SkeletalSpawnModule::SkeletalSpawnModule()
    : SkeletalSpawnModule(SkeletalSpawnModuleConfig{})
{}

SkeletalSpawnModule::SkeletalSpawnModule(const SkeletalSpawnModuleConfig& config)
    : config_(config)
{}

// -- IVFXModule interface -----------------------------------------------------

const char* SkeletalSpawnModule::GetName() const noexcept {
    return "skeletal_spawn";
}

std::string_view SkeletalSpawnModule::GetShaderCode() const {
    return kSkeletalSpawnShaderCode;
}

std::vector<ParameterDef> SkeletalSpawnModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(3);

    {
        ParameterDef p;
        p.name       = "skeletal_mesh_entity_id";
        p.type       = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(config_.skeletalMeshEntityId);
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "spawn_radius";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.spawnRadius;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name        = "per_bone_weighted";
        p.type        = ParameterType::Bool;
        p.defaultBool = config_.perBoneWeighted;
        params.push_back(p);
    }

    return params;
}

void SkeletalSpawnModule::ApplyJsonConfig(const nlohmann::json& config) {
    config_ = ParseConfig(config);
}

void SkeletalSpawnModule::Execute(const ExecuteContext& /*ctx*/) {
    // Query bone data from the provider. If no provider is set or the skeleton
    // has no bones, the GPU buffer remains unmodified and the HLSL guard
    // (cbBoneCount == 0) prevents any bone-based spawn logic.
    if (!provider_) {
        return;
    }

    const uint32_t newBoneCount = provider_->GetBoneCount();
    const float* transformData = provider_->GetBoneTransformsData();

    if (newBoneCount == 0 || transformData == nullptr) {
        boneCount_ = 0;
        return;
    }

    // Track the bone count for the GPU constant buffer.
    // Actual GPU buffer allocation and upload is deferred to the render graph
    // integration layer (VFXSystem), which has access to the RHI device.
    // This module stores the bone count so GetBoneCount() returns the correct
    // value for the CB upload.
    boneCount_ = newBoneCount;
}

// -- Configuration & provider access ------------------------------------------

const SkeletalSpawnModuleConfig& SkeletalSpawnModule::GetConfig() const noexcept {
    return config_;
}

void SkeletalSpawnModule::SetProvider(ISkeletalMeshProvider* provider) noexcept {
    provider_ = provider;
}

ISkeletalMeshProvider* SkeletalSpawnModule::GetProvider() const noexcept {
    return provider_;
}

RHIBufferHandle SkeletalSpawnModule::GetBoneTransformBuffer() const noexcept {
    return boneTransformBuffer_;
}

uint32_t SkeletalSpawnModule::GetBoneCount() const noexcept {
    return boneCount_;
}

// -- JSON deserialization -----------------------------------------------------

SkeletalSpawnModuleConfig SkeletalSpawnModule::ParseConfig(const nlohmann::json& j) {
    SkeletalSpawnModuleConfig cfg;

    if (!j.is_object()) {
        UZ_LOG(VFX, Warning,
               "SkeletalSpawnModule::ParseConfig: expected JSON object, got {}; "
               "using defaults",
               j.type_name());
        return cfg;
    }

    if (j.contains("skeletal_mesh_entity_id") && j["skeletal_mesh_entity_id"].is_number_integer()) {
        cfg.skeletalMeshEntityId = j["skeletal_mesh_entity_id"].get<uint32_t>();
    }

    if (j.contains("spawn_radius") && j["spawn_radius"].is_number()) {
        const float raw = j["spawn_radius"].get<float>();
        if (raw < 0.0f) {
            UZ_LOG(VFX, Warning,
                   "SkeletalSpawnModule::ParseConfig: spawn_radius {} is negative; "
                   "clamping to 0.0",
                   raw);
        }
        cfg.spawnRadius = std::max(0.0f, raw);
    }

    if (j.contains("per_bone_weighted") && j["per_bone_weighted"].is_boolean()) {
        cfg.perBoneWeighted = j["per_bone_weighted"].get<bool>();
    }

    return cfg;
}

} // namespace uz
