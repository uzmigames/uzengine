## 1. Swap Chain Recreation (Flip Model)

- [ ] 1.1 Destroy the existing `IDXGISwapChain` in `src/renderer/swap_chain.cpp` using a dedicated `SwapChain::Destroy()` method that waits for all in-flight frames to complete via fence signals before releasing COM references
- [ ] 1.2 Recreate the swap chain in `src/renderer/swap_chain.cpp` using `DXGI_SWAP_EFFECT_FLIP_DISCARD`, `DXGI_SCALING_NONE`, 3 back buffers, and `DXGI_FORMAT_R8G8B8A8_UNORM` format
- [ ] 1.3 Query `IDXGIFactory5::CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING)` at swap chain creation time and store the result in `SwapChain::m_tearing_supported`; log the result with `UZ_LOG_INFO`
- [ ] 1.4 Implement `SwapChain::RefreshBackBufferPointers()` that re-acquires `ID3D12Resource*` handles for all 3 back buffers via `GetBuffer()` after swap chain recreation; call this from `renderer.cpp` immediately after recreation
- [ ] 1.5 Add `SwapChain::Resize(width, height)` that destroys and recreates the swap chain at the new resolution, refreshing back buffer pointers and notifying dependent render targets via a callback registered by `renderer.cpp`

## 2. HDR Output Detection and Color Space

- [ ] 2.1 At swap chain creation, enumerate attached monitors via `IDXGIOutput6::GetDesc1` and check `ColorSpace` for `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020` support to detect an HDR-capable display
- [ ] 2.2 When HDR is detected, call `IDXGISwapChain4::SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)` and switch the back buffer format to `DXGI_FORMAT_R16G16B16A16_FLOAT`; store the active color space in `SwapChain::m_hdr_enabled`
- [ ] 2.3 Add CVar `r.hdr_output` (default `0`) that overrides HDR auto-detection; when set to `1` forces HDR path; when `0` forces SDR regardless of display capability

## 3. VSync Mode CVars and Present Logic

- [ ] 3.1 Add CVar `r.vsync_mode` (int, default `1`) in `src/renderer/renderer.cpp` with valid range 0-3: `0`=uncapped, `1`=VSYNC ON, `2`=MAILBOX, `3`=VRR
- [ ] 3.2 Implement VSync mode `0` (uncapped): call `Present(0, DXGI_PRESENT_ALLOW_TEARING)` when tearing is supported, else `Present(0, 0)`
- [ ] 3.3 Implement VSync mode `1` (VSYNC ON): call `IDXGIOutput::WaitForVBlank()` before `Present(1, 0)` to deliver frames at exact vblank boundaries and prevent irregular CPU scheduling jitter
- [ ] 3.4 Implement VSync mode `2` (MAILBOX): call `Present(0, 0)` with the flip model and enforce a frame limiter (see section 4); no tearing flag needed because flip discards late frames
- [ ] 3.5 Implement VSync mode `3` (VRR): call `IDXGISwapChain1::SetMaximumFrameLatency(1)`, wait on the frame latency waitable object, and call `Present(0, DXGI_PRESENT_ALLOW_TEARING)`; only activate when `m_tearing_supported` is true, otherwise fall back to mode 1 with a warning
- [ ] 3.6 Read `r.vsync_mode` each frame before `Present` to allow runtime switching without recreating the swap chain; log mode transitions with `UZ_LOG_INFO`

## 4. Frame Rate Limiter

- [ ] 4.1 Add CVar `r.frame_rate_limit` (float, default `0.0` = unlimited) in `src/renderer/renderer.cpp`; when non-zero, compute target frame time as `1.0 / r.frame_rate_limit` seconds
- [ ] 4.2 Implement the frame limiter in `src/renderer/swap_chain.cpp` using `QueryPerformanceCounter` and a high-resolution spin-wait loop that yields via `_mm_pause()` to avoid burning a full CPU core; sleep for `target_time - 2ms` then spin for the remaining 2ms
- [ ] 4.3 Apply the frame limiter only in VSync modes `0` (uncapped) and `2` (MAILBOX); skip it in modes `1` and `3` where the display timing controls frame pacing

## 5. Integration with Renderer

- [ ] 5.1 Update `src/renderer/renderer.cpp` to call `SwapChain::Destroy()` + `SwapChain::Create()` when `r.vsync_mode` changes at runtime, correctly handling the HDR path and refreshing all render target views
- [ ] 5.2 Update all code in `src/renderer/` that holds raw `ID3D12Resource*` back buffer pointers to instead call `SwapChain::GetBackBuffer(index)` after any swap chain recreation event
- [ ] 5.3 Add `SwapChain::GetCurrentBackBufferIndex()` wrapping `IDXGISwapChain3::GetCurrentBackBufferIndex()` and use it in the render loop instead of a manually tracked index

## 6. Testing

- [ ] 6.1 Create `tests/renderer/test_swap_chain.cpp` using a mock DXGI factory that records `CreateSwapChainForHwnd` call parameters for verification without requiring a real window or GPU
- [ ] 6.2 Test that swap chain creation with flip model passes `DXGI_SWAP_EFFECT_FLIP_DISCARD` and 3 back buffers to the factory mock
- [ ] 6.3 Test `SwapChain::Destroy()` waits for fences: mock fence returns not-signaled on first query, signaled on second; verify `Destroy()` does not return until signaled
- [ ] 6.4 Test `RefreshBackBufferPointers()` calls `GetBuffer(0)`, `GetBuffer(1)`, `GetBuffer(2)` after recreation
- [ ] 6.5 Test VSync mode CVar range validation: setting `r.vsync_mode` to `-1` or `4` clamps/rejects and leaves the previous valid mode active
- [ ] 6.6 Test frame limiter logic: with target 60fps, verify the spin-wait loop exits within 1ms of the target frame time using a mock `QueryPerformanceCounter` that advances time linearly
- [ ] 6.7 Test VRR mode falls back to mode 1 and emits a warning when `m_tearing_supported` is false
- [ ] 6.8 Test HDR override CVar `r.hdr_output 1` forces `SetColorSpace1` to be called even when the mock monitor reports SDR capability
- [ ] 6.9 Test runtime vsync mode switch triggers swap chain destroy + recreate and refreshes back buffer pointers
- [ ] 6.10 Run coverage -- all new source files must reach >= 95% line and branch coverage before marking complete
