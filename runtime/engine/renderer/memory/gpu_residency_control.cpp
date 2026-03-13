/// GpuResidencyControl implementation.
///
/// Stub implementation for DX12 MakeResident/Evict. The actual native calls
/// will be wired when the RHI exposes ID3D12Pageable* via GetNativeResource().

#include "gpu_residency_control.h"
#include "log.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ResidencyResultName
// ─────────────────────────────────────────────────────────────────────────────

const char* ResidencyResultName(ResidencyResult result) {
    switch (result) {
    case ResidencyResult::Success:         return "Success";
    case ResidencyResult::OutOfMemory:     return "OutOfMemory";
    case ResidencyResult::InvalidResource: return "InvalidResource";
    case ResidencyResult::NotSupported:    return "NotSupported";
    default:                               return "Unknown";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool GpuResidencyControl::Init(IRHIDevice* device) {
    if (!device) {
        UZ_LOG(Renderer, Error, "GpuResidencyControl::Init: device is null");
        return false;
    }

    m_device = device;

    // DX12 supports explicit residency control. Vulkan uses sparse binding
    // (future). Metal manages residency automatically (not supported).
    // For now, assume DX12 backend — set supported to true.
    // TODO: Query actual backend type from device when RHI exposes it.
    m_supported = true;

    UZ_LOG(Renderer, Info, "GpuResidencyControl initialized: supported={}",
           m_supported ? "true" : "false");
    return true;
}

void GpuResidencyControl::Shutdown() {
    m_device = nullptr;
    m_supported = false;

    UZ_LOG(Renderer, Info, "GpuResidencyControl shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// MakeResident / Evict
// ─────────────────────────────────────────────────────────────────────────────

ResidencyResult GpuResidencyControl::MakeResident(const RHITextureHandle* textures,
                                                   uint32_t count) {
    if (!textures || count == 0) {
        UZ_LOG(Renderer, Error,
               "GpuResidencyControl::MakeResident: invalid args (textures={}, count={})",
               textures ? "valid" : "null", count);
        return ResidencyResult::InvalidResource;
    }

    if (!m_supported) {
        return ResidencyResult::NotSupported;
    }

    // Stub: actual ID3D12Device::MakeResident calls go here once the RHI
    // exposes ID3D12Pageable* via a GetNativeResource() method.
    UZ_LOG(Renderer, Trace,
           "GpuResidencyControl::MakeResident: {} textures (stub)", count);

    return ResidencyResult::Success;
}

ResidencyResult GpuResidencyControl::Evict(const RHITextureHandle* textures,
                                            uint32_t count) {
    if (!textures || count == 0) {
        UZ_LOG(Renderer, Error,
               "GpuResidencyControl::Evict: invalid args (textures={}, count={})",
               textures ? "valid" : "null", count);
        return ResidencyResult::InvalidResource;
    }

    if (!m_supported) {
        return ResidencyResult::NotSupported;
    }

    // Stub: actual ID3D12Device::Evict calls go here.
    UZ_LOG(Renderer, Trace,
           "GpuResidencyControl::Evict: {} textures (stub)", count);

    return ResidencyResult::Success;
}

} // namespace uz
