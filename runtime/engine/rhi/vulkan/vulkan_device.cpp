/// Vulkan Device Implementation
///
/// Initializes the Vulkan 1.2+ stack: instance, validation layer, physical device,
/// logical device, VMA allocator, descriptor pool, pipeline layout.
/// All resource Create/Destroy methods are implemented here.
///
/// Uses volk for dynamic function loading (no static Vulkan loader needed).
/// Uses VMA for GPU memory management with volk function pointers.

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__linux__)
#define VK_USE_PLATFORM_XLIB_KHR
#endif

// volk must be included before vulkan.h and defines all function pointers
#define VOLK_IMPLEMENTATION
#include <volk.h>

// VMA with dynamic vulkan functions (volk provides them)
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "vulkan_device.h"
#include "vulkan_swapchain.h"
#include "vulkan_fence.h"
#include "vulkan_command_list.h"

#include <tracy/TracyVulkan.hpp>

#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Maps RHIFormat to VkFormat.
static VkFormat ToVkFormat(RHIFormat format) {
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
static bool IsDepthFormat(RHIFormat format) {
    return format == RHIFormat::D16Unorm || format == RHIFormat::D32Float
        || format == RHIFormat::D24UnormS8Uint || format == RHIFormat::D32FloatS8Uint;
}

/// Returns true if the format has a stencil component.
static bool HasStencil(RHIFormat format) {
    return format == RHIFormat::D24UnormS8Uint || format == RHIFormat::D32FloatS8Uint;
}

/// Maps RHICompareFunc to VkCompareOp.
static VkCompareOp ToVkCompareOp(RHICompareFunc func) {
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
static VkBlendFactor ToVkBlendFactor(RHIBlendFactor factor) {
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
static VkBlendOp ToVkBlendOp(RHIBlendOp op) {
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
static VkSamplerAddressMode ToVkAddressMode(RHIAddressMode mode) {
    switch (mode) {
        case RHIAddressMode::Wrap:   return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case RHIAddressMode::Clamp:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case RHIAddressMode::Mirror: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case RHIAddressMode::Border: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default:                     return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

/// Maps RHIPrimitiveTopology to VkPrimitiveTopology.
static VkPrimitiveTopology ToVkTopology(RHIPrimitiveTopology topo) {
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
static VkCullModeFlags ToVkCullMode(RHICullMode mode) {
    switch (mode) {
        case RHICullMode::None:  return VK_CULL_MODE_NONE;
        case RHICullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        case RHICullMode::Back:  return VK_CULL_MODE_BACK_BIT;
        default:                 return VK_CULL_MODE_BACK_BIT;
    }
}

/// Debug messenger callback — routes validation messages to engine logging.
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* /*userData*/)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        UZ_LOG(RHI, Error, "Vulkan validation: {}", callbackData->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        UZ_LOG(RHI, Warning, "Vulkan validation: {}", callbackData->pMessage);
    } else {
        UZ_LOG(RHI, Debug, "Vulkan validation: {}", callbackData->pMessage);
    }
    return VK_FALSE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

VulkanDevice::~VulkanDevice() {
    WaitIdle();

    // Release all pooled resources
    for (auto& buf : m_bufferPool.Slots()) {
        if (buf.bufferView != VK_NULL_HANDLE) vkDestroyBufferView(m_device, buf.bufferView, nullptr);
        if (buf.buffer != VK_NULL_HANDLE) vmaDestroyBuffer(m_allocator, buf.buffer, buf.allocation);
    }
    for (auto& tex : m_texturePool.Slots()) {
        if (tex.imageView != VK_NULL_HANDLE) vkDestroyImageView(m_device, tex.imageView, nullptr);
        if (tex.ownsImage && tex.image != VK_NULL_HANDLE) {
            vmaDestroyImage(m_allocator, tex.image, tex.allocation);
        }
    }
    for (auto& shader : m_shaderPool.Slots()) {
        if (shader.module != VK_NULL_HANDLE) vkDestroyShaderModule(m_device, shader.module, nullptr);
    }
    for (auto& sampler : m_samplerPool.Slots()) {
        if (sampler.sampler != VK_NULL_HANDLE) vkDestroySampler(m_device, sampler.sampler, nullptr);
    }
    for (auto& pipeline : m_pipelinePool.Slots()) {
        if (pipeline.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, pipeline.pipeline, nullptr);
    }

    if (m_pipelineLayout != VK_NULL_HANDLE)       vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    if (m_staticSamplerLinear != VK_NULL_HANDLE) vkDestroySampler(m_device, m_staticSamplerLinear, nullptr);
    if (m_staticSamplerShadow != VK_NULL_HANDLE) vkDestroySampler(m_device, m_staticSamplerShadow, nullptr);
    if (m_descriptorPool != VK_NULL_HANDLE)      vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    if (m_tracyVkCtx) TracyVkDestroy(m_tracyVkCtx);
    if (m_allocator)                          vmaDestroyAllocator(m_allocator);
    if (m_device != VK_NULL_HANDLE)          vkDestroyDevice(m_device, nullptr);

    if (m_debugMessenger != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    }
    if (m_instance != VK_NULL_HANDLE) vkDestroyInstance(m_instance, nullptr);

    UZ_LOG(RHI, Info, "VulkanDevice destroyed");
}

bool VulkanDevice::Init(const RHIDeviceDesc& desc) {
    UZ_PROFILE_FUNCTION();
    m_validationEnabled = desc.enableValidation;

    if (!CreateInstance(desc.enableValidation))        return false;
    if (desc.enableValidation) SetupDebugMessenger();
    if (!SelectPhysicalDevice(desc.adapterIndex))      return false;
    if (!CreateLogicalDevice())                         return false;
    if (!CreateAllocator())                             return false;
    if (!CreateDescriptorPool())                        return false;
    if (!CreateDescriptorSetLayout())                   return false;
    if (!CreatePipelineLayout())                        return false;

    // Build root layout mapping rootParamIndex → HLSL register.
    // Must match DX12 root signature exactly so SetRootCBV binds to the correct
    // descriptor binding (32 + shaderRegister due to -fvk-b-shift 32 0).
    {
        m_rootLayout.paramCount = 0;
        auto& p0 = m_rootLayout.params[m_rootLayout.paramCount++];
        p0.type = RHIRootParamDesc::Type::Constants;
        p0.shaderRegister = 0;   // b0
        p0.registerSpace = 0;
        p0.num32BitValues = 4;

        auto& p1 = m_rootLayout.params[m_rootLayout.paramCount++];
        p1.type = RHIRootParamDesc::Type::CBV;
        p1.shaderRegister = 1;   // b1 — PerFrame
        p1.registerSpace = 0;

        auto& p2 = m_rootLayout.params[m_rootLayout.paramCount++];
        p2.type = RHIRootParamDesc::Type::CBV;
        p2.shaderRegister = 2;   // b2 — PerObject
        p2.registerSpace = 0;

        auto& p3 = m_rootLayout.params[m_rootLayout.paramCount++];
        p3.type = RHIRootParamDesc::Type::CBV;
        p3.shaderRegister = 4;   // b4 — Shadow (NOT b3!)
        p3.registerSpace = 0;

        auto& p4 = m_rootLayout.params[m_rootLayout.paramCount++];
        p4.type = RHIRootParamDesc::Type::UAV_Table;
        p4.shaderRegister = 0;   // u0
        p4.registerSpace = 0;

        auto& p5 = m_rootLayout.params[m_rootLayout.paramCount++];
        p5.type = RHIRootParamDesc::Type::SRV_Table;
        p5.shaderRegister = 0;   // t0 — SRV table
        p5.registerSpace = 0;
    }

    // Vulkan maps to SM6.6 equivalent (bindless via descriptor indexing)
    m_shaderModel = ShaderModel::SM6_6;
    if (desc.forceShaderModel.has_value() && *desc.forceShaderModel < m_shaderModel) {
        UZ_LOG(RHI, Warning, "Shader model forced: {} -> {}",
               ShaderModelToString(m_shaderModel), ShaderModelToString(*desc.forceShaderModel));
        m_shaderModel = *desc.forceShaderModel;
    }

    // Tracy GPU context disabled — causes VMA assert on buffer operations.
    // TODO: Investigate TracyVkContext interaction with VMA persistent mapping.
    m_tracyVkCtx = nullptr;

    RHIMemoryBudget budget = GetMemoryBudget();
    UZ_LOG(RHI, Info, "VulkanDevice initialized: adapter='{}', api={}, SM={}, VRAM={:.0f}MB",
           m_adapterName, m_apiVersionStr, ShaderModelToString(m_shaderModel),
           static_cast<double>(budget.budgetBytes) / (1024.0 * 1024.0));

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization Steps
// ─────────────────────────────────────────────────────────────────────────────

bool VulkanDevice::CreateInstance(bool enableValidation) {
    UZ_PROFILE_FUNCTION();

    VkResult result = volkInitialize();
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "volkInitialize failed (Vulkan loader not found), result={}",
               static_cast<int>(result));
        return false;
    }

    // Query the highest Vulkan instance version the loader supports.
    uint32_t instanceVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) {
        vkEnumerateInstanceVersion(&instanceVersion);
    }
    UZ_LOG(RHI, Info, "Vulkan loader supports instance version {}.{}.{}",
           VK_API_VERSION_MAJOR(instanceVersion),
           VK_API_VERSION_MINOR(instanceVersion),
           VK_API_VERSION_PATCH(instanceVersion));

    // Request 1.2 if supported, otherwise fall back to what the loader offers.
    uint32_t requestedVersion = VK_API_VERSION_1_2;
    if (instanceVersion < VK_API_VERSION_1_2) {
        UZ_LOG(RHI, Warning, "Loader does not support Vulkan 1.2, requesting {}.{} instead",
               VK_API_VERSION_MAJOR(instanceVersion), VK_API_VERSION_MINOR(instanceVersion));
        requestedVersion = instanceVersion;
    }

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "UzEngine";
    appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 2, 0);
    appInfo.pEngineName = "UzEngine";
    appInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 2, 0);
    appInfo.apiVersion = requestedVersion;

    // Required extensions
    std::vector<const char*> instanceExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined(__linux__)
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif
    };

    std::vector<const char*> layers;
    if (enableValidation) {
        // Check if the validation layer is actually available before requesting it.
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        bool validationLayerFound = false;
        for (const auto& layer : availableLayers) {
            if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                validationLayerFound = true;
                break;
            }
        }

        if (validationLayerFound) {
            instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            layers.push_back("VK_LAYER_KHRONOS_validation");
        } else {
            UZ_LOG(RHI, Warning, "Validation requested but VK_LAYER_KHRONOS_validation not available "
                   "(install Vulkan SDK for validation). Continuing without validation.");
        }
    }

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    createInfo.ppEnabledExtensionNames = instanceExtensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();

    result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateInstance failed, result={}", static_cast<int>(result));
        return false;
    }

    // Load instance-level function pointers via volk
    volkLoadInstance(m_instance);

    UZ_LOG(RHI, Info, "Vulkan instance created (validation={})", enableValidation);
    return true;
}

