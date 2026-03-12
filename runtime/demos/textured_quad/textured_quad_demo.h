/// UzEngine Textured Quad Demo — Backend-Agnostic
///
/// Creates a window, initializes an RHI device (any backend), and renders
/// a procedural checkerboard quad using triple-buffered frame pacing.
///
/// Validates the full texture pipeline across all RHI backends:
/// - Texture creation with `ShaderResource | CopyDst` usage
/// - Texture upload via `UploadContext::UploadTexture` (staging → default heap)
/// - SRV descriptor allocation in the global bindless heap
/// - Sampler creation and sampler heap access
/// - SM6.6 bindless path: `ResourceDescriptorHeap[index]` / `SamplerDescriptorHeap[index]`
/// - SM5 fallback path: Input Assembler vertex binding + descriptor table SRV
/// - Root constants with 3 values (vertexBufferIndex, textureIndex, samplerIndex)
///
/// # Shader Architecture
/// The vertex shader fetches vertex data from a StructuredBuffer via
/// `ResourceDescriptorHeap` (SM6.6) or from the Input Assembler (SM5).
/// The pixel shader samples via heap indices pushed through root constants.

#pragma once

#include "rhi_backend.h"

#include <cstdint>
#include <string>

namespace uz {

/// Configuration for running the textured quad demo.
struct TexturedQuadConfig {
    uint32_t frameCount = 0;            ///< Number of frames to render (0 = infinite)
    std::string screenshotPath;         ///< If non-empty, save screenshot to this path
    uint32_t screenshotFrame = 5;       ///< Frame number to capture (default: 5)
    RHIBackend backend = RHIBackend::DX12; ///< RHI backend to use
};

/// Runs the backend-agnostic textured quad render loop.
///
/// Creates a 1280x720 window, generates a procedural 256x256 checkerboard
/// texture, uploads it to the GPU, and renders a quad using the selected
/// backend. Automatically selects SM6.6 bindless or SM5 Input Assembler
/// path based on GPU capabilities reported by `IRHIDevice::IsBindless()`.
///
/// # Arguments
/// * `config` — Demo configuration (frame count, screenshot settings, backend).
///
/// # Returns
/// `true` if all frames rendered without errors.
/// `false` if any initialization step failed.
///
/// # Side Effects
/// - Opens a visible window titled "UzEngine -- Textured Quad Demo"
/// - Logs RHI errors on failure
/// - Logs final FPS statistics on completion
///
/// # Example
/// ```cpp
/// uz::LogInit();
/// uz::TexturedQuadConfig cfg;
/// cfg.backend = uz::RHIBackend::DX12;
/// bool ok = uz::RunTexturedQuadDemo(cfg);
/// uz::LogShutdown();
/// ```
bool RunTexturedQuadDemo(const TexturedQuadConfig& config = {});

} // namespace uz
