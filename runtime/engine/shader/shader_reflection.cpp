/// UzEngine Shader Reflection — SPIRV-Reflect implementation.
///
/// Extracts descriptor bindings, push constant blocks, entry point names,
/// and stage flags from compiled SPIR-V bytecode using the spirv-reflect library.
///
/// Key algorithms:
///   - `ReflectSPIRV`: walks `spvReflectEnumerateDescriptorBindings` and
///     `spvReflectEnumeratePushConstantBlocks`, mapping SPIR-V types to the
///     engine's `ShaderResourceType` vocabulary.
///   - `MergeBindings`: deduplicates bindings from multiple pipeline stages by
///     (set, binding) key, ORing stage-flag bits when the same slot appears in
///     more than one stage.
///   - `SortBindings`: sorts the merged list by (set, binding) for deterministic
///     root-signature / descriptor-set layout construction.

#include "shader_reflection.h"
#include "log.h"
#include "profiler.h"

#include <spirv_reflect.h>

#include <algorithm>
#include <unordered_map>

UZ_DECLARE_LOG_CATEGORY(Shader);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Type Mapping Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Maps a SPIR-V descriptor type integer to the engine's `ShaderResourceType`.
///
/// Combined-image-samplers and plain samplers both become `Sampler`.
/// Storage images, storage buffers, and texel-storage buffers become `UnorderedAccess`.
/// Acceleration structures are treated as read-only `ShaderResource`.
/// Unrecognised descriptor types fall back to `ShaderResource`.
///
/// # Arguments
/// - `spvDescriptorType`: raw `SpvReflectDescriptorType` value cast to uint32_t.
///
/// # Returns
/// The closest engine-side `ShaderResourceType` equivalent.
ShaderResourceType MapSpvDescriptorType(uint32_t spvDescriptorType) {
    switch (static_cast<SpvReflectDescriptorType>(spvDescriptorType)) {
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            return ShaderResourceType::ConstantBuffer;

        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            return ShaderResourceType::ShaderResource;

        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            return ShaderResourceType::UnorderedAccess;

        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
        case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return ShaderResourceType::Sampler;

        case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            return ShaderResourceType::ShaderResource;

        case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            return ShaderResourceType::ShaderResource;

        default:
            return ShaderResourceType::ShaderResource;
    }
}

/// Converts a SPIR-V shader stage bitmask into the engine's `ShaderStageFlags`.
///
/// Each SPIR-V `SPV_REFLECT_SHADER_STAGE_*_BIT` is tested independently so
/// multi-stage modules (rare but valid) produce a correctly combined result.
/// Tessellation control → Hull, tessellation evaluation → Domain, to match
/// HLSL / DX12 terminology.
///
/// # Arguments
/// - `spvStageFlags`: raw `SpvReflectShaderStageFlagBits` value cast to uint32_t.
///
/// # Returns
/// Engine `ShaderStageFlags` bitmask with the corresponding stage bits set.
ShaderStageFlags MapSpvStageFlags(uint32_t spvStageFlags) {
    ShaderStageFlags flags = 0;
    if (spvStageFlags & SPV_REFLECT_SHADER_STAGE_VERTEX_BIT)                  flags |= kStageVertex;
    if (spvStageFlags & SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT)                flags |= kStagePixel;
    if (spvStageFlags & SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT)                 flags |= kStageCompute;
    if (spvStageFlags & SPV_REFLECT_SHADER_STAGE_GEOMETRY_BIT)                flags |= kStageGeometry;
    if (spvStageFlags & SPV_REFLECT_SHADER_STAGE_TESSELLATION_CONTROL_BIT)    flags |= kStageHull;
    if (spvStageFlags & SPV_REFLECT_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) flags |= kStageDomain;
    if (spvStageFlags & SPV_REFLECT_SHADER_STAGE_TASK_BIT_NV)                 flags |= kStageAmplification;
    if (spvStageFlags & SPV_REFLECT_SHADER_STAGE_MESH_BIT_NV)                 flags |= kStageMesh;
    return flags;
}

