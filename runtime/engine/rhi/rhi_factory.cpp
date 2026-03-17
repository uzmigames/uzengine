/// Unified RHI Device Factory
///
/// Dispatches `CreateRHIDevice()` to the correct backend implementation
/// based on `RHIDeviceDesc::backend`. Returns `std::unique_ptr<IRHIDevice>`
/// for RAII-safe ownership transfer.

#include "rhi_backend.h"
#include "log.h"

#include <memory>

// Include backend device headers (Windows-only backends)
#ifdef _WIN32
#include "dx12/dx12_device.h"
#include "dx11/dx11_device.h"
#endif

// Vulkan backend (cross-platform — Windows and Linux)
#include "vulkan/vulkan_device.h"

// Metal backend (macOS/iOS only)
#if defined(__APPLE__)
#include "metal/metal_device.h"
#endif

namespace uz {

namespace {
    RHIBackend g_activeBackend = RHIBackend::DX12;
    ShaderModel g_activeShaderModel = ShaderModel::SM5_0;
} // namespace

RHIBackend GetActiveRHIBackend() { return g_activeBackend; }
ShaderModel GetActiveShaderModel() { return g_activeShaderModel; }

std::unique_ptr<IRHIDevice> CreateRHIDevice(const RHIDeviceDesc& desc) {
    switch (desc.backend) {
#ifdef _WIN32
        case RHIBackend::DX12: {
            auto device = std::make_unique<DX12Device>();
            if (!device->Init(desc)) {
                UZ_LOG(RHI, Error, "CreateRHIDevice: DX12Device::Init failed");
                return nullptr;
            }
            g_activeBackend = desc.backend;
            g_activeShaderModel = device->GetShaderModel();
            return device;
        }

        case RHIBackend::DX11: {
            auto device = std::make_unique<DX11Device>();
            if (!device->Init(desc)) {
                UZ_LOG(RHI, Error, "CreateRHIDevice: DX11Device::Init failed");
                return nullptr;
            }
            g_activeBackend = desc.backend;
            g_activeShaderModel = device->GetShaderModel();
            return device;
        }
#endif

        case RHIBackend::Vulkan: {
            auto device = std::make_unique<VulkanDevice>();
            if (!device->Init(desc)) {
                UZ_LOG(RHI, Error, "CreateRHIDevice: VulkanDevice::Init failed");
                return nullptr;
            }
            g_activeBackend = desc.backend;
            g_activeShaderModel = device->GetShaderModel();
            return device;
        }

#if defined(__APPLE__)
        case RHIBackend::Metal: {
            auto device = std::make_unique<MetalDevice>();
            if (!device->Init(desc)) {
                UZ_LOG(RHI, Error, "CreateRHIDevice: MetalDevice::Init failed");
                return nullptr;
            }
            g_activeBackend = desc.backend;
            return device;
        }
#else
        case RHIBackend::Metal:
            UZ_LOG(RHI, Error, "CreateRHIDevice: Metal backend requires macOS or iOS");
            return nullptr;
#endif

        default:
            UZ_LOG(RHI, Error, "CreateRHIDevice: unsupported backend {}",
                   static_cast<int>(desc.backend));
            return nullptr;
    }
}

} // namespace uz
