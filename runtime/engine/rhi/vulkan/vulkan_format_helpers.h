#pragma once

/// Vulkan format and state conversion helpers.
///
/// Inline functions that map RHI enums to Vulkan equivalents. Shared between
/// vulkan_device.cpp, vulkan_resources.cpp, and vulkan_command_list.cpp.
/// Kept as inline to avoid link-time duplication issues across TUs.

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__linux__)
#define VK_USE_PLATFORM_XLIB_KHR
#endif

#include <volk.h>

#include "../rhi.h"

namespace uz::vk {

/// Maps RHIFormat to VkFormat.
inline VkFormat ToVkFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::R8Unorm:        return VK_FORMAT_R8_UNORM;
        case RHIFormat::RG8Unorm:       return VK_FORMAT_R8G8_UNORM;
        case RHIFormat::RGBA8Unorm:     return VK_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::RGBA8Srgb:      return VK_FORMAT_R8G8B8A8_SRGB;
        case RHIFormat::BGRA8Unorm:     return VK_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::BGRA8Srgb:      return VK_FORMAT_B8G8R8A8_SRGB;
        case RHIFormat::R16Float:       return VK_FORMAT_R16_SFLOAT;
        case RHIFormat::RG16Float:      return VK_FORMAT_R16G16_SFLOAT;
        case RHIFormat::RGBA16Float:    return VK_FORMAT_R16G16B16A16_SFLOAT;
        case RHIFormat::R32Float:       return VK_FORMAT_R32_SFLOAT;
        case RHIFormat::RG32Float:      return VK_FORMAT_R32G32_SFLOAT;
        case RHIFormat::RGB32Float:     return VK_FORMAT_R32G32B32_SFLOAT;
        case RHIFormat::RGBA32Float:    return VK_FORMAT_R32G32B32A32_SFLOAT;
        case RHIFormat::R32Uint:        return VK_FORMAT_R32_UINT;
        case RHIFormat::RG32Uint:       return VK_FORMAT_R32G32_UINT;
        case RHIFormat::RGBA32Uint:     return VK_FORMAT_R32G32B32A32_UINT;
        case RHIFormat::D16Unorm:       return VK_FORMAT_D16_UNORM;
        case RHIFormat::D32Float:       return VK_FORMAT_D32_SFLOAT;
        case RHIFormat::D24UnormS8Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
        case RHIFormat::D32FloatS8Uint: return VK_FORMAT_D32_SFLOAT_S8_UINT;
        default:                        return VK_FORMAT_UNDEFINED;
    }
}

/// Returns true if the format is a depth or depth-stencil format.
inline bool IsDepthFormat(RHIFormat format) {
    return format == RHIFormat::D16Unorm || format == RHIFormat::D32Float
        || format == RHIFormat::D24UnormS8Uint || format == RHIFormat::D32FloatS8Uint;
}

/// Returns true if the format has a stencil component.
inline bool HasStencil(RHIFormat format) {
    return format == RHIFormat::D24UnormS8Uint || format == RHIFormat::D32FloatS8Uint;
}

/// Maps RHICompareFunc to VkCompareOp.
inline VkCompareOp ToVkCompareOp(RHICompareFunc func) {
    switch (func) {
        case RHICompareFunc::Never:        return VK_COMPARE_OP_NEVER;
        case RHICompareFunc::Less:         return VK_COMPARE_OP_LESS;
        case RHICompareFunc::Equal:        return VK_COMPARE_OP_EQUAL;
        case RHICompareFunc::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case RHICompareFunc::Greater:      return VK_COMPARE_OP_GREATER;
        case RHICompareFunc::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
        case RHICompareFunc::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case RHICompareFunc::Always:       return VK_COMPARE_OP_ALWAYS;
        default:                           return VK_COMPARE_OP_LESS;
    }
}

/// Maps RHIBlendFactor to VkBlendFactor.
inline VkBlendFactor ToVkBlendFactor(RHIBlendFactor factor) {
    switch (factor) {
        case RHIBlendFactor::Zero:       return VK_BLEND_FACTOR_ZERO;
        case RHIBlendFactor::One:        return VK_BLEND_FACTOR_ONE;
        case RHIBlendFactor::SrcColor:   return VK_BLEND_FACTOR_SRC_COLOR;
        case RHIBlendFactor::InvSrcColor:return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case RHIBlendFactor::SrcAlpha:   return VK_BLEND_FACTOR_SRC_ALPHA;
        case RHIBlendFactor::InvSrcAlpha:return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case RHIBlendFactor::DstColor:   return VK_BLEND_FACTOR_DST_COLOR;
        case RHIBlendFactor::InvDstColor:return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case RHIBlendFactor::DstAlpha:   return VK_BLEND_FACTOR_DST_ALPHA;
        case RHIBlendFactor::InvDstAlpha:return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        default:                         return VK_BLEND_FACTOR_ONE;
    }
}

/// Maps RHIBlendOp to VkBlendOp.
inline VkBlendOp ToVkBlendOp(RHIBlendOp op) {
    switch (op) {
        case RHIBlendOp::Add:         return VK_BLEND_OP_ADD;
        case RHIBlendOp::Subtract:    return VK_BLEND_OP_SUBTRACT;
        case RHIBlendOp::RevSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case RHIBlendOp::Min:         return VK_BLEND_OP_MIN;
        case RHIBlendOp::Max:         return VK_BLEND_OP_MAX;
        default:                      return VK_BLEND_OP_ADD;
    }
}

/// Maps RHIAddressMode to VkSamplerAddressMode.
inline VkSamplerAddressMode ToVkAddressMode(RHIAddressMode mode) {
    switch (mode) {
        case RHIAddressMode::Wrap:   return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case RHIAddressMode::Clamp:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case RHIAddressMode::Mirror: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case RHIAddressMode::Border: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default:                     return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

/// Maps RHIPrimitiveTopology to VkPrimitiveTopology.
inline VkPrimitiveTopology ToVkTopology(RHIPrimitiveTopology topo) {
    switch (topo) {
        case RHIPrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case RHIPrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case RHIPrimitiveTopology::LineList:       return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case RHIPrimitiveTopology::LineStrip:      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case RHIPrimitiveTopology::PointList:      return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        default:                                   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

/// Maps RHICullMode to VkCullModeFlags.
inline VkCullModeFlags ToVkCullMode(RHICullMode mode) {
    switch (mode) {
        case RHICullMode::None:  return VK_CULL_MODE_NONE;
        case RHICullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        case RHICullMode::Back:  return VK_CULL_MODE_BACK_BIT;
        default:                 return VK_CULL_MODE_BACK_BIT;
    }
}

} // namespace uz::vk
