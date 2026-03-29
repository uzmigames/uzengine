---
name: Vulkan scratch UBO pattern for SetRootConstants
description: Vulkan SetRootConstants uses scratch UBOs (not push constants) because DXC -fvk-b-shift 32 0 maps cbuffer to UBO descriptors
type: project
---

Vulkan shaders compiled with DXC `-fvk-b-shift 32 0` map `cbuffer register(bN)` to a UBO descriptor at binding `32 + N`. Push constants are a completely different Vulkan mechanism that shaders never read.

**Why:** `SetRootConstants` originally used `vkCmdPushConstants()`, which wrote data the shader could not access. All compute passes using `SetRootConstants` (TransmittanceLUT, MultiScatterLUT, IBL baker) produced zero output.

**How to apply:** `SetRootConstants` now creates a VMA-allocated scratch UBO (CPU_TO_GPU, persistently mapped), copies data into it, and writes a `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` descriptor at binding `32 + hlslRegister`. Same pattern as DX11's `GetOrCreateScratchCB`. Scratch UBOs are lazily created per slot (max 8), grow-only, and destroyed in the command list destructor.
