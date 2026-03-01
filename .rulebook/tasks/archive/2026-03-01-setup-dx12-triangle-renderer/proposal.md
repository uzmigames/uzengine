# Proposal: Setup DirectX 12 Triangle Renderer

## Why
This is the foundational phase of the UzEngine PoC. We need to prove that a C++ application can render 3D content natively on the GPU using DirectX 12 with custom HLSL shaders. A spinning 3D triangle with per-vertex color interpolation is the minimal viable proof that GPU rendering is working correctly. This establishes the rendering pipeline that all subsequent phases (frame capture, encoding, streaming) will build upon. Without a working DX12 renderer, no streaming PoC is possible.

## What Changes
- Create the CMake build system with vcpkg manifest for dependency management
- Create a Win32 window for DX12 rendering output
- Initialize the full DirectX 12 pipeline: DXGI factory, device, command queue, swap chain (double-buffered), RTV descriptor heap, root signature, and pipeline state object
- Write HLSL vertex and pixel shaders (embedded as strings, compiled at runtime via D3DCompile)
- Create a vertex buffer with a colored triangle (3 vertices with POSITION + COLOR)
- Create a constant buffer for the MVP (Model-View-Projection) matrix that drives the spinning animation
- Implement the render loop with rotation driven by elapsed time using a high-resolution timer
- Set up the project directory structure for all future phases (src/renderer, src/encoder, src/muxer, src/network, src/common)

## Impact
- Affected specs: renderer/spec.md (new)
- Affected code: CMakeLists.txt, vcpkg.json, src/main.cpp, src/renderer/renderer.h, src/renderer/renderer.cpp, src/renderer/shaders/vertex_shader.hlsl, src/renderer/shaders/pixel_shader.hlsl, src/common/types.h, src/common/timer.h
- Breaking change: NO (greenfield project)
- User benefit: Visual confirmation that DX12 GPU rendering works correctly - a spinning RGB triangle in a native window
