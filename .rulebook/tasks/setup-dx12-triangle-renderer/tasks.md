## 1. Project Setup
- [x] 1.1 Create root CMakeLists.txt with C++20, vcpkg toolchain integration, and DirectX 12 linking <!-- complete -->
- [x] 1.2 Create vcpkg.json manifest with project metadata <!-- complete -->
- [x] 1.3 Create directory structure: src/renderer, src/encoder, src/muxer, src/network, src/common, external, tests, docs <!-- complete -->
- [x] 1.4 Create src/common/types.h with shared type definitions (FrameData, EncoderConfig, resolution constants) <!-- complete -->
- [x] 1.5 Create src/common/timer.h with high-resolution timer using QueryPerformanceCounter <!-- complete -->

## 2. DirectX 12 Initialization
- [x] 2.1 Create src/main.cpp with Win32 window creation (WndProc, RegisterClass, CreateWindowEx) at 1280x720 <!-- complete -->
- [x] 2.2 Create src/renderer/renderer.h with DX12Renderer class interface (init, update, render, shutdown) <!-- complete -->
- [x] 2.3 Implement DXGI factory creation (CreateDXGIFactory2) and GPU adapter enumeration <!-- complete -->
- [x] 2.4 Implement D3D12 device creation (D3D12CreateDevice with feature level 12_0) <!-- complete -->
- [x] 2.5 Implement command queue, command allocator (per frame), and command list creation <!-- complete -->
- [x] 2.6 Implement swap chain creation (IDXGISwapChain4, 2 back buffers, DXGI_FORMAT_R8G8B8A8_UNORM) <!-- complete -->
- [x] 2.7 Implement RTV descriptor heap (2 descriptors for double buffering) <!-- complete -->

## 3. Render Pipeline
- [x] 3.1 Write HLSL vertex shader with cbuffer for worldViewProj matrix, VSInput (POSITION + COLOR), PSInput output <!-- embedded in renderer.h -->
- [x] 3.2 Write HLSL pixel shader with color passthrough and alpha = 1.0 <!-- embedded in renderer.h -->
- [x] 3.3 Implement runtime shader compilation using D3DCompile with embedded HLSL strings <!-- complete -->
- [x] 3.4 Create root signature with one root constant buffer view (register b0) <!-- using root 32-bit constants -->
- [x] 3.5 Create pipeline state object (PSO) with input layout, compiled shaders, rasterizer and blend state <!-- complete -->
- [x] 3.6 Create vertex buffer with triangle (3 vertices: red, green, blue corners) using upload heap <!-- complete -->
- [x] 3.7 Create constant buffer for MVP matrix with per-frame updates <!-- using root 32-bit constants instead -->

## 4. Render Loop
- [x] 4.1 Implement frame update: calculate rotation angle from elapsed time, build MVP matrix using DirectXMath <!-- complete -->
- [x] 4.2 Implement command recording: clear RTV, set viewport/scissor, bind PSO + root signature, set VB, DrawInstanced <!-- complete -->
- [x] 4.3 Implement frame presentation with swap chain Present and fence-based CPU/GPU synchronization <!-- complete -->
- [x] 4.4 Implement window message loop integration with render loop <!-- PeekMessage non-blocking loop -->
- [x] 4.5 Implement graceful shutdown: wait for GPU idle, release all COM objects <!-- complete -->

## 5. Testing
- [x] 5.1 Verify CMake configures and builds successfully with MSVC <!-- build successful -->
- [ ] 5.2 Verify application launches and shows a window at 1280x720
- [ ] 5.3 Verify spinning triangle renders with correct RGB color interpolation
- [ ] 5.4 Verify rotation is smooth and time-based (not frame-rate dependent)
- [ ] 5.5 Verify clean shutdown without memory leaks or device errors (enable D3D12 debug layer)

## 6. Documentation
- [x] 6.1 Create docs/architecture.md with project overview and pipeline diagram <!-- complete -->
- [x] 6.2 Document build instructions (CMake configure + build commands) <!-- included in architecture.md -->