bool VulkanDevice::SetupDebugMessenger() {
    if (!vkCreateDebugUtilsMessengerEXT) {
        UZ_LOG(RHI, Warning, "VK_EXT_debug_utils not available — skipping debug messenger");
        return false;
    }

    VkDebugUtilsMessengerCreateInfoEXT ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                   | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                   | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = DebugMessengerCallback;

    VkResult result = vkCreateDebugUtilsMessengerEXT(m_instance, &ci, nullptr, &m_debugMessenger);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Warning, "vkCreateDebugUtilsMessengerEXT failed, result={}", static_cast<int>(result));
        return false;
    }

    UZ_LOG(RHI, Info, "Vulkan debug messenger installed");
    return true;
}

bool VulkanDevice::SelectPhysicalDevice(uint32_t adapterIndex) {
    UZ_PROFILE_FUNCTION();

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        UZ_LOG(RHI, Error, "No Vulkan-capable GPUs found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // Sort: discrete GPUs first, then integrated
    std::vector<std::pair<VkPhysicalDevice, VkPhysicalDeviceProperties>> candidates;
    candidates.reserve(deviceCount);
    for (auto dev : devices) {
        VkPhysicalDeviceProperties props = {};
        vkGetPhysicalDeviceProperties(dev, &props);
        candidates.push_back({dev, props});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const auto& a, const auto& b) {
            // Prefer discrete > integrated > virtual > cpu > other
            auto priority = [](VkPhysicalDeviceType type) -> int {
                switch (type) {
                    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 4;
                    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3;
                    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 2;
                    case VK_PHYSICAL_DEVICE_TYPE_CPU:            return 1;
                    default:                                      return 0;
                }
            };
            return priority(a.second.deviceType) > priority(b.second.deviceType);
        });

    if (adapterIndex >= static_cast<uint32_t>(candidates.size())) {
        UZ_LOG(RHI, Error, "GPU adapter index {} out of range (found {})", adapterIndex, candidates.size());
        return false;
    }

    m_physicalDevice = candidates[adapterIndex].first;
    const auto& props = candidates[adapterIndex].second;
    m_adapterName = props.deviceName;

    uint32_t apiMajor = VK_API_VERSION_MAJOR(props.apiVersion);
    uint32_t apiMinor = VK_API_VERSION_MINOR(props.apiVersion);
    uint32_t apiPatch = VK_API_VERSION_PATCH(props.apiVersion);
    m_apiVersionStr = std::to_string(apiMajor) + "." + std::to_string(apiMinor) + "." + std::to_string(apiPatch);

    UZ_LOG(RHI, Info, "Selected GPU [{}]: '{}' (Vulkan {})", adapterIndex, m_adapterName, m_apiVersionStr);
    return true;
}

