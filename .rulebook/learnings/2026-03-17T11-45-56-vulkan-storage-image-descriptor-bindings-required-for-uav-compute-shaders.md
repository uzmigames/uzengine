# Vulkan STORAGE_IMAGE descriptor bindings required for UAV compute shaders
**Source**: manual
**Date**: 2026-03-17
**Related Task**: phase0-0.8.0-uniform-buffer-system
**Tags**: vulkan, descriptor-layout, uav, storage-image, compute, spirv
Vulkan descriptor set layout must include STORAGE_IMAGE bindings for compute shader UAVs (RWTexture2D). UzEngine had 48 bindings (16 tex + 16 sampler + 16 CB) but zero UAV bindings. Added bindings 48-63 as VK_DESCRIPTOR_TYPE_STORAGE_IMAGE and `-fvk-u-shift 48 0` to DXC SPIR-V compilation so HLSL `register(u0)` maps to Vulkan binding 48. Without this, compute shaders writing to UAVs silently fail.