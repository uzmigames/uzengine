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

namespace uz {

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
            return device;
        }

        case RHIBackend::DX11: {
            auto* device = new DX11Device();
            if (!device->Init(desc)) {
                UZ_LOG(RHI, Error, "CreateRHIDevice: DX11Device::Init failed");
                delete device;
                return nullptr;
            }
            return device;
        }
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