bool VulkanDevice::CreateLogicalDevice() {
    UZ_PROFILE_FUNCTION();

    // Find queue families
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    m_graphicsQueueFamily = UINT32_MAX;
    m_transferQueueFamily = UINT32_MAX;
    m_computeQueueFamily = UINT32_MAX;

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && m_graphicsQueueFamily == UINT32_MAX) {
            m_graphicsQueueFamily = i;
        }
        // Prefer a dedicated transfer queue (transfer but not graphics)
        if ((queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
            && !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            && m_transferQueueFamily == UINT32_MAX) {
            m_transferQueueFamily = i;
        }
        // Prefer a dedicated compute queue (compute but not graphics)
        if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
            && !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            && m_computeQueueFamily == UINT32_MAX) {
            m_computeQueueFamily = i;
        }
    }

    if (m_graphicsQueueFamily == UINT32_MAX) {
        UZ_LOG(RHI, Error, "No graphics queue family found");
        return false;
    }

    // Fall back to graphics queue for transfers if no dedicated transfer queue
    if (m_transferQueueFamily == UINT32_MAX) {
        m_transferQueueFamily = m_graphicsQueueFamily;
        UZ_LOG(RHI, Info, "No dedicated transfer queue family — transfers will use graphics queue");
    } else {
        UZ_LOG(RHI, Info, "Found dedicated transfer queue family={}", m_transferQueueFamily);
    }

    // Log compute queue discovery result
    if (m_computeQueueFamily == UINT32_MAX) {
        UZ_LOG(RHI, Info, "No dedicated compute queue family — async compute will use graphics queue");
    } else {
        UZ_LOG(RHI, Info, "Found dedicated compute queue family={}", m_computeQueueFamily);
    }

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

    VkDeviceQueueCreateInfo graphicsQueueCI = {};
    graphicsQueueCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    graphicsQueueCI.queueFamilyIndex = m_graphicsQueueFamily;
    graphicsQueueCI.queueCount = 1;
    graphicsQueueCI.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(graphicsQueueCI);

    if (m_transferQueueFamily != m_graphicsQueueFamily) {
        VkDeviceQueueCreateInfo transferQueueCI = {};
        transferQueueCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        transferQueueCI.queueFamilyIndex = m_transferQueueFamily;
        transferQueueCI.queueCount = 1;
        transferQueueCI.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(transferQueueCI);
    }

    // Create dedicated compute queue if found and distinct from graphics/transfer
    if (m_computeQueueFamily != UINT32_MAX
        && m_computeQueueFamily != m_graphicsQueueFamily
        && m_computeQueueFamily != m_transferQueueFamily) {
        VkDeviceQueueCreateInfo computeQueueCI = {};
        computeQueueCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        computeQueueCI.queueFamilyIndex = m_computeQueueFamily;
        computeQueueCI.queueCount = 1;
        computeQueueCI.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(computeQueueCI);
    }

    // Required device extensions
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
    };

    // Enable Vulkan 1.2 features (timeline semaphores, descriptor indexing)
    VkPhysicalDeviceVulkan12Features vk12Features = {};
    vk12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vk12Features.timelineSemaphore = VK_TRUE;
    vk12Features.descriptorIndexing = VK_TRUE;
    vk12Features.bufferDeviceAddress = VK_TRUE;

    // Enable dynamic rendering (VK_KHR_dynamic_rendering)
    VkPhysicalDeviceDynamicRenderingFeatures dynRenderingFeatures = {};
    dynRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynRenderingFeatures.dynamicRendering = VK_TRUE;
    vk12Features.pNext = &dynRenderingFeatures;

    // Enable synchronization2 (VK_KHR_synchronization2)
    VkPhysicalDeviceSynchronization2Features sync2Features = {};
    sync2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    sync2Features.synchronization2 = VK_TRUE;
    dynRenderingFeatures.pNext = &sync2Features;

    VkPhysicalDeviceFeatures deviceFeatures = {};
    deviceFeatures.fillModeNonSolid = VK_TRUE; // Wireframe support
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &vk12Features;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    VkResult result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateDevice failed, result={}", static_cast<int>(result));
        return false;
    }

    // Load device-level function pointers via volk
    volkLoadDevice(m_device);

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_transferQueueFamily, 0, &m_transferQueue);

    if (m_computeQueueFamily != UINT32_MAX) {
        vkGetDeviceQueue(m_device, m_computeQueueFamily, 0, &m_computeQueue);
    }

    UZ_LOG(RHI, Info, "Vulkan device created (graphicsQueue={}, transferQueue={}, computeQueue={})",
           m_graphicsQueueFamily, m_transferQueueFamily,
           m_computeQueueFamily == UINT32_MAX ? -1 : static_cast<int>(m_computeQueueFamily));

    // Query and log descriptor indexing capabilities for future bindless support
    {
        VkPhysicalDeviceVulkan12Features supported12 = {};
        supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

        VkPhysicalDeviceFeatures2 features2 = {};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &supported12;
        vkGetPhysicalDeviceFeatures2(m_physicalDevice, &features2);

        UZ_LOG(RHI, Info, "Descriptor indexing capabilities: "
               "descriptorIndexing={}, "
               "runtimeDescriptorArray={}, "
               "descriptorBindingPartiallyBound={}, "
               "descriptorBindingVariableDescriptorCount={}, "
               "shaderSampledImageArrayNonUniformIndexing={}",
               supported12.descriptorIndexing ? "true" : "false",
               supported12.runtimeDescriptorArray ? "true" : "false",
               supported12.descriptorBindingPartiallyBound ? "true" : "false",
               supported12.descriptorBindingVariableDescriptorCount ? "true" : "false",
               supported12.shaderSampledImageArrayNonUniformIndexing ? "true" : "false");

        if (supported12.descriptorIndexing &&
            supported12.runtimeDescriptorArray &&
            supported12.descriptorBindingPartiallyBound) {
            UZ_LOG(RHI, Info, "Bindless descriptors: AVAILABLE (all required features supported)");
        } else {
            UZ_LOG(RHI, Warning, "Bindless descriptors: NOT AVAILABLE (missing required features)");
        }
    }

    return true;
}

bool VulkanDevice::CreateAllocator() {
    UZ_PROFILE_FUNCTION();

    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorCI = {};
    allocatorCI.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorCI.physicalDevice = m_physicalDevice;
    allocatorCI.device = m_device;
    allocatorCI.instance = m_instance;
    allocatorCI.pVulkanFunctions = &vulkanFunctions;
    allocatorCI.vulkanApiVersion = VK_API_VERSION_1_2;

    VkResult result = vmaCreateAllocator(&allocatorCI, &m_allocator);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vmaCreateAllocator failed, result={}", static_cast<int>(result));
        return false;
    }

    UZ_LOG(RHI, Info, "VMA allocator created");
    return true;
}

bool VulkanDevice::CreateDescriptorPool() {
    UZ_PROFILE_FUNCTION();

    // Large pool sizes for the global descriptor heap
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 16384 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4096 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096 },
        { VK_DESCRIPTOR_TYPE_SAMPLER, 2048 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192 },
    };

    VkDescriptorPoolCreateInfo poolCI = {};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolCI.maxSets = 32768;
    poolCI.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolCI.pPoolSizes = poolSizes;

    VkResult result = vkCreateDescriptorPool(m_device, &poolCI, nullptr, &m_descriptorPool);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateDescriptorPool failed, result={}", static_cast<int>(result));
        return false;
    }

    UZ_LOG(RHI, Info, "Vulkan descriptor pool created");
    return true;
}

