/// UzEngine DX12 Triangle Demo
///
/// First geometry render test. Renders a colored RGB triangle using
/// SM6.6 bindless vertex fetch from a StructuredBuffer.
///
/// # What This Validates
/// - Vertex buffer creation as StructuredBuffer
/// - SRV descriptor allocation and bindless access
/// - ShaderPipeline integration (VFS mount → cache → DXC compilation)
/// - Graphics PSO creation (root signature, rasterizer, blend state)
/// - Complete draw call pipeline: Begin → barriers → clear → set pipeline
///   → set root constant → draw → barriers → End → Execute → Present
///
/// # Shader Architecture
/// No Input Assembler vertex layout. The vertex shader receives
/// `SV_VertexID` and fetches vertex data from a StructuredBuffer
/// via `ResourceDescriptorHeap[vertexBufferIndex]`.

#pragma once

#include <cstdint>

namespace uz {

/// Runs the DX12 triangle render loop.
///
/// Creates a 1280x720 window, loads shaders via ShaderPipeline (VFS → cache → DXC),
/// creates a pipeline, uploads triangle vertices to a StructuredBuffer,
/// and renders with triple-buffered frame pacing.
///
/// # Arguments
/// * `frameCount` - Number of frames to render. Pass `0` to run until
///   the user closes the window.
///
/// # Returns
/// `true` if all frames rendered without errors.
/// `false` if any initialization step failed (device, shaders, pipeline, buffers).
///
/// # Side Effects
/// - Opens a visible window titled "UzEngine -- Triangle Demo"
/// - Mounts `{exe_dir}/../demos/triangle/shaders/` into VFS at `/demo/triangle`
/// - Caches compiled shaders in `{exe_dir}/cache/shaders/`
/// - Logs pipeline results, SRV index, and final FPS statistics
///
/// # Example
/// ```cpp
/// uz::LogInit();
/// bool ok = uz::RunTriangleDemo(0); // Run until window close
/// uz::LogShutdown();
/// ```
bool RunTriangleDemo(uint32_t frameCount = 0);

} // namespace uz
