# Scalars BEFORE matrices in constant buffers — avoids SPIRV std140 offset corruption
**Source**: manual
**Date**: 2026-03-15
**Tags**: rendering, constant-buffer, spirv, std140, cross-platform, gotcha
In constant buffer layouts (PerFrameCB, PerObjectCB), scalar fields (uint32_t indices, float values) must be placed BEFORE matrix fields (Mat4). This is because SPIRV std140 layout rules can cause unexpected padding/offset shifts when scalars follow matrices. Placing scalars first guarantees predictable offsets across DX12 (HLSL) and Vulkan (SPIRV) backends. This pattern is enforced in constant_buffers.h.