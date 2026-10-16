#pragma once

/// UzEngine RHI Backend Registration and Factory
///
/// Provides the factory function for creating a backend-specific IRHIDevice.
/// Backend selection is compile-time only (UZ_BACKEND_DX12, UZ_BACKEND_VULKAN, UZ_BACKEND_METAL).
/// There is no runtime backend switching.

#include "rhi.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace uz {

/// Available RHI backends (compile-time selection only via `UZ_BACKEND_*` defines).
enum class RHIBackend : uint8_t {
    DX12,    ///< DirectX 12 backend (Windows only, SM6.6 bindless).
    DX11,    ///< DirectX 11 backend (Windows only, SM5 slot-based binding).
    Vulkan,  ///< Vulkan backend (cross-platform, Linux/Windows).
    Metal,   ///< Metal backend (macOS/iOS only).
    Null,    ///< Headless backend for testing (no GPU, all operations are no-ops).
};

/// Configuration for device creation via `CreateRHIDevice()`.
///
/// Typically constructed from an `RHIConfig` via `ToDeviceDesc()`, but can
/// be filled manually for direct device creation.
struct RHIDeviceDesc {
    RHIBackend backend = RHIBackend::DX12;   ///< Which backend to initialize.
    bool enableValidation = false;           ///< Enable API validation layer (D3D12 Debug, VK validation).
    bool enableGpuValidation = false;        ///< Enable GPU-based validation (expensive, dev only).
    bool enableBreadcrumbs = false;          ///< Enable DRED auto-breadcrumbs + page-fault tracking (DX12 only).
    uint32_t adapterIndex = 0;               ///< GPU adapter index (0 = system default/highest performance).
    uint8_t debugPoisonByte = 0;             ///< 0 = off; else fill every created resource with this byte (debug determinism gate).

    /// Forces a specific shader model instead of auto-negotiating the highest supported.
    /// Used for testing fallback paths (e.g., force SM5_0 to test FXC compilation).
    /// When set, the device clamps to this shader model even if the GPU supports higher.
    std::optional<ShaderModel> forceShaderModel;
};

/// Creates a backend-specific RHI device.
///
/// # Returns
/// A valid `std::unique_ptr<IRHIDevice>` on success, nullptr on failure
/// (unsupported hardware, missing drivers, etc.). Ownership is transferred
/// to the caller via `unique_ptr` — the device is destroyed when the
/// `unique_ptr` goes out of scope or is reset.
///
/// # Backend Selection
/// The active backend is determined at compile time via UZ_BACKEND_* defines.
/// Passing a backend that was not compiled in returns nullptr.
///
/// # Logging
/// Logs adapter info, feature level, and memory budget on success.
/// Logs detailed error on failure (driver version, missing features, etc.).
std::unique_ptr<IRHIDevice> CreateRHIDevice(const RHIDeviceDesc& desc);

/// Returns the currently active RHI backend.
///
/// Set automatically by `CreateRHIDevice()` on successful device creation.
/// Defaults to `RHIBackend::DX12` if no device has been created yet.
///
/// # Thread Safety
/// Read-only after device creation. Not safe to call concurrently with
/// `CreateRHIDevice()`.
///
/// # Returns
/// The `RHIBackend` value of the most recently created device.
RHIBackend GetActiveRHIBackend();

/// Returns the shader model negotiated by the most recently created device.
///
/// Set automatically by `CreateRHIDevice()` after device initialization.
/// Returns `SM5_0` if no device has been created yet.
ShaderModel GetActiveShaderModel();

} // namespace uz
