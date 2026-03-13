/// UzEngine GPU Residency Control — DX12 MakeResident/Evict wrapper.
///
/// Provides a cross-backend abstraction for controlling GPU resource residency.
/// On DX12, wraps `ID3D12Device::MakeResident` / `ID3D12Device::Evict`.
/// On Vulkan, will manage sparse binding (future).
/// On Metal, no-op (Metal manages residency automatically).
///
/// # Current Status
/// The actual DX12 `MakeResident`/`Evict` calls require `ID3D12Pageable*`,
/// which the RHI does not yet expose. For now, these methods are stubs that
/// log the operation and return `Success`. They will be wired to native
/// calls when the RHI adds `GetNativeResource()` support.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread.

#pragma once

#include "rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// ResidencyResult — Operation result codes
// ─────────────────────────────────────────────────────────────────────────────

/// Result of a GPU residency operation.
enum class ResidencyResult : uint8_t {
    Success,         ///< Operation completed successfully.
    OutOfMemory,     ///< GPU ran out of memory during MakeResident.
    InvalidResource, ///< One or more resource handles were invalid.
    NotSupported,    ///< Backend does not support explicit residency control.
};

/// Returns a human-readable name for a residency result.
///
/// # Returns
/// A null-terminated string literal. Returns "Unknown" for out-of-range values.
[[nodiscard]] const char* ResidencyResultName(ResidencyResult result);

// ─────────────────────────────────────────────────────────────────────────────
// GpuResidencyControl — Residency management class
// ─────────────────────────────────────────────────────────────────────────────

/// Controls GPU resource residency (make resident / evict).
///
/// Wraps platform-specific residency APIs behind a uniform interface.
/// Currently a stub for DX12; actual `ID3D12Pageable*` calls will be
/// added when the RHI exposes native resource access.
///
/// # Lifecycle
/// 1. Construct, call `Init(device)`.
/// 2. Call `MakeResident()` / `Evict()` as needed.
/// 3. Call `Shutdown()` before destroying the device.
class GpuResidencyControl {
public:
    GpuResidencyControl() = default;
    ~GpuResidencyControl() = default;

    // Non-copyable.
    GpuResidencyControl(const GpuResidencyControl&) = delete;
    GpuResidencyControl& operator=(const GpuResidencyControl&) = delete;

    /// Initializes with a device and detects backend support.
    ///
    /// # Parameters
    /// - `device` — Non-null RHI device pointer. Must outlive this object.
    ///
    /// # Returns
    /// `true` on success, `false` if device is null.
    [[nodiscard]] bool Init(IRHIDevice* device);

    /// Shuts down and clears internal state.
    void Shutdown();

    /// Makes textures resident in GPU memory.
    ///
    /// On DX12, this will call `ID3D12Device::MakeResident` on the underlying
    /// `ID3D12Pageable` objects. Currently a stub — returns `Success`.
    ///
    /// # Parameters
    /// - `textures` — Array of texture handles to make resident. Must not be null.
    /// - `count` — Number of handles. Must be > 0.
    ///
    /// # Returns
    /// `ResidencyResult::Success` on success.
    /// `ResidencyResult::NotSupported` if the backend lacks residency control.
    /// `ResidencyResult::InvalidResource` if textures is null or count is 0.
    [[nodiscard]] ResidencyResult MakeResident(const RHITextureHandle* textures, uint32_t count);

    /// Evicts textures from GPU memory.
    ///
    /// On DX12, this will call `ID3D12Device::Evict`. Currently a stub.
    ///
    /// # Parameters
    /// - `textures` — Array of texture handles to evict. Must not be null.
    /// - `count` — Number of handles. Must be > 0.
    ///
    /// # Returns
    /// `ResidencyResult::Success` on success.
    /// `ResidencyResult::NotSupported` if the backend lacks residency control.
    /// `ResidencyResult::InvalidResource` if textures is null or count is 0.
    [[nodiscard]] ResidencyResult Evict(const RHITextureHandle* textures, uint32_t count);

    /// Returns true if explicit residency control is supported on this backend.
    [[nodiscard]] bool IsSupported() const noexcept { return m_supported; }

    /// Returns true if initialized via `Init()`.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_device != nullptr; }

private:
    IRHIDevice* m_device = nullptr;
    bool m_supported = false;
};

} // namespace uz