bool VulkanDevice::CreateDescriptorSetLayout() {
    UZ_PROFILE_FUNCTION();

    // ── Create static samplers (equivalent to DX12 static samplers) ─────────
    // s0 (binding 16): linear wrap — general texturing
    {
        VkSamplerCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        ci.magFilter = VK_FILTER_LINEAR;
        ci.minFilter = VK_FILTER_LINEAR;
        ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        ci.maxAnisotropy = 1.0f;
        ci.minLod = 0.0f;
        ci.maxLod = VK_LOD_CLAMP_NONE;
        if (vkCreateSampler(m_device, &ci, nullptr, &m_staticSamplerLinear) != VK_SUCCESS) {
            UZ_LOG(RHI, Error, "Failed to create static linear sampler");
            return false;
        }
    }
    // s1 (binding 17): comparison LESS, border black — shadow PCF
    {
        VkSamplerCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        ci.magFilter = VK_FILTER_LINEAR;
        ci.minFilter = VK_FILTER_LINEAR;
        ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        ci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        ci.compareEnable = VK_TRUE;
        ci.compareOp = VK_COMPARE_OP_LESS;
        ci.maxAnisotropy = 1.0f;
        ci.minLod = 0.0f;
        ci.maxLod = VK_LOD_CLAMP_NONE;
        if (vkCreateSampler(m_device, &ci, nullptr, &m_staticSamplerShadow) != VK_SUCCESS) {
            UZ_LOG(RHI, Error, "Failed to create static shadow comparison sampler");
            return false;
        }
    }

    // ── Descriptor set layout ───────────────────────────────────────────────
    // Matches the DXC -fvk-*-shift flags in shader_compiler.cpp:
    //   binding 0..15  = SAMPLED_IMAGE  (t0..t15, via -fvk-t-shift 0 0)
    //   binding 16..31 = SAMPLER        (s0..s15, via -fvk-s-shift 16 0)
    //   binding 32..47 = UNIFORM_BUFFER (b0..b15, via -fvk-b-shift 32 0)
    constexpr uint32_t kMaxTextureBindings = 16;
    constexpr uint32_t kSamplerBindingOffset = 16;
    constexpr uint32_t kMaxSamplerBindings = 16;
    constexpr uint32_t kCBBindingOffset = 32;
    constexpr uint32_t kMaxCBBindings = 16;

    constexpr uint32_t kTotalBindings = kMaxTextureBindings + kMaxSamplerBindings + kMaxCBBindings;
    VkDescriptorSetLayoutBinding bindings[kTotalBindings] = {};

    for (uint32_t i = 0; i < kMaxTextureBindings; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
                               | VK_SHADER_STAGE_COMPUTE_BIT;
    }

    for (uint32_t i = 0; i < kMaxSamplerBindings; ++i) {
        auto& b = bindings[kMaxTextureBindings + i];
        b.binding = kSamplerBindingOffset + i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
                     | VK_SHADER_STAGE_COMPUTE_BIT;
    }

    // Make s0 and s1 immutable samplers — equivalent to DX12 static samplers.
    // These are baked into the descriptor set layout and never need explicit binding.
    bindings[kMaxTextureBindings + 0].pImmutableSamplers = &m_staticSamplerLinear;  // s0
    bindings[kMaxTextureBindings + 1].pImmutableSamplers = &m_staticSamplerShadow;  // s1

    // b registers (b0-b15) → bindings 32-47 as uniform buffers
    for (uint32_t i = 0; i < kMaxCBBindings; ++i) {
        auto& b = bindings[kMaxTextureBindings + kMaxSamplerBindings + i];
        b.binding = kCBBindingOffset + i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
                     | VK_SHADER_STAGE_COMPUTE_BIT;
    }

    // Mark all bindings as PARTIALLY_BOUND so unwritten bindings in a new
    // descriptor set snapshot are not accessed by the GPU (we only copy
    // bindings that were actually written via m_writtenBindingsMask).
    VkDescriptorBindingFlags bindingFlags[kTotalBindings] = {};
    for (uint32_t i = 0; i < kTotalBindings; ++i) {
        bindingFlags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    }
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCI = {};
    bindingFlagsCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsCI.bindingCount  = kTotalBindings;
    bindingFlagsCI.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutCI = {};
    layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCI.pNext = &bindingFlagsCI;
    layoutCI.bindingCount = kTotalBindings;
    layoutCI.pBindings = bindings;

    VkResult result = vkCreateDescriptorSetLayout(m_device, &layoutCI, nullptr, &m_descriptorSetLayout);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateDescriptorSetLayout failed, result={}", static_cast<int>(result));
        return false;
    }

    UZ_LOG(RHI, Info, "Vulkan descriptor set layout created (16 textures + 16 samplers [s0=linear, s1=shadow] + 16 CBVs)");
    return true;
}

bool VulkanDevice::CreatePipelineLayout() {
    UZ_PROFILE_FUNCTION();

    // 128 bytes of push constants — matches DX12 root constant space (32 uint32 values)
    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
                                 | VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 128;

    VkPipelineLayoutCreateInfo layoutCI = {};
    layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCI.pushConstantRangeCount = 1;
    layoutCI.pPushConstantRanges = &pushConstantRange;
    layoutCI.setLayoutCount = 1;
    layoutCI.pSetLayouts = &m_descriptorSetLayout;

    VkResult result = vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &m_pipelineLayout);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreatePipelineLayout failed, result={}", static_cast<int>(result));
        return false;
    }

    UZ_LOG(RHI, Info, "Vulkan pipeline layout created (128 bytes push constants + descriptor set 0)");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Operations
// ─────────────────────────────────────────────────────────────────────────────

RHIBufferHandle VulkanDevice::CreateBuffer(const RHIBufferDesc& desc) {
    UZ_PROFILE_FUNCTION();

    VkBufferCreateInfo bufferCI = {};
    bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.size = desc.sizeBytes;
    bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Map usage flags to VkBufferUsageFlags
    VkBufferUsageFlags usageFlags = 0;
    if (HasFlag(desc.usage, RHIBufferUsage::VertexBuffer))     usageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::IndexBuffer))      usageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::ConstantBuffer))   usageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::StructuredBuffer)) usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::UnorderedAccess))  usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::IndirectArgs))     usageFlags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::CopySrc))          usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::CopyDst))          usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    // Always add transfer bits for flexibility
    usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    // Enable buffer device address for GPU virtual address queries
    usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    bufferCI.usage = usageFlags;

    // Map memory type to VMA
    VmaAllocationCreateInfo allocCI = {};
    switch (desc.memory) {
        case RHIMemoryType::Default:
            allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            break;
        case RHIMemoryType::Upload:
            allocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case RHIMemoryType::Readback:
            allocCI.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
            allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo allocInfo = {};
    VkResult result = vmaCreateBuffer(m_allocator, &bufferCI, &allocCI, &buffer, &allocation, &allocInfo);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vmaCreateBuffer failed: size={}, result={}",
               desc.sizeBytes, static_cast<int>(result));
        return RHIBufferHandle::Invalid();
    }

    // Debug name via VK_EXT_debug_utils
    if (desc.debugName && vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo = {};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(buffer);
        nameInfo.pObjectName = desc.debugName;
        vkSetDebugUtilsObjectNameEXT(m_device, &nameInfo);
    }

    uint32_t generation = 0;
    uint32_t index = m_bufferPool.Allocate(generation);
    VulkanBuffer* slot = m_bufferPool.Get(index, generation);
    if (!slot) {
        vmaDestroyBuffer(m_allocator, buffer, allocation);
        UZ_LOG(RHI, Error, "CreateBuffer: pool allocation failed");
        return RHIBufferHandle::Invalid();
    }

    slot->buffer = buffer;
    slot->allocation = allocation;
    slot->desc = desc;
    slot->generation = generation;
    slot->mappedPtr = allocInfo.pMappedData;

    // Cache the buffer device address for GPU address lookups
    {
        VkBufferDeviceAddressInfo addrInfo = {};
        addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addrInfo.buffer = buffer;
        slot->gpuAddress = vkGetBufferDeviceAddress(m_device, &addrInfo);
    }

    // Assign logical SRV index for structured buffers
    if (HasFlag(desc.usage, RHIBufferUsage::StructuredBuffer) && desc.stride > 0) {
        slot->srvIndex = m_nextSrvIndex++;
    }

    UZ_LOG(RHI, Debug, "CreateBuffer: size={}, stride={} (name='{}')",
           desc.sizeBytes, desc.stride, desc.debugName ? desc.debugName : "");

    return {index, generation};
}

