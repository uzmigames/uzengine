#pragma once

/// VFX Skeletal Spawn Module -- spawn particles from skeletal mesh bone positions.
///
/// Enables spawning particles at the world-space positions of bones in a
/// skeletal mesh. Each frame the module queries bone transforms from an
/// `ISkeletalMeshProvider` interface, uploads them to a GPU StructuredBuffer,
/// and emits an HLSL snippet that selects a bone and offsets the spawn position.
///
/// The bone selection uses a hash-based pseudo-random approach per particle:
///   `boneIdx = (particleSeed * 2654435761u) % boneCount`
/// This produces acceptable uniformity for small bone counts (typically <200)
/// where modulo bias is negligible.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.
///
/// @ref Epic 2019 "Niagara VFX System" -- skeletal mesh location DI pattern

#include "vfx_module.h"

#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <string>
#include <json.hpp>

namespace uz {

// ---------------------------------------------------------------------------
// ISkeletalMeshProvider -- query interface for bone transform data
// ---------------------------------------------------------------------------

/// Abstract interface for providing world-space bone transforms to the VFX system.
///
/// Concrete implementations may be a full skeletal mesh component (future) or a
/// synthetic test provider (demo). The VFX system queries this each frame to
/// upload bone data to the GPU.
///
/// # Lifetime
/// The provider must outlive any `SkeletalSpawnModule` that references it.
/// The module stores a non-owning pointer.
///
/// # Thread Safety
/// Implementations need not be thread-safe -- called only from the main/render thread.
class ISkeletalMeshProvider {
public:
    virtual ~ISkeletalMeshProvider() = default;

    /// Returns world-space bone transforms as column-major float4x4 matrices.
    ///
    /// The returned pointer is valid until the next frame (or until the provider
    /// is destroyed). Returns nullptr if the skeleton is not ready or has no bones.
    ///
    /// # Returns
    /// - Pointer to `GetBoneCount() * 16` floats (4x4 matrix per bone), or nullptr.
    [[nodiscard]] virtual const float* GetBoneTransformsData() const noexcept = 0;

    /// Returns the number of bones in the skeleton.
    ///
    /// # Returns
    /// Bone count (0 if skeleton is not ready).
    [[nodiscard]] virtual uint32_t GetBoneCount() const noexcept = 0;
};

// ---------------------------------------------------------------------------
// SkeletalSpawnModuleConfig -- configuration for bone-based particle spawning
// ---------------------------------------------------------------------------

/// Configuration for `SkeletalSpawnModule`.
///
/// | Field               | Default | Description                                |
/// |---------------------|---------|--------------------------------------------|
/// | skeletalMeshEntityId | 0      | ECS entity owning the skeletal mesh         |
/// | spawnRadius          | 5.0    | Random offset from bone position (cm)       |
/// | perBoneWeighted      | true   | Weight bone selection by bone length (future)|
///
/// @ref Epic 2019 "Niagara VFX System" -- skeletal mesh spawn parameters
struct SkeletalSpawnModuleConfig {
    /// ECS entity ID that owns the skeletal mesh component.
    /// Used for entity lookup during Update() when no provider is set directly.
    uint32_t skeletalMeshEntityId = 0;

    /// Random positional offset radius from the bone position (centimeters).
    /// Each spawned particle is displaced by a random vector within this sphere.
    float spawnRadius = 5.0f;

