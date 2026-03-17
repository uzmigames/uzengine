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
#include "vulkan_format_helpers.h"
#include "vulkan_swapchain.h"
#include "vulkan_fence.h"
#include "vulkan_command_list.h"

#include <tracy/TracyVulkan.hpp>

#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <cstring>

namespace uz {

using namespace uz::vk; // Format/state conversion helpers from vulkan_format_helpers.h

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

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

    // Tracy GPU context disabled — TracyVkContext triggers a VMA assert when
    // VMA_ALLOCATION_CREATE_MAPPED_BIT is used (persistent mapping conflicts with
    // Tracy's internal buffer tracking). Disabling until Tracy 0.12+ resolves the
    // VMA interop issue (TracyVulkan.hpp assumes non-persistently-mapped buffers).
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

    // Enable Vulkan 1.2 features (timeline semaphores, descriptor indexing, bindless)
    VkPhysicalDeviceVulkan12Features vk12Features = {};
    vk12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vk12Features.timelineSemaphore = VK_TRUE;
    vk12Features.descriptorIndexing = VK_TRUE;
    vk12Features.bufferDeviceAddress = VK_TRUE;
    // Bindless descriptor features — equivalent to DX12 SM6.6 ResourceDescriptorHeap
    vk12Features.runtimeDescriptorArray = VK_TRUE;
    vk12Features.descriptorBindingPartiallyBound = VK_TRUE;
    vk12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    vk12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;
    vk12Features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    vk12Features.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
    vk12Features.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;

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

        m_bindlessSupported = supported12.descriptorIndexing
                           && supported12.runtimeDescriptorArray
                           && supported12.descriptorBindingPartiallyBound
                           && supported12.shaderSampledImageArrayNonUniformIndexing;

        if (m_bindlessSupported) {
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
    poolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
                 | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
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
        ci.compareOp = VK_COMPARE_OP_LESS; // Shadow maps use standard-Z (UE5 parity)
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
    //   binding 48..63 = STORAGE_IMAGE  (u0..u15, via -fvk-u-shift 48 0)
    constexpr uint32_t kMaxTextureBindings = 16;
    constexpr uint32_t kSamplerBindingOffset = 16;
    constexpr uint32_t kMaxSamplerBindings = 16;
    constexpr uint32_t kCBBindingOffset = 32;
    constexpr uint32_t kMaxCBBindings = 16;
    constexpr uint32_t kUAVBindingOffset = 48;
    constexpr uint32_t kMaxUAVBindings = 16;

    constexpr uint32_t kTotalBindings = kMaxTextureBindings + kMaxSamplerBindings + kMaxCBBindings + kMaxUAVBindings;
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

    // u registers (u0-u15) → bindings 48-63 as storage images (RWTexture2D / RWBuffer)
    for (uint32_t i = 0; i < kMaxUAVBindings; ++i) {
        auto& b = bindings[kMaxTextureBindings + kMaxSamplerBindings + kMaxCBBindings + i];
        b.binding = kUAVBindingOffset + i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    // Mark all bindings as PARTIALLY_BOUND so unwritten bindings in a new
    // descriptor set snapshot are not accessed by the GPU (we only copy
    // bindings that were actually written via m_writtenBindingsMask).
    VkDescriptorBindingFlags bindingFlags[kTotalBindings] = {};
    for (uint32_t i = 0; i < kTotalBindings; ++i) {
        bindingFlags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
                        | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    }
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCI = {};
    bindingFlagsCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsCI.bindingCount  = kTotalBindings;
    bindingFlagsCI.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutCI = {};
    layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCI.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutCI.pNext = &bindingFlagsCI;
    layoutCI.bindingCount = kTotalBindings;
    layoutCI.pBindings = bindings;

    VkResult result = vkCreateDescriptorSetLayout(m_device, &layoutCI, nullptr, &m_descriptorSetLayout);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateDescriptorSetLayout failed, result={}", static_cast<int>(result));
        return false;
    }

    UZ_LOG(RHI, Info, "Vulkan descriptor set layout created (16 textures + 16 samplers [s0=linear, s1=shadow] + 16 CBVs + 16 UAVs)");
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

VulkanTexture* VulkanDevice::FindTextureByPoolIndex(uint32_t poolIndex) {
    auto& slots = m_texturePool.Slots();
    if (poolIndex < slots.size()) {
        auto& tex = slots[poolIndex];
        if (tex.image != VK_NULL_HANDLE && tex.imageView != VK_NULL_HANDLE) {
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