void VulkanDevice::DestroyBuffer(RHIBufferHandle handle) {
    UZ_PROFILE_FUNCTION();
    VulkanBuffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) {
        UZ_LOG(RHI, Warning, "DestroyBuffer: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (buf->bufferView != VK_NULL_HANDLE) {
        vkDestroyBufferView(m_device, buf->bufferView, nullptr);
        buf->bufferView = VK_NULL_HANDLE;
    }
    if (buf->buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, buf->buffer, buf->allocation);
        buf->buffer = VK_NULL_HANDLE;
        buf->allocation = nullptr;
    }
    buf->mappedPtr = nullptr;
    m_bufferPool.Free(handle.index);
}

void* VulkanDevice::MapBuffer(RHIBufferHandle handle) {
    VulkanBuffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) return nullptr;

    // VMA persistent mapping — already mapped at creation for Upload/Readback
    if (buf->mappedPtr) return buf->mappedPtr;

    // Not a mappable buffer
    if (buf->desc.memory == RHIMemoryType::Default) return nullptr;

    // Fallback: map explicitly (should not normally happen for Upload/Readback)
    void* data = nullptr;
    VkResult result = vmaMapMemory(m_allocator, buf->allocation, &data);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "MapBuffer: vmaMapMemory failed, result={}", static_cast<int>(result));
        return nullptr;
    }
    return data;
}

void VulkanDevice::UnmapBuffer(RHIBufferHandle /*handle*/) {
    // All mappable Vulkan buffers use VMA_ALLOCATION_CREATE_MAPPED_BIT (persistent mapping).
    // Explicit unmap is not needed — VMA manages the mapping lifetime.
}

uint32_t VulkanDevice::GetBufferSRVIndex(RHIBufferHandle handle) const {
    const VulkanBuffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) return UINT32_MAX;
    return buf->srvIndex;
}

uint64_t VulkanDevice::GetBufferGPUAddress(RHIBufferHandle handle) const {
    const VulkanBuffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf || buf->buffer == VK_NULL_HANDLE) return 0;
    return buf->gpuAddress;
}

VulkanDevice::BufferAddressLookup VulkanDevice::FindBufferByGPUAddress(uint64_t gpuAddress) const {
    const auto& slots = m_bufferPool.Slots();
    for (const auto& buf : slots) {
        if (buf.buffer == VK_NULL_HANDLE || buf.gpuAddress == 0 || buf.generation == 0) continue;
        const uint64_t bufEnd = buf.gpuAddress + buf.desc.sizeBytes;
        if (gpuAddress >= buf.gpuAddress && gpuAddress < bufEnd) {
            return { buf.buffer, gpuAddress - buf.gpuAddress, buf.desc.sizeBytes };
        }
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Operations
// ─────────────────────────────────────────────────────────────────────────────

RHITextureHandle VulkanDevice::CreateTexture(const RHITextureDesc& desc) {
    UZ_PROFILE_FUNCTION();

    VkImageCreateInfo imageCI = {};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = ToVkFormat(desc.format);
    imageCI.extent = {desc.width, desc.height, 1};
    imageCI.mipLevels = desc.mipLevels;
    imageCI.arrayLayers = desc.depthOrArraySize;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Map usage flags
    VkImageUsageFlags usageFlags = 0;
    if (HasFlag(desc.usage, RHITextureUsage::ShaderResource))  usageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::RenderTarget))    usageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::DepthStencil))    usageFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::UnorderedAccess)) usageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::CopySrc))         usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::CopyDst))         usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    // Always add transfer bits for flexibility
    usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    imageCI.usage = usageFlags;

    // Cube map flag
    if (desc.depthOrArraySize == 6) {
        imageCI.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkResult result = vmaCreateImage(m_allocator, &imageCI, &allocCI, &image, &allocation, nullptr);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vmaCreateImage failed: {}x{}, result={}",
               desc.width, desc.height, static_cast<int>(result));
        return RHITextureHandle::Invalid();
    }

    // Debug name
    if (desc.debugName && vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo = {};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(image);
        nameInfo.pObjectName = desc.debugName;
        vkSetDebugUtilsObjectNameEXT(m_device, &nameInfo);
    }

    // Create image view
    VkImageViewCreateInfo viewCI = {};
    viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image = image;
    viewCI.format = ToVkFormat(desc.format);
    viewCI.subresourceRange.baseMipLevel = 0;
    viewCI.subresourceRange.levelCount = desc.mipLevels;
    viewCI.subresourceRange.baseArrayLayer = 0;
    viewCI.subresourceRange.layerCount = desc.depthOrArraySize;

    if (IsDepthFormat(desc.format)) {
        viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (HasStencil(desc.format)) {
            viewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    if (desc.depthOrArraySize == 6) {
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    } else if (desc.depthOrArraySize > 1) {
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    } else {
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    }

    VkImageView imageView = VK_NULL_HANDLE;
    result = vkCreateImageView(m_device, &viewCI, nullptr, &imageView);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateImageView failed, result={}", static_cast<int>(result));
        vmaDestroyImage(m_allocator, image, allocation);
        return RHITextureHandle::Invalid();
    }

    uint32_t generation = 0;
    uint32_t index = m_texturePool.Allocate(generation);
    VulkanTexture* slot = m_texturePool.Get(index, generation);
    if (!slot) {
        vkDestroyImageView(m_device, imageView, nullptr);
        vmaDestroyImage(m_allocator, image, allocation);
        UZ_LOG(RHI, Error, "CreateTexture: pool allocation failed");
        return RHITextureHandle::Invalid();
    }

    slot->image = image;
    slot->allocation = allocation;
    slot->imageView = imageView;
    slot->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    slot->desc = desc;
    slot->generation = generation;
    slot->ownsImage = true;

    // Assign logical descriptor indices
    if (HasFlag(desc.usage, RHITextureUsage::ShaderResource)) {
        slot->srvIndex = m_nextSrvIndex++;
    }
    if (HasFlag(desc.usage, RHITextureUsage::RenderTarget)) {
        slot->rtvIndex = m_nextSrvIndex++;
    }
    if (HasFlag(desc.usage, RHITextureUsage::DepthStencil)) {
        slot->dsvIndex = m_nextSrvIndex++;
    }

    UZ_LOG(RHI, Debug, "CreateTexture: {}x{}, mips={} (name='{}')",
           desc.width, desc.height, desc.mipLevels,
           desc.debugName ? desc.debugName : "");

    return {index, generation};
}