    /// When true, bone selection is weighted by bone length so that longer
    /// bones (e.g., femur) spawn proportionally more particles than short ones
    /// (e.g., finger joints). Currently stored for future use -- the HLSL
    /// snippet uses uniform random selection regardless of this flag.
    bool perBoneWeighted = true;
};

// ---------------------------------------------------------------------------
// SkeletalSpawnModule -- spawn particles from bone positions
// ---------------------------------------------------------------------------

/// VFX module that spawns particles at skeletal mesh bone positions.
///
/// Implements `IVFXModule`. The HLSL snippet selects a random bone from
/// `g_boneTransforms`, extracts its translation, and applies a random offset
/// within `spawnRadius` to produce the particle's initial position.
///
/// # Setup
/// Set the bone data provider via `SetProvider()`. If no provider is set,
/// the module silently produces no spawn position override (the default
/// emitter position is used instead).
///
/// # HLSL Dependencies
/// The snippet assumes these names are accessible:
/// - `g_boneTransforms`  : `StructuredBuffer<float4x4>` at register(t9)
/// - `cbBoneCount`       : `uint` in SkeletalSpawnCB at register(b5)
/// - `cbSpawnRadius`     : `float` in SkeletalSpawnCB
/// - `cbUseWeightedBones`: `uint` in SkeletalSpawnCB
/// - `particleSeed`      : `uint` per-particle random seed
/// - `RandomVec3(seed)`  : helper returning float3 in [0,1]^3
///
/// # Example
/// ```cpp
/// SkeletalSpawnModuleConfig cfg;
/// cfg.spawnRadius = 8.0f;
/// auto module = std::make_unique<SkeletalSpawnModule>(cfg);
/// module->SetProvider(&mySkeleton);
/// stack.Push(std::move(module));
/// ```
class SkeletalSpawnModule final : public IVFXModule {
public:
    /// Default-constructs with `SkeletalSpawnModuleConfig` defaults.
    SkeletalSpawnModule();

    /// Constructs with the given configuration.
    ///
    /// # Parameters
    /// - `config`: Spawn parameters. Copied into the module.
    explicit SkeletalSpawnModule(const SkeletalSpawnModuleConfig& config);

    ~SkeletalSpawnModule() override = default;

    // -- IVFXModule interface -------------------------------------------------

    /// Returns `"skeletal_spawn"`.
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns the HLSL snippet for bone-based spawn position override.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns parameter definitions for skeletal spawn configuration.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// Applies JSON configuration to module fields.
    void ApplyJsonConfig(const nlohmann::json& config) override;

    /// CPU-side execution: uploads bone transforms to the GPU buffer.
    ///
    /// Queries the provider for bone data, reallocates the GPU buffer if
    /// bone count changed, and uploads the transform matrices.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration & provider access --------------------------------------

    /// Returns the current configuration.
    [[nodiscard]] const SkeletalSpawnModuleConfig& GetConfig() const noexcept;

    /// Sets the bone data provider (non-owning pointer).
    ///
    /// The provider must outlive this module. Pass nullptr to disconnect.
    void SetProvider(ISkeletalMeshProvider* provider) noexcept;

    /// Returns the current bone data provider, or nullptr if none is set.
    [[nodiscard]] ISkeletalMeshProvider* GetProvider() const noexcept;

    /// Returns the RHI handle of the bone transform GPU buffer.
    /// Invalid if no bones have been uploaded yet.
    [[nodiscard]] RHIBufferHandle GetBoneTransformBuffer() const noexcept;

    /// Returns the number of bones currently uploaded to the GPU.
    [[nodiscard]] uint32_t GetBoneCount() const noexcept;

    // -- JSON deserialization -------------------------------------------------

    /// Parses a `SkeletalSpawnModuleConfig` from a JSON object.
    ///
    /// Accepted keys (all optional):
    /// | Key                        | Type    | Description                 |
    /// |----------------------------|---------|-----------------------------|
    /// | `"skeletal_mesh_entity_id"` | uint    | ECS entity ID               |
    /// | `"spawn_radius"`           | float   | Offset radius (cm), >= 0    |
    /// | `"per_bone_weighted"`      | bool    | Weight by bone length       |
    [[nodiscard]] static SkeletalSpawnModuleConfig ParseConfig(const nlohmann::json& j);

private:
    SkeletalSpawnModuleConfig config_;

    /// Non-owning pointer to the bone data provider.
    ISkeletalMeshProvider* provider_ = nullptr;

    /// GPU structured buffer holding bone world transforms (float4x4 per bone).
    RHIBufferHandle boneTransformBuffer_ = RHIBufferHandle::Invalid();

    /// Number of bones currently in the GPU buffer.
    uint32_t boneCount_ = 0;
};

} // namespace uz
