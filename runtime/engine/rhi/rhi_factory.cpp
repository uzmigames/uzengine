/// Unified RHI Device Factory
///
/// Dispatches `CreateRHIDevice()` / `DestroyRHIDevice()` to the correct
/// backend implementation based on `RHIDeviceDesc::backend`.
/// This replaces the per-backend factory functions that were previously
/// defined in each backend's device.cpp file.

#include "rhi_backend.h"
#include "log.h"

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
} // namespace

RHIBackend GetActiveRHIBackend() { return g_activeBackend; }

IRHIDevice* CreateRHIDevice(const RHIDeviceDesc& desc) {
    switch (desc.backend) {
#ifdef _WIN32
        case RHIBackend::DX12: {
            auto* device = new DX12Device();
            if (!device->Init(desc)) {
                UZ_LOG(RHI, Error, "CreateRHIDevice: DX12Device::Init failed");
                delete device;
                return nullptr;
            }
            g_activeBackend = desc.backend;
            return device;
        }

        case RHIBackend::DX11: {
            auto* device = new DX11Device();
            if (!device->Init(desc)) {
                UZ_LOG(RHI, Error, "CreateRHIDevice: DX11Device::Init failed");
                delete device;
                return nullptr;
            }
            g_activeBackend = desc.backend;
            return device;
        }
#endif

        case RHIBackend::Vulkan: {
            auto* device = new VulkanDevice();
            if (!device->Init(desc)) {
                UZ_LOG(RHI, Error, "CreateRHIDevice: VulkanDevice::Init failed");
                delete device;
                return nullptr;
            }
            g_activeBackend = desc.backend;
            return device;
        }

#if defined(__APPLE__)
        case RHIBackend::Metal: {
            auto* device = new MetalDevice();
            if (!device->Init(desc)) {
                UZ_LOG(RHI, Error, "CreateRHIDevice: MetalDevice::Init failed");
                delete device;
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

void DestroyRHIDevice(IRHIDevice* device) {
    delete device;
}

} // namespace uz