void VulkanDevice::DestroyTexture(RHITextureHandle handle) {
    UZ_PROFILE_FUNCTION();
    VulkanTexture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) {
        UZ_LOG(RHI, Warning, "DestroyTexture: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (tex->imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, tex->imageView, nullptr);
        tex->imageView = VK_NULL_HANDLE;
    }
    if (tex->ownsImage && tex->image != VK_NULL_HANDLE) {
        vmaDestroyImage(m_allocator, tex->image, tex->allocation);
        tex->image = VK_NULL_HANDLE;
        tex->allocation = nullptr;
    }
    m_texturePool.Free(handle.index);
}

uint32_t VulkanDevice::GetTextureSRVIndex(RHITextureHandle handle) const {
    const VulkanTexture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) return UINT32_MAX;
    return tex->srvIndex;
}

uint32_t VulkanDevice::GetTextureUAVIndex(RHITextureHandle /*handle*/) const {
    return UINT32_MAX; // TODO: Vulkan UAV not yet implemented
}

RHITextureDesc VulkanDevice::GetTextureDesc(RHITextureHandle handle) const {
    const VulkanTexture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) return {};
    return tex->desc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader Operations
// ─────────────────────────────────────────────────────────────────────────────

RHIShaderHandle VulkanDevice::CreateShader(const RHIShaderDesc& desc) {
    UZ_PROFILE_FUNCTION();
    if (!desc.bytecode || desc.bytecodeSize == 0) {
        UZ_LOG(RHI, Error, "CreateShader: null/empty bytecode (name='{}')",
               desc.debugName ? desc.debugName : "");
        return RHIShaderHandle::Invalid();
    }

    VkShaderModuleCreateInfo moduleCI = {};
    moduleCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCI.codeSize = static_cast<size_t>(desc.bytecodeSize);
    moduleCI.pCode = reinterpret_cast<const uint32_t*>(desc.bytecode);

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(m_device, &moduleCI, nullptr, &shaderModule);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateShaderModule failed, result={} (name='{}')",
               static_cast<int>(result), desc.debugName ? desc.debugName : "");
        return RHIShaderHandle::Invalid();
    }

    // Debug name
    if (desc.debugName && vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo = {};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_SHADER_MODULE;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(shaderModule);
        nameInfo.pObjectName = desc.debugName;
        vkSetDebugUtilsObjectNameEXT(m_device, &nameInfo);
    }

    uint32_t generation = 0;
    uint32_t index = m_shaderPool.Allocate(generation);
    VulkanShader* slot = m_shaderPool.Get(index, generation);
    if (!slot) {
        vkDestroyShaderModule(m_device, shaderModule, nullptr);
        UZ_LOG(RHI, Error, "CreateShader: pool allocation failed");
        return RHIShaderHandle::Invalid();
    }

    slot->module = shaderModule;
    slot->bytecode.assign(
        static_cast<const uint8_t*>(desc.bytecode),
        static_cast<const uint8_t*>(desc.bytecode) + desc.bytecodeSize);
    slot->stage = desc.stage;
    slot->generation = generation;

    UZ_LOG(RHI, Debug, "CreateShader: stage={}, size={} (name='{}')",
           static_cast<int>(desc.stage), desc.bytecodeSize,
           desc.debugName ? desc.debugName : "");

    return {index, generation};
}