/// Returns a human-readable string for a `ShaderResourceType` enum value.
/// Intended for logging and debug output only — not a stable serialisation format.
///
/// # Returns
/// A null-terminated string literal; never returns null.
const char* ShaderResourceTypeName(ShaderResourceType type) {
    switch (type) {
        case ShaderResourceType::ConstantBuffer:  return "ConstantBuffer";
        case ShaderResourceType::ShaderResource:   return "ShaderResource";
        case ShaderResourceType::UnorderedAccess:  return "UnorderedAccess";
        case ShaderResourceType::Sampler:          return "Sampler";
        case ShaderResourceType::PushConstant:     return "PushConstant";
    }
    return "Unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// ReflectSPIRV
// ─────────────────────────────────────────────────────────────────────────────

ShaderReflectionResult ReflectSPIRV(const void* bytecode, size_t size) {
    UZ_PROFILE_FUNCTION();
    ShaderReflectionResult result;

    if (bytecode == nullptr || size == 0) {
        result.error = "Null or empty bytecode";
        UZ_LOG(Shader, Error, "ReflectSPIRV failed: {}", result.error);
        return result;
    }

    // SPIRV word size is 4 bytes
    if (size % 4 != 0) {
        result.error = "Bytecode size is not a multiple of 4";
        UZ_LOG(Shader, Error, "ReflectSPIRV failed: {}", result.error);
        return result;
    }

    SpvReflectShaderModule module = {};
    SpvReflectResult spvResult = spvReflectCreateShaderModule(size, bytecode, &module);
    if (spvResult != SPV_REFLECT_RESULT_SUCCESS) {
        result.error = "spvReflectCreateShaderModule failed with code " + std::to_string(static_cast<int>(spvResult));
        UZ_LOG(Shader, Error, "ReflectSPIRV failed: {}", result.error);
        return result;
    }

    // Extract entry point and stage
    if (module.entry_point_count > 0 && module.entry_points != nullptr) {
        result.entryPoint = module.entry_points[0].name ? module.entry_points[0].name : "";
        result.stage = MapSpvStageFlags(static_cast<uint32_t>(module.entry_points[0].shader_stage));
    } else {
        result.stage = MapSpvStageFlags(static_cast<uint32_t>(module.shader_stage));
    }

    // Enumerate descriptor bindings
    uint32_t bindingCount = 0;
    spvResult = spvReflectEnumerateDescriptorBindings(&module, &bindingCount, nullptr);
    if (spvResult != SPV_REFLECT_RESULT_SUCCESS) {
        result.error = "spvReflectEnumerateDescriptorBindings count failed with code " + std::to_string(static_cast<int>(spvResult));
        spvReflectDestroyShaderModule(&module);
        UZ_LOG(Shader, Error, "ReflectSPIRV failed: {}", result.error);
        return result;
    }

    if (bindingCount > 0) {
        std::vector<SpvReflectDescriptorBinding*> spvBindings(bindingCount);
        spvResult = spvReflectEnumerateDescriptorBindings(&module, &bindingCount, spvBindings.data());
        if (spvResult != SPV_REFLECT_RESULT_SUCCESS) {
            result.error = "spvReflectEnumerateDescriptorBindings failed with code " + std::to_string(static_cast<int>(spvResult));
            spvReflectDestroyShaderModule(&module);
            UZ_LOG(Shader, Error, "ReflectSPIRV failed: {}", result.error);
            return result;
        }

        for (uint32_t i = 0; i < bindingCount; ++i) {
            const auto* b = spvBindings[i];
            if (b == nullptr) continue;

            ShaderResourceBinding rb;
            rb.name       = b->name ? b->name : "";
            rb.type       = MapSpvDescriptorType(static_cast<uint32_t>(b->descriptor_type));
            rb.set        = b->set;
            rb.binding    = b->binding;
            rb.count      = (b->count > 0) ? b->count : 1;
            rb.stageFlags = result.stage;
            result.bindings.push_back(std::move(rb));
        }
    }

    // Enumerate push constant blocks
    uint32_t pushCount = 0;
    spvResult = spvReflectEnumeratePushConstantBlocks(&module, &pushCount, nullptr);
    if (spvResult == SPV_REFLECT_RESULT_SUCCESS && pushCount > 0) {
        std::vector<SpvReflectBlockVariable*> pushBlocks(pushCount);
        spvResult = spvReflectEnumeratePushConstantBlocks(&module, &pushCount, pushBlocks.data());
        if (spvResult == SPV_REFLECT_RESULT_SUCCESS) {
            for (uint32_t i = 0; i < pushCount; ++i) {
                const auto* block = pushBlocks[i];
                if (block == nullptr) continue;

                ShaderResourceBinding rb;
                rb.name       = block->name ? block->name : "push_constants";
                rb.type       = ShaderResourceType::PushConstant;
                rb.set        = 0;
                rb.binding    = 0;
                rb.count      = 1;
                rb.stageFlags = result.stage;
                result.bindings.push_back(std::move(rb));
            }
        }
    }

    spvReflectDestroyShaderModule(&module);

    UZ_LOG(Shader, Debug, "ReflectSPIRV: entry='{}', bindings={}, stage=0x{:X}",
           result.entryPoint, result.bindings.size(), result.stage);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

bool ReflectionSucceeded(const ShaderReflectionResult& result) {
    return result.error.empty();
}

/// Merges per-stage reflection results into a single deduplicated binding list.
///
/// When the same (set, binding) slot appears in multiple stages (e.g., a
/// uniform buffer visible to both vertex and fragment shaders), the entries are
/// collapsed into one and their `stageFlags` fields are OR-combined so the final
/// binding accurately represents all stages that consume it.
///
/// Dedup key: `(set << 32) | binding` packed into a uint64_t for cheap hashing.
/// Insertion order within each stage is preserved for the first occurrence.
///
/// # Arguments
/// - `stageResults`: one `ShaderReflectionResult` per compiled stage (e.g., VS, PS).
///
/// # Returns
/// A flat vector of unique bindings, ready for `SortBindings`.
std::vector<ShaderResourceBinding> MergeBindings(
    const std::vector<ShaderReflectionResult>& stageResults)
{
    // Key: (set, binding) → index in merged vector
    struct SetBindingKey {
        uint32_t set;
        uint32_t binding;
        bool operator==(const SetBindingKey& other) const {
            return set == other.set && binding == other.binding;
        }
    };
    struct KeyHash {
        size_t operator()(const SetBindingKey& k) const {
            return std::hash<uint64_t>{}(
                (static_cast<uint64_t>(k.set) << 32) | k.binding);
        }
    };

    std::unordered_map<SetBindingKey, size_t, KeyHash> indexMap;
    std::vector<ShaderResourceBinding> merged;

    for (const auto& stageResult : stageResults) {
        for (const auto& b : stageResult.bindings) {
            SetBindingKey key{b.set, b.binding};
            auto it = indexMap.find(key);
            if (it != indexMap.end()) {
                // Deduplicate: OR the stage flags
                merged[it->second].stageFlags |= b.stageFlags;
            } else {
                indexMap[key] = merged.size();
                merged.push_back(b);
            }
        }
    }

    return merged;
}

/// Sorts a binding list by (set, binding) in ascending order.
///
/// Deterministic ordering is required so that root-signature / descriptor-set
/// layouts built from the same shader produce identical results regardless of
/// the order in which bindings were discovered during reflection.
///
/// # Arguments
/// - `bindings`: list to sort in-place.
void SortBindings(std::vector<ShaderResourceBinding>& bindings) {
    std::sort(bindings.begin(), bindings.end(),
        [](const ShaderResourceBinding& a, const ShaderResourceBinding& b) {
            if (a.set != b.set) return a.set < b.set;
            return a.binding < b.binding;
        });
}

} // namespace uz
