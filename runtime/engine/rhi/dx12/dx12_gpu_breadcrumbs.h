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

/// Maximum number of breadcrumb markers that can be written per frame.
///
/// Each marker writes a single `uint32_t` to the readback buffer via
/// `WriteBufferImmediate`. Total buffer size = `kMaxBreadcrumbs * 4` bytes.
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
    /// Creates the readback buffer for breadcrumb marker storage.
    ///
    /// # Arguments
    /// * `device` — Parent DX12 device.
    ///
    /// # Returns
    /// `true` on success, `false` if buffer creation fails.
    bool Init(DX12Device* device);

    /// Resets the marker counter for a new frame. Call at the start of each frame.
    void BeginFrame();

    /// Writes a breadcrumb marker at the current GPU execution point.
    ///
    /// Uses `WriteBufferImmediate` (requires `ID3D12GraphicsCommandList2`).
    /// Markers are 1-based so that 0 means "never reached".
    ///
    /// # Arguments
    /// * `cmdList` — Active command list (must support `WriteBufferImmediate`).
    /// * `name` — Human-readable marker name (string literal, not copied).
    void Mark(ID3D12GraphicsCommandList* cmdList, const char* name);

    /// Maps the breadcrumb buffer, finds the last completed marker, and logs results.
    ///
    /// Call after device removal is detected. Logs the full marker trail with
    /// `DONE`, `>>> LAST`, and `NOT REACHED` status for each marker.
    void LogLastMarker();

private:
    DX12Device* m_device = nullptr;

    ComPtr<ID3D12Resource> m_breadcrumbBuffer;    ///< GPU-writable + CPU-readable buffer
    uint32_t m_markerCount = 0;                    ///< Markers written this frame
    const char* m_names[kMaxBreadcrumbs] = {};     ///< Marker name for each index
};

} // namespace uz