void VulkanDevice::DestroyShader(RHIShaderHandle handle) {
    UZ_PROFILE_FUNCTION();
    VulkanShader* shader = m_shaderPool.Get(handle.index, handle.generation);
    if (!shader) {
        UZ_LOG(RHI, Warning, "DestroyShader: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (shader->module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_device, shader->module, nullptr);
        shader->module = VK_NULL_HANDLE;
    }
    shader->bytecode.clear();
    m_shaderPool.Free(handle.index);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Operations
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle VulkanDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) {
    UZ_PROFILE_FUNCTION();

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    VulkanShader* vsShader = m_shaderPool.Get(desc.vertexShader.index, desc.vertexShader.generation);
    if (!vsShader || vsShader->module == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Error, "CreateGraphicsPipeline: invalid vertex shader");
        return RHIPipelineHandle::Invalid();
    }

    VkPipelineShaderStageCreateInfo vsStage = {};
    vsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vsStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vsStage.module = vsShader->module;
    vsStage.pName = "main";
    shaderStages.push_back(vsStage);

    if (desc.pixelShader.IsValid()) {
        VulkanShader* psShader = m_shaderPool.Get(desc.pixelShader.index, desc.pixelShader.generation);
        if (psShader && psShader->module != VK_NULL_HANDLE) {
            VkPipelineShaderStageCreateInfo psStage = {};
            psStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            psStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            psStage.module = psShader->module;
            psStage.pName = "main";
            shaderStages.push_back(psStage);
        }
    }

    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputCI = {};
    vertexInputCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkVertexInputBindingDescription bindingDesc = {};
    std::vector<VkVertexInputAttributeDescription> attributeDescs;

    if (desc.inputElementCount > 0 && desc.inputElements) {
        bindingDesc.binding = 0;
        bindingDesc.stride = desc.vertexStride;
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        attributeDescs.resize(desc.inputElementCount);
        for (uint32_t i = 0; i < desc.inputElementCount; ++i) {
            const auto& elem = desc.inputElements[i];
            attributeDescs[i].binding = elem.inputSlot;
            attributeDescs[i].location = i;
            attributeDescs[i].format = ToVkFormat(elem.format);
            attributeDescs[i].offset = elem.byteOffset;
        }

        vertexInputCI.vertexBindingDescriptionCount = 1;
        vertexInputCI.pVertexBindingDescriptions = &bindingDesc;
        vertexInputCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescs.size());
        vertexInputCI.pVertexAttributeDescriptions = attributeDescs.data();
    }

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI = {};
    inputAssemblyCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyCI.topology = ToVkTopology(desc.topology);
    inputAssemblyCI.primitiveRestartEnable = VK_FALSE;

    // Viewport/scissor — dynamic state
    VkPipelineViewportStateCreateInfo viewportCI = {};
    viewportCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportCI.viewportCount = 1;
    viewportCI.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizerCI = {};
    rasterizerCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizerCI.depthClampEnable = VK_FALSE;
    rasterizerCI.rasterizerDiscardEnable = VK_FALSE;
    rasterizerCI.polygonMode = (desc.rasterizer.fillMode == RHIFillMode::Wireframe)
                                   ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizerCI.cullMode = ToVkCullMode(desc.rasterizer.cullMode);
    rasterizerCI.frontFace = desc.rasterizer.frontCounterClockwise
                                 ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rasterizerCI.depthBiasEnable = (desc.rasterizer.depthBias != 0.0f || desc.rasterizer.slopeScaledDepthBias != 0.0f)
                                       ? VK_TRUE : VK_FALSE;
    rasterizerCI.depthBiasConstantFactor = desc.rasterizer.depthBias;
    rasterizerCI.depthBiasSlopeFactor = desc.rasterizer.slopeScaledDepthBias;
    rasterizerCI.lineWidth = 1.0f;

    // Multisample — no MSAA
    VkPipelineMultisampleStateCreateInfo msCI = {};
    msCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth-stencil
    VkPipelineDepthStencilStateCreateInfo depthStencilCI = {};
    depthStencilCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilCI.depthTestEnable = desc.depthStencil.depthEnable ? VK_TRUE : VK_FALSE;
    depthStencilCI.depthWriteEnable = desc.depthStencil.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencilCI.depthCompareOp = ToVkCompareOp(desc.depthStencil.depthFunc);

    // Color blend
    VkPipelineColorBlendAttachmentState blendAttachments[8] = {};
    for (uint32_t i = 0; i < desc.renderTargetCount; ++i) {
        const auto& blend = desc.renderTargetBlend[i];
        blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                           | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachments[i].blendEnable = blend.blendEnable ? VK_TRUE : VK_FALSE;
        blendAttachments[i].srcColorBlendFactor = ToVkBlendFactor(blend.srcColor);
        blendAttachments[i].dstColorBlendFactor = ToVkBlendFactor(blend.dstColor);
        blendAttachments[i].colorBlendOp = ToVkBlendOp(blend.colorOp);
        blendAttachments[i].srcAlphaBlendFactor = ToVkBlendFactor(blend.srcAlpha);
        blendAttachments[i].dstAlphaBlendFactor = ToVkBlendFactor(blend.dstAlpha);
        blendAttachments[i].alphaBlendOp = ToVkBlendOp(blend.alphaOp);
    }

    VkPipelineColorBlendStateCreateInfo colorBlendCI = {};
    colorBlendCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendCI.attachmentCount = desc.renderTargetCount;
    colorBlendCI.pAttachments = blendAttachments;

    // Dynamic state: viewport and scissor
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicStateCI = {};
    dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateCI.dynamicStateCount = 2;
    dynamicStateCI.pDynamicStates = dynamicStates;

    // Dynamic rendering — specify color attachment formats in pNext chain
    std::vector<VkFormat> colorFormats;
    colorFormats.reserve(desc.renderTargetCount);
    for (uint32_t i = 0; i < desc.renderTargetCount; ++i) {
        colorFormats.push_back(ToVkFormat(desc.renderTargetFormats[i]));
    }

    VkPipelineRenderingCreateInfo renderingCI = {};
    renderingCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCI.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
    renderingCI.pColorAttachmentFormats = colorFormats.data();
    renderingCI.depthAttachmentFormat = (desc.depthStencilFormat != RHIFormat::Unknown)
                                            ? ToVkFormat(desc.depthStencilFormat) : VK_FORMAT_UNDEFINED;
    renderingCI.stencilAttachmentFormat = HasStencil(desc.depthStencilFormat)
                                              ? ToVkFormat(desc.depthStencilFormat) : VK_FORMAT_UNDEFINED;

    // Create the pipeline
    VkGraphicsPipelineCreateInfo pipelineCI = {};
    pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.pNext = &renderingCI;
    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();
    pipelineCI.pVertexInputState = &vertexInputCI;
    pipelineCI.pInputAssemblyState = &inputAssemblyCI;
    pipelineCI.pViewportState = &viewportCI;
    pipelineCI.pRasterizationState = &rasterizerCI;
    pipelineCI.pMultisampleState = &msCI;
    pipelineCI.pDepthStencilState = &depthStencilCI;
    pipelineCI.pColorBlendState = &colorBlendCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.layout = m_pipelineLayout;
    pipelineCI.renderPass = VK_NULL_HANDLE; // Dynamic rendering — no render pass

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult pipelineResult = vkCreateGraphicsPipelines(
        m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline);
    if (pipelineResult != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateGraphicsPipelines failed, result={} (name='{}')",
               static_cast<int>(pipelineResult), desc.debugName ? desc.debugName : "");
        return RHIPipelineHandle::Invalid();
    }

    // Debug name
    if (desc.debugName && vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo = {};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(pipeline);
        nameInfo.pObjectName = desc.debugName;
        vkSetDebugUtilsObjectNameEXT(m_device, &nameInfo);
    }

    uint32_t generation = 0;
    uint32_t index = m_pipelinePool.Allocate(generation);
    VulkanPipeline* slot = m_pipelinePool.Get(index, generation);
    if (!slot) {
        vkDestroyPipeline(m_device, pipeline, nullptr);
        UZ_LOG(RHI, Error, "CreateGraphicsPipeline: pool allocation failed");
        return RHIPipelineHandle::Invalid();
    }

    slot->pipeline = pipeline;
    slot->pipelineLayout = m_pipelineLayout;
    slot->topology = desc.topology;
    slot->generation = generation;
    slot->desc = desc;

    UZ_LOG(RHI, Debug, "CreateGraphicsPipeline: {} RT(s) (name='{}')",
           desc.renderTargetCount, desc.debugName ? desc.debugName : "");

    return {index, generation};
}

RHIPipelineHandle VulkanDevice::CreateComputePipeline(const RHIComputePipelineDesc& desc) {
    UZ_LOG(RHI, Warning, "CreateComputePipeline: not yet implemented in Vulkan backend (name='{}')",
           desc.debugName ? desc.debugName : "");
    return RHIPipelineHandle::Invalid();
}

