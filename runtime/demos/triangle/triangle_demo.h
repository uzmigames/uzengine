#pragma once

/// DX12 Triangle Demo — first geometry render test.
///
/// Renders a colored triangle using SM6.6 bindless vertex fetch.
/// Validates: vertex buffer creation, structured buffer SRV, shader compilation,
/// PSO creation, and the complete draw call pipeline.

#include <cstdint>

namespace uz {

/// Runs the triangle render loop for the specified number of frames.
///
/// Returns true if all frames completed without DX12 validation errors.
/// Pass 0 for frameCount to run until window close.
bool RunTriangleDemo(uint32_t frameCount = 0);

} // namespace uz
