/// UzEngine DX12 Textured Quad Demo
///
/// Renders a textured quad using SM6.6 bindless resource access.
/// Validates the full texture pipeline: creation, upload, SRV allocation,
/// sampler creation, and bindless sampling in the pixel shader.
///
/// # What This Validates
/// - Texture creation with `ShaderResource | CopyDst` usage
/// - Texture upload via `UploadContext::UploadTexture` (staging → default heap)
/// - SRV descriptor allocation in the global bindless heap
/// - Sampler creation and bindless sampler heap access
/// - SM6.6 `ResourceDescriptorHeap[index]` and `SamplerDescriptorHeap[index]`
/// - Root constants with 3 values (vertexBufferIndex, textureIndex, samplerIndex)
///
/// # Shader Architecture
/// The vertex shader fetches from a StructuredBuffer via `ResourceDescriptorHeap`.
/// The pixel shader samples a texture via `ResourceDescriptorHeap` and
/// `SamplerDescriptorHeap`, both indexed by root constants pushed from CPU.

#pragma once

#include <cstdint>

namespace uz {

/// Runs the DX12 textured quad render loop.
///
/// Creates a 1280x720 window, generates a procedural 256x256 checkerboard
/// texture, uploads it to the GPU, and renders a full-screen quad using
/// SM6.6 bindless texture sampling.
///
/// # Arguments
/// * `frameCount` - Number of frames to render. Pass `0` to run until
///   the user closes the window.
///
/// # Returns
/// `true` if all frames rendered without errors.
/// `false` if any initialization step failed.
bool RunTexturedQuadDemo(uint32_t frameCount = 0);

} // namespace uz
