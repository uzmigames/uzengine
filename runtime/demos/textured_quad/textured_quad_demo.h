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

namespace uz {

/// Runs the backend-agnostic textured quad render loop.
///
/// Creates a 1280x720 window, generates a procedural 256x256 checkerboard
/// texture, uploads it to the GPU, and renders a quad using the selected
/// backend. Automatically selects SM6.6 bindless or SM5 Input Assembler
/// path based on GPU capabilities reported by `IRHIDevice::IsBindless()`.
///
/// # Arguments
/// * `backend` - Which RHI backend to initialize (DX12, DX11, etc.).
/// * `frameCount` - Number of frames to render. Pass `0` to run until
///   the user closes the window.
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
/// bool ok = uz::RunTexturedQuadDemo(uz::RHIBackend::DX12, 300);
/// uz::LogShutdown();
/// ```
bool RunTexturedQuadDemo(RHIBackend backend = RHIBackend::DX12, uint32_t frameCount = 0);

} // namespace uz
