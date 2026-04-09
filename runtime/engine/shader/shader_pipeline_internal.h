/// ShaderPipeline Internal Header
///
/// Shared between shader_pipeline.cpp and shader_pipeline_batch.cpp.
/// NOT part of the public API — consumers include shader_pipeline.h only.

#pragma once

#ifdef _WIN32

#include "shader_pipeline.h"

#include "shader_cache.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "shader_dependency_graph.h"
#include "shader_hot_reload.h"
#include "shader_source_manager.h"
#include "shader_vfs.h"
#include "log.h"
#include "profiler.h"
#include "../rhi/rhi_backend.h"
#include "../rhi/rhi_config.h"

#include <filesystem>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Stage inference from filename convention
// ─────────────────────────────────────────────────────────────────────────────

/// Infers the shader stage from the virtual path using suffix conventions:
/// `_vs.hlsl` → Vertex, `_ps.hlsl` → Pixel, `_cs.hlsl` → Compute, etc.
inline ShaderStageCompile InferStageFromPath(const std::string& path) {
    auto stem = std::filesystem::path(path).stem().string();
    if (stem.size() >= 3) {
        auto suffix = stem.substr(stem.size() - 3);
        if (suffix == "_vs") return ShaderStageCompile::Vertex;
        if (suffix == "_ps") return ShaderStageCompile::Pixel;
        if (suffix == "_cs") return ShaderStageCompile::Compute;
        if (suffix == "_gs") return ShaderStageCompile::Geometry;
        if (suffix == "_hs") return ShaderStageCompile::Hull;
        if (suffix == "_ds") return ShaderStageCompile::Domain;
        if (suffix == "_as") return ShaderStageCompile::Amplification;
        if (suffix == "_ms") return ShaderStageCompile::Mesh;
    }
    if (stem.size() >= 4) {
        auto suffix4 = stem.substr(stem.size() - 4);
        if (suffix4 == "_lib") return ShaderStageCompile::Library;
    }
    // Fallback: check for common naming patterns
    if (stem.find("vert") != std::string::npos) return ShaderStageCompile::Vertex;
    if (stem.find("pixel") != std::string::npos || stem.find("frag") != std::string::npos)
        return ShaderStageCompile::Pixel;
    if (stem.find("comp") != std::string::npos) return ShaderStageCompile::Compute;
    return ShaderStageCompile::Vertex; // Default
}

/// Maps the device ShaderModel to the compiler ShaderModelCompile enum.
inline ShaderModelCompile DeviceSMToCompileSM(ShaderModel sm) {
    switch (sm) {
        case ShaderModel::SM5_0: return ShaderModelCompile::SM5_0;
        case ShaderModel::SM5_1: return ShaderModelCompile::SM5_1;
        case ShaderModel::SM6_0: return ShaderModelCompile::SM6_0;
        case ShaderModel::SM6_1: return ShaderModelCompile::SM6_1;
        case ShaderModel::SM6_2: return ShaderModelCompile::SM6_2;
        case ShaderModel::SM6_3: return ShaderModelCompile::SM6_3;
        case ShaderModel::SM6_4: return ShaderModelCompile::SM6_4;
        case ShaderModel::SM6_5: return ShaderModelCompile::SM6_5;
        case ShaderModel::SM6_6: return ShaderModelCompile::SM6_6;
        case ShaderModel::SM6_7: return ShaderModelCompile::SM6_7;
        case ShaderModel::SM6_8: return ShaderModelCompile::SM6_8;
        case ShaderModel::SM6_9: return ShaderModelCompile::SM6_9;
    }
    return ShaderModelCompile::SM6_6;
}

/// Applies bindless/SM5 configuration and backend-specific output format.
///
/// When bindless is true: injects UZ_BINDLESS=1 define, targets the device's
/// negotiated shader model (SM6.6+).
/// When bindless is false: targets SM5_0 (FXC) for DX backends, SM6_0 (DXC) for Vulkan.
/// When Vulkan backend is active: forces SPIRV output and always uses DXC
/// (FXC cannot produce SPIR-V).
inline void ApplyBindlessConfig(ShaderCompileDesc& desc, bool bindless) {
    const bool isVulkan = (GetActiveRHIBackend() == RHIBackend::Vulkan);

    if (bindless) {
        desc.defines.push_back("UZ_BINDLESS=1");
        // Use the device's actual shader model — compile with the highest SM available
        const ShaderModel deviceSM = GetActiveShaderModel();
        desc.model = DeviceSMToCompileSM(deviceSM);
        // Minimum SM6.6 for bindless
        if (desc.model < ShaderModelCompile::SM6_6) {
            desc.model = ShaderModelCompile::SM6_6;
        }
    } else {
        const bool isDX11 = (GetActiveRHIBackend() == RHIBackend::DX11);
        if (isDX11) {
            // DX11 requires DXBC bytecode via FXC (SM5.0). DXIL (SM6.0+) is rejected
            // by ID3D11Device::CreateVertexShader/CreatePixelShader with E_INVALIDARG.
            desc.model = ShaderModelCompile::SM5_0;
        } else {
            // DX12 without bindless: use SM6_0 (system d3d12.dll supports SM6.0+ root sigs).
            // Vulkan requires DXC (FXC cannot emit SPIR-V), so also SM6_0 minimum.
            desc.model = ShaderModelCompile::SM6_0;
        }
    }

    if (isVulkan) {
        desc.output = ShaderOutputFormat::SPIRV;
    }
}

/// Returns true if the given shader model requires DXC (SM 6.0+).
/// SM 5.0/5.1 use FXC instead.
inline bool RequiresDXC(ShaderModelCompile model) {
    return model >= ShaderModelCompile::SM6_0;
}

// ─────────────────────────────────────────────────────────────────────────────
// PIMPL — holds all subsystems with correct initialization order
// ─────────────────────────────────────────────────────────────────────────────

/// PIMPL internals — owns all shader subsystems in dependency order.
struct ShaderPipeline::Impl {
    ShaderVFS vfs;
    ShaderSourceManager sourceManager;
    ShaderDependencyGraph depGraph;
    ShaderCache cache;
    ShaderHotReload hotReload;
    ShaderCompiler* compiler = nullptr;       ///< DXC — SM 6.0+
    ShaderCompilerFXC* compilerFXC = nullptr; ///< FXC — SM 5.0
    ShaderPipeline::Config config;

    /// VFS must exist before sourceManager is constructed.
    explicit Impl(const ShaderPipeline::Config& cfg)
        : sourceManager(vfs)
        , cache(cfg.cacheDir)
        , config(cfg)
    {
        hotReload.SetDependencyGraph(&depGraph);
    }
};

} // namespace uz

#endif // _WIN32
