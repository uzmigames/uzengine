/// UzEngine Shader Reflection System
///
/// Extracts resource binding metadata from compiled shader bytecode.
/// Currently supports SPIRV via SPIRV-Reflect. DXIL reflection will be
/// added when the DX12 reflection API is wired up.
///
/// # Usage
/// ```cpp
/// auto result = uz::ReflectSPIRV(spirvData, spirvSize);
/// if (uz::ReflectionSucceeded(result)) {
///     for (const auto& b : result.bindings) { ... }
/// }
/// ```

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Enums & Flags
// ─────────────────────────────────────────────────────────────────────────────

/// Type of shader resource binding (maps to DX12 register classes).
enum class ShaderResourceType : uint8_t {
    ConstantBuffer,   ///< CBV (b registers)
    ShaderResource,   ///< SRV (t registers) — textures, structured buffers
    UnorderedAccess,  ///< UAV (u registers) — read-write textures/buffers
    Sampler,          ///< Sampler (s registers)
    PushConstant,     ///< Push constant / root constant
};

/// Bitmask flags identifying which pipeline stages use a binding.
///
/// Multiple flags can be ORed together to indicate a binding used by several stages.
/// Used by `MergeBindings()` to combine per-stage reflection into a unified layout.
enum ShaderStageFlag : uint32_t {
    kStageVertex        = 1 << 0,  ///< Vertex shader stage
    kStagePixel         = 1 << 1,  ///< Pixel/fragment shader stage
    kStageCompute       = 1 << 2,  ///< Compute shader stage
    kStageGeometry      = 1 << 3,  ///< Geometry shader stage
    kStageHull          = 1 << 4,  ///< Hull/tessellation control stage
    kStageDomain        = 1 << 5,  ///< Domain/tessellation evaluation stage
    kStageAmplification = 1 << 6,  ///< Amplification shader stage (mesh pipeline)
    kStageMesh          = 1 << 7,  ///< Mesh shader stage (mesh pipeline)
};

/// Combined bitmask of `ShaderStageFlag` values.
using ShaderStageFlags = uint32_t;

// ─────────────────────────────────────────────────────────────────────────────
// Data Structures
// ─────────────────────────────────────────────────────────────────────────────

/// A single resource binding extracted from shader reflection.
///
/// Represents one descriptor/register slot used by the shader. After merging
/// across stages via `MergeBindings()`, the `stageFlags` field indicates all
/// stages that reference this binding.
struct ShaderResourceBinding {
    std::string name;                ///< Variable name in HLSL (e.g., "g_SceneConstants")
    ShaderResourceType type;         ///< Resource type: CBV, SRV, UAV, Sampler, or PushConstant
    uint32_t set     = 0;            ///< Descriptor set (Vulkan) / register space (DX12)
    uint32_t binding = 0;            ///< Binding slot / register number within the set
    uint32_t count   = 1;            ///< Array size (1 for non-array bindings)
    ShaderStageFlags stageFlags = 0; ///< Bitmask of `ShaderStageFlag` values indicating which stages use this
};

/// Result of reflecting a single shader stage.
struct ShaderReflectionResult {
    std::vector<ShaderResourceBinding> bindings; ///< All resource bindings
    std::string entryPoint;                       ///< Entry point name
    ShaderStageFlags stage = 0;                   ///< Stage this shader targets
    std::string error;                            ///< Non-empty if reflection failed
};

// ─────────────────────────────────────────────────────────────────────────────
// Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Reflects SPIRV bytecode using SPIRV-Reflect to extract resource bindings.
///
/// # Arguments
/// * `bytecode` — Pointer to compiled SPIRV bytecode.
/// * `size` — Size in bytes of the bytecode buffer.
///
/// # Returns
/// `ShaderReflectionResult` with extracted bindings. If reflection fails,
/// `result.error` is non-empty and `result.bindings` is empty.
[[nodiscard]] ShaderReflectionResult ReflectSPIRV(const void* bytecode, size_t size);

/// Checks if a reflection result indicates success (empty error string).
///
/// # Arguments
/// * `result` — The reflection result to check.
///
/// # Returns
/// `true` if `result.error` is empty.
[[nodiscard]] bool ReflectionSucceeded(const ShaderReflectionResult& result);

/// Merges bindings from multiple shader stages into a combined pipeline layout.
///
/// Deduplicates bindings by `(set, binding)` key and ORs `stageFlags` together
/// so the merged result shows which stages reference each binding.
///
/// # Arguments
/// * `stageResults` — Reflection results from each pipeline stage.
///
/// # Returns
/// Merged and deduplicated binding list.
[[nodiscard]] std::vector<ShaderResourceBinding> MergeBindings(
    const std::vector<ShaderReflectionResult>& stageResults);

/// Sorts bindings by `(set, binding)` for deterministic output.
///
/// # Arguments
/// * `bindings` — Binding list to sort in-place.
void SortBindings(std::vector<ShaderResourceBinding>& bindings);

// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers (exposed for testing)
// ─────────────────────────────────────────────────────────────────────────────

/// Maps a SPIRV-Reflect descriptor type enum to our `ShaderResourceType`.
///
/// # Arguments
/// * `spvDescriptorType` — SPIRV-Reflect `SpvReflectDescriptorType` value.
///
/// # Returns
/// Mapped `ShaderResourceType`. Returns `ShaderResource` for unknown types
/// as a safe default.
[[nodiscard]] ShaderResourceType MapSpvDescriptorType(uint32_t spvDescriptorType);

/// Maps SPIRV-Reflect shader stage bitmask to our `ShaderStageFlags`.
///
/// # Arguments
/// * `spvStageFlags` — SPIRV-Reflect `SpvReflectShaderStageFlagBits` bitmask.
///
/// # Returns
/// Combined `ShaderStageFlags` bitmask.
[[nodiscard]] ShaderStageFlags MapSpvStageFlags(uint32_t spvStageFlags);

/// Returns a human-readable string name for a `ShaderResourceType` value.
///
/// # Arguments
/// * `type` — The resource type to name.
///
/// # Returns
/// Static string (e.g., "ConstantBuffer", "ShaderResource"). Never null.
[[nodiscard]] const char* ShaderResourceTypeName(ShaderResourceType type);

} // namespace uz
