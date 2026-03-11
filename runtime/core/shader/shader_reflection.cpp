/// UzEngine Shader Reflection — SPIRV-Reflect implementation.

#include "shader_reflection.h"
#include "log.h"

#include <spirv_reflect.h>

#include <algorithm>
#include <unordered_map>

UZ_DECLARE_LOG_CATEGORY(Shader);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Type Mapping Helpers
// ─────────────────────────────────────────────────────────────────────────────

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

void SortBindings(std::vector<ShaderResourceBinding>& bindings) {
    std::sort(bindings.begin(), bindings.end(),
        [](const ShaderResourceBinding& a, const ShaderResourceBinding& b) {
            if (a.set != b.set) return a.set < b.set;
            return a.binding < b.binding;
        });
}

} // namespace uz
