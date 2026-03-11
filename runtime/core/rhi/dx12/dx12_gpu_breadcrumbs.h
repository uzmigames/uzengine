#pragma once

/// DX12 GPU Breadcrumb Markers
///
/// Writes marker values into a GPU-visible buffer via WriteBufferImmediate.
/// On device removal, the readback buffer reveals the last successfully executed marker,
/// pinpointing the exact GPU operation that caused the hang/crash.
///
/// Dev builds only — controlled by UZ_GPU_BREADCRUMBS define.

#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace uz {

using Microsoft::WRL::ComPtr;

class DX12Device;

/// Maximum number of breadcrumb markers per frame.
static constexpr uint32_t kMaxBreadcrumbs = 256;

// ─────────────────────────────────────────────────────────────────────────────
// DX12GpuBreadcrumbs
// ─────────────────────────────────────────────────────────────────────────────

/// Writes sequential marker values to a GPU buffer for crash diagnostics.
///
/// Usage:
///   breadcrumbs.BeginFrame();
///   breadcrumbs.Mark(cmdList, "BeforeGBuffer");
///   // ... GBuffer pass ...
///   breadcrumbs.Mark(cmdList, "AfterGBuffer");
///   // ... on device removal ...
///   breadcrumbs.LogLastMarker();
class DX12GpuBreadcrumbs {
public:
    /// Initializes the breadcrumb buffer. Returns false on failure.
    bool Init(DX12Device* device);

    /// Resets the marker counter for a new frame.
    void BeginFrame();

    /// Writes a breadcrumb marker at the current GPU execution point.
    void Mark(ID3D12GraphicsCommandList* cmdList, const char* name);

    /// Reads back the breadcrumb buffer and logs the last completed marker.
    /// Call after device removal is detected.
    void LogLastMarker();

private:
    DX12Device* m_device = nullptr;

    ComPtr<ID3D12Resource> m_breadcrumbBuffer;    ///< GPU-writable + CPU-readable buffer
    uint32_t m_markerCount = 0;                    ///< Markers written this frame
    const char* m_names[kMaxBreadcrumbs] = {};     ///< Marker name for each index
};

} // namespace uz
