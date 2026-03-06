#pragma once

// gizmo_utils.h — Shared utilities for gizmo GPU resource creation.
// RULE RHI-1: No backend types above rhi.h — uses RHI API only.

#include "engine/renderer/core/renderer.h"  // For Vertex struct
#include "engine/renderer/rhi/rhi.h"

#include <cstdint>
#include <cstring>

namespace uz {

/// Upload a vertex buffer to the GPU via RHI.
/// Returns the RhiBuffer handle, or RHI_NULL on failure.
inline RhiBuffer gizmo_upload_vb(
    const Vertex* verts, uint32_t vertCount,
    const char* debugName)
{
    const uint32_t bufferSize = sizeof(Vertex) * vertCount;

    RhiBufferDesc desc = {};
    desc.size           = bufferSize;
    desc.usage          = RHI_BUFFER_USAGE_VERTEX;
    desc.debug_name     = debugName;

    RhiBuffer buf = rhi()->buffer_create(&desc);
    if (buf == RHI_NULL) {
        OutputDebugStringA("[UzEngine] Failed to create gizmo VB: ");
        OutputDebugStringA(debugName);
        OutputDebugStringA("\n");
        return RHI_NULL;
    }

    rhi()->buffer_upload(buf, verts, bufferSize);
    return buf;
}

} // namespace uz