void VulkanDevice::DestroyPipeline(RHIPipelineHandle handle) {
    UZ_PROFILE_FUNCTION();
    VulkanPipeline* pipeline = m_pipelinePool.Get(handle.index, handle.generation);
    if (!pipeline) {
        UZ_LOG(RHI, Warning, "DestroyPipeline: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (pipeline->pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, pipeline->pipeline, nullptr);
        pipeline->pipeline = VK_NULL_HANDLE;
    }
    m_pipelinePool.Free(handle.index);
}

std::vector<uint8_t> VulkanDevice::GetPipelineCachedBlob(RHIPipelineHandle /*handle*/) {
    // Vulkan uses VkPipelineCache for disk persistence natively
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampler Operations
// ─────────────────────────────────────────────────────────────────────────────

RHISamplerHandle VulkanDevice::CreateSampler(const RHISamplerDesc& desc) {
    UZ_PROFILE_FUNCTION();

    VkSamplerCreateInfo samplerCI = {};
    samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    switch (desc.filter) {
        case RHIFilterMode::Point:
            samplerCI.magFilter = VK_FILTER_NEAREST;
            samplerCI.minFilter = VK_FILTER_NEAREST;
            samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        case RHIFilterMode::Linear:
            samplerCI.magFilter = VK_FILTER_LINEAR;
            samplerCI.minFilter = VK_FILTER_LINEAR;
            samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            break;
        case RHIFilterMode::Anisotropic:
            samplerCI.magFilter = VK_FILTER_LINEAR;
            samplerCI.minFilter = VK_FILTER_LINEAR;
            samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerCI.anisotropyEnable = VK_TRUE;
            samplerCI.maxAnisotropy = static_cast<float>(desc.maxAnisotropy);
            break;
    }

    samplerCI.addressModeU = ToVkAddressMode(desc.addressU);
    samplerCI.addressModeV = ToVkAddressMode(desc.addressV);
    samplerCI.addressModeW = ToVkAddressMode(desc.addressW);
    samplerCI.mipLodBias = desc.mipLodBias;
    samplerCI.compareEnable = (desc.compareFunc != RHICompareFunc::Never) ? VK_TRUE : VK_FALSE;
    samplerCI.compareOp = ToVkCompareOp(desc.compareFunc);
    samplerCI.minLod = desc.minLod;
    samplerCI.maxLod = desc.maxLod;
    samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    VkSampler sampler = VK_NULL_HANDLE;
    VkResult result = vkCreateSampler(m_device, &samplerCI, nullptr, &sampler);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateSampler failed, result={}", static_cast<int>(result));
        return RHISamplerHandle::Invalid();
    }

    uint32_t generation = 0;
    uint32_t index = m_samplerPool.Allocate(generation);
    VulkanSampler* slot = m_samplerPool.Get(index, generation);
    if (!slot) {
        vkDestroySampler(m_device, sampler, nullptr);
        UZ_LOG(RHI, Error, "CreateSampler: pool allocation failed");
        return RHISamplerHandle::Invalid();
    }

    slot->sampler = sampler;
    slot->samplerIndex = m_nextSamplerIndex++;
    slot->generation = generation;

    UZ_LOG(RHI, Debug, "CreateSampler: index={} (name='{}')",
           slot->samplerIndex, desc.debugName ? desc.debugName : "");

    return {index, generation};
}

void VulkanDevice::DestroySampler(RHISamplerHandle handle) {
    UZ_PROFILE_FUNCTION();
    VulkanSampler* sampler = m_samplerPool.Get(handle.index, handle.generation);
    if (!sampler) {
        UZ_LOG(RHI, Warning, "DestroySampler: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (sampler->sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, sampler->sampler, nullptr);
        sampler->sampler = VK_NULL_HANDLE;
    }
    m_samplerPool.Free(handle.index);
}

uint32_t VulkanDevice::GetSamplerIndex(RHISamplerHandle handle) const {
    const VulkanSampler* sampler = m_samplerPool.Get(handle.index, handle.generation);
    if (!sampler) return UINT32_MAX;
    return sampler->samplerIndex;
}

// ─────────────────────────────────────────────────────────────────────────────
// Swap Chain Operations
// ─────────────────────────────────────────────────────────────────────────────

IRHISwapChain* VulkanDevice::CreateSwapChain(const RHISwapChainDesc& desc) {
    auto* swapChain = new VulkanSwapChain();
    if (!swapChain->Init(this, desc)) {
        UZ_LOG(RHI, Error, "VulkanSwapChain::Init failed");
        delete swapChain;
        return nullptr;
    }
    return swapChain;
}

void VulkanDevice::DestroySwapChain(IRHISwapChain* swapChain) {
    delete swapChain;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fence Operations
// ─────────────────────────────────────────────────────────────────────────────

IRHIFence* VulkanDevice::CreateFence() {
    auto* fence = new VulkanFence();
    if (!fence->Init(this)) {
        UZ_LOG(RHI, Error, "VulkanFence::Init failed");
        delete fence;
        return nullptr;
    }
    return fence;
}

void VulkanDevice::DestroyFence(IRHIFence* fence) {
    delete fence;
}

// ─────────────────────────────────────────────────────────────────────────────
// Command List Operations
// ─────────────────────────────────────────────────────────────────────────────

IRHICommandList* VulkanDevice::CreateCommandList(RHIQueueType queue) {
    auto* cmdList = new VulkanCommandList();
    if (!cmdList->Init(this, queue)) {
        UZ_LOG(RHI, Error, "VulkanCommandList::Init failed");
        delete cmdList;
        return nullptr;
    }
    return cmdList;
}

void VulkanDevice::DestroyCommandList(IRHICommandList* cmdList) {
    delete cmdList;
}

// ─────────────────────────────────────────────────────────────────────────────
// Synchronization
// ─────────────────────────────────────────────────────────────────────────────

void VulkanDevice::WaitIdle() {
    UZ_PROFILE_FUNCTION();
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Device Queries
// ─────────────────────────────────────────────────────────────────────────────

const char* VulkanDevice::GetAdapterName() const {
    return m_adapterName.c_str();
}

const char* VulkanDevice::GetFeatureLevel() const {
    return m_apiVersionStr.c_str();
}

RHIMemoryBudget VulkanDevice::GetMemoryBudget() const {
    if (!m_physicalDevice) return {};

    VkPhysicalDeviceMemoryProperties memProps = {};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);

    // Sum up device-local heap sizes
    RHIMemoryBudget budget = {};
    for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            budget.budgetBytes += memProps.memoryHeaps[i].size;
        }
    }

    // VMA can provide usage stats
    if (m_allocator) {
        VmaTotalStatistics stats = {};
        vmaCalculateStatistics(m_allocator, &stats);
        budget.usageBytes = stats.total.statistics.allocationBytes;
    }

    return budget;
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource Lookup by Logical Index
// ─────────────────────────────────────────────────────────────────────────────

VulkanTexture* VulkanDevice::FindTextureBySrvIndex(uint32_t srvIndex) {
    for (auto& tex : m_texturePool.Slots()) {
        if (tex.srvIndex == srvIndex && tex.image != VK_NULL_HANDLE) {
            return &tex;
        }
    }
    return nullptr;
}

VulkanSampler* VulkanDevice::FindSamplerByIndex(uint32_t samplerIndex) {
    for (auto& sam : m_samplerPool.Slots()) {
        if (sam.samplerIndex == samplerIndex && sam.sampler != VK_NULL_HANDLE) {
            return &sam;
        }
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Copy Queue Stubs
// ─────────────────────────────────────────────────────────────────────────────

uint64_t VulkanDevice::SignalCopyQueue()
{
    UZ_LOG(RHI, Warning, "Vulkan transfer queue not yet implemented — SignalCopyQueue is a no-op");
    return 0;
}

void VulkanDevice::GraphicsWaitForCopy(uint64_t /*value*/)
{
    UZ_LOG(RHI, Warning, "Vulkan transfer queue not yet implemented — GraphicsWaitForCopy is a no-op");
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute Queue Stubs
// ─────────────────────────────────────────────────────────────────────────────

uint64_t VulkanDevice::SignalComputeQueue()
{
    UZ_LOG(RHI, Warning, "Vulkan compute queue not yet implemented — SignalComputeQueue is a no-op");
    return 0;
}

void VulkanDevice::GraphicsWaitForCompute(uint64_t /*value*/)
{
    UZ_LOG(RHI, Warning, "Vulkan compute queue not yet implemented — GraphicsWaitForCompute is a no-op");
}

void VulkanDevice::ComputeWaitForGraphics(uint64_t /*value*/)
{
    UZ_LOG(RHI, Warning, "Vulkan compute queue not yet implemented — ComputeWaitForGraphics is a no-op");
}

} // namespace uz
