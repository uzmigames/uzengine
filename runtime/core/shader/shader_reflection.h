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
enum ShaderStageFlag : uint32_t {
    kStageVertex        = 1 << 0,
    kStagePixel         = 1 << 1,
    kStageCompute       = 1 << 2,
    kStageGeometry      = 1 << 3,
    kStageHull          = 1 << 4,
    kStageDomain        = 1 << 5,
    kStageAmplification = 1 << 6,
    kStageMesh          = 1 << 7,
};
using ShaderStageFlags = uint32_t;

// ─────────────────────────────────────────────────────────────────────────────
// Data Structures
// ─────────────────────────────────────────────────────────────────────────────

/// A single resource binding extracted from shader reflection.
struct ShaderResourceBinding {
    std::string name;                ///< Variable name in HLSL
    ShaderResourceType type;         ///< CBV, SRV, UAV, Sampler, PushConstant
    uint32_t set     = 0;            ///< Descriptor set (Vulkan) / register space (DX12)
    uint32_t binding = 0;            ///< Binding slot / register number
    uint32_t count   = 1;            ///< Array size (1 for non-array)
    ShaderStageFlags stageFlags = 0; ///< Which stages use this binding
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

/// Reflect SPIRV bytecode using SPIRV-Reflect.
/// Returns extracted bindings, or a result with non-empty `error` on failure.
[[nodiscard]] ShaderReflectionResult ReflectSPIRV(const void* bytecode, size_t size);

/// Check if reflection succeeded (no error).
[[nodiscard]] bool ReflectionSucceeded(const ShaderReflectionResult& result);

/// Merge bindings from multiple stages into a combined layout.
/// Deduplicates by (set, binding) and ORs stage flags together.
[[nodiscard]] std::vector<ShaderResourceBinding> MergeBindings(
    const std::vector<ShaderReflectionResult>& stageResults);

/// Sort bindings by (set, binding) for deterministic output.
void SortBindings(std::vector<ShaderResourceBinding>& bindings);

// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers (exposed for testing)
// ─────────────────────────────────────────────────────────────────────────────

/// Maps a SPIRV-Reflect descriptor type to our ShaderResourceType.
/// Returns ShaderResource for unknown types as a safe default.
[[nodiscard]] ShaderResourceType MapSpvDescriptorType(uint32_t spvDescriptorType);

/// Maps SPIRV-Reflect shader stage bits to our ShaderStageFlags.
[[nodiscard]] ShaderStageFlags MapSpvStageFlags(uint32_t spvStageFlags);

/// Returns the string name for a ShaderResourceType (for logging/debug).
[[nodiscard]] const char* ShaderResourceTypeName(ShaderResourceType type);

} // namespace uz
