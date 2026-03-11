/// UzEngine RHI Configuration Resolution — Implementation
///
/// Reads backend/shader-model settings from Config (CLI + .uzproject)
/// and falls back to platform auto-detection.

#include "rhi_config.h"
#include "uz_config.h"
#include "uz_log.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// String Conversion Helpers
// ─────────────────────────────────────────────────────────────────────────────

const char* RHIBackendToString(RHIBackend backend) {
    switch (backend) {
        case RHIBackend::DX12:   return "DX12";
        case RHIBackend::Vulkan: return "Vulkan";
        case RHIBackend::Metal:  return "Metal";
        case RHIBackend::Null:   return "Null";
    }
    return "Unknown";
}

const char* ShaderModelToString(ShaderModel sm) {
    switch (sm) {
        case ShaderModel::SM5_0: return "SM5.0";
        case ShaderModel::SM5_1: return "SM5.1";
        case ShaderModel::SM6_0: return "SM6.0";
        case ShaderModel::SM6_6: return "SM6.6";
    }
    return "Unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Parsing
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Lowercases a string for case-insensitive comparison.
std::string ToLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

/// Parses a backend name string (case-insensitive).
/// Returns true if recognized, false otherwise.
bool ParseBackend(const std::string& raw, RHIBackend& out) {
    const std::string s = ToLower(raw);
    if (s == "dx12")        { out = RHIBackend::DX12;   return true; }
    if (s == "vulkan")      { out = RHIBackend::Vulkan; return true; }
    if (s == "metal")       { out = RHIBackend::Metal;  return true; }
    if (s == "null")        { out = RHIBackend::Null;   return true; }
    return false;
}

/// Parses a shader model string (case-insensitive).
/// Accepts: "sm5", "sm5.0", "sm5.1", "sm6", "sm6.0", "sm6.6"
bool ParseShaderModel(const std::string& raw, ShaderModel& out) {
    const std::string s = ToLower(raw);
    if (s == "sm5" || s == "sm5.0")  { out = ShaderModel::SM5_0; return true; }
    if (s == "sm5.1")                { out = ShaderModel::SM5_1; return true; }
    if (s == "sm6" || s == "sm6.0")  { out = ShaderModel::SM6_0; return true; }
    if (s == "sm6.6")                { out = ShaderModel::SM6_6; return true; }
    return false;
}

/// Returns the platform default backend.
RHIBackend PlatformDefaultBackend() {
#ifdef _WIN32
    return RHIBackend::DX12;
#elif defined(__APPLE__)
    return RHIBackend::Metal;
#else
    return RHIBackend::Vulkan;
#endif
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ResolveRHIConfig
// ─────────────────────────────────────────────────────────────────────────────

RHIConfig ResolveRHIConfig(const Config& config) {
    RHIConfig result{};
    result.backend = PlatformDefaultBackend();

    // Backend: CLI "rhi" or project "render.rhi"
    const std::string backendStr = config.GetString("rhi",
        config.GetString("render.rhi", ""));
    if (!backendStr.empty()) {
        RHIBackend parsed{};
        if (ParseBackend(backendStr, parsed)) {
            result.backend = parsed;
        } else {
            UZ_LOG(RHI, Warning, "Unknown RHI backend '{}', using default", backendStr);
        }
    }

    // Shader model: CLI "shader-model" / "shader_model" or project "render.shader_model"
    const std::string smStr = config.GetString("shader-model",
        config.GetString("shader_model",
            config.GetString("render.shader_model", "")));
    if (!smStr.empty()) {
        ShaderModel parsed{};
        if (ParseShaderModel(smStr, parsed)) {
            result.shaderModel = parsed;
        } else {
            UZ_LOG(RHI, Warning, "Unknown shader model '{}', using default", smStr);
        }
    }

    // Boolean flags
    result.enableValidation = config.GetBool("gpu-validation",
        config.GetBool("render.gpu_validation", false));
    result.enableGpuValidation = result.enableValidation;
    result.enableBreadcrumbs = config.GetBool("gpu-breadcrumbs",
        config.GetBool("render.gpu_breadcrumbs", false));

    // Adapter index
    result.adapterIndex = static_cast<uint32_t>(config.GetInt("adapter",
        config.GetInt("render.adapter", 0)));

    UZ_LOG(RHI, Info, "Resolved RHI config: backend={}, shader_model={}, validation={}, breadcrumbs={}, adapter={}",
           RHIBackendToString(result.backend),
           ShaderModelToString(result.shaderModel),
           result.enableValidation,
           result.enableBreadcrumbs,
           result.adapterIndex);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// ToDeviceDesc
// ─────────────────────────────────────────────────────────────────────────────

RHIDeviceDesc ToDeviceDesc(const RHIConfig& config) {
    RHIDeviceDesc desc{};
    desc.backend = config.backend;
    desc.enableValidation = config.enableValidation;
    desc.enableGpuValidation = config.enableGpuValidation;
    desc.adapterIndex = config.adapterIndex;
    return desc;
}

} // namespace uz
