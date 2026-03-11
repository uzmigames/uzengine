#pragma once

/// UzEngine RHI Configuration Resolution
///
/// Resolves the active RHI backend and shader model from multiple sources
/// with cascading precedence: CLI > .uzproject > platform auto-detect.

#include "rhi_backend.h"

#include <cstdint>
#include <string>

namespace uz {

class Config; // forward declare

/// Shader model levels supported by the engine.
enum class ShaderModel : uint8_t {
    SM5_0,   ///< DirectX 11 baseline
    SM5_1,   ///< DirectX 11.3+ (dynamic indexing)
    SM6_0,   ///< DirectX 12 baseline (wave intrinsics)
    SM6_1,   ///< Barycentric intrinsics, SV_ViewID
    SM6_2,   ///< Float16, denorm mode control
    SM6_3,   ///< DirectX Raytracing (DXR 1.0)
    SM6_4,   ///< Variable rate shading, low-precision packed dot
    SM6_5,   ///< DXR 1.1, mesh shaders, sampler feedback
    SM6_6,   ///< DirectX 12 Ultimate (bindless, dynamic resources)
    SM6_7,   ///< Advanced texture ops, relaxed precision
};

/// Resolved RHI configuration from all sources.
///
/// Produced by ResolveRHIConfig(), consumed by device creation.
/// All fields have safe defaults matching the highest-capability path.
struct RHIConfig {
    RHIBackend backend = RHIBackend::DX12;
    ShaderModel shaderModel = ShaderModel::SM6_6;
    bool enableValidation = false;
    bool enableGpuValidation = false;
    bool enableBreadcrumbs = false;
    uint32_t adapterIndex = 0;
};

/// Resolves RHI configuration from all available sources.
///
/// Priority (highest to lowest):
/// 1. CLI arguments: --rhi, --shader-model, --gpu-validation, --gpu-breadcrumbs
/// 2. .uzproject config: render.rhi, render.shader_model
/// 3. Auto-detect: platform default backend (DX12 on Windows, Vulkan elsewhere)
///
/// CLI flag formats:
/// - `--rhi dx12|vulkan|metal|null`
/// - `--shader-model sm5|sm5.1|sm6|sm6.1|sm6.2|sm6.3|sm6.4|sm6.5|sm6.6|sm6.7`
/// - `--gpu-validation` (boolean flag)
/// - `--gpu-breadcrumbs` (boolean flag)
/// - `--adapter N` (GPU index)
[[nodiscard]] RHIConfig ResolveRHIConfig(const Config& config);

/// Converts RHIBackend enum to display string.
[[nodiscard]] const char* RHIBackendToString(RHIBackend backend);

/// Converts ShaderModel enum to display string.
[[nodiscard]] const char* ShaderModelToString(ShaderModel sm);

/// Converts a RHIConfig into a RHIDeviceDesc for device creation.
[[nodiscard]] RHIDeviceDesc ToDeviceDesc(const RHIConfig& config);

} // namespace uz
