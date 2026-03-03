# Proposal: phase2-frame-pacing-vsync-modes

## Why
The swap chain uses `DXGI_SWAP_EFFECT_DISCARD` (legacy blit model), which forces DXGI to copy the back buffer into the DWM composition surface every frame. This introduces one frame of display latency, causes visible tearing at frame rates above the monitor refresh rate, and is incompatible with Variable Refresh Rate (VRR / G-Sync / FreeSync) displays. The flip model (`DXGI_SWAP_EFFECT_FLIP_DISCARD`) eliminates the blit copy, halves display latency, and is required for VRR and HDR10 output — all of which are expected by players on modern hardware.

## What Changes
- Recreate the swap chain in `src/renderer/swap_chain.cpp` using `DXGI_SWAP_EFFECT_FLIP_DISCARD` with `DXGI_SCALING_NONE` and 3 back buffers
- Add `IDXGISwapChain4::SetColorSpace1` with `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020` when HDR display output is detected via `IDXGIOutput6::GetDesc1`
- Implement four VSync modes controlled by CVar `r.vsync_mode`: `0` = uncapped (no sync), `1` = VSYNC ON (`SyncInterval=1`), `2` = MAILBOX (flip with `SyncInterval=0` + frame limiter), `3` = VRR (flip with `SyncInterval=0` + `IDXGISwapChain1::SetMaximumFrameLatency(1)` + DXGI tearing flag)
- Add frame pacing using `IDXGIOutput::WaitForVBlank` in VSync mode 1 to ensure frames are delivered at exact vblank boundaries, preventing jitter caused by irregular CPU scheduling
- Add `r.frame_rate_limit` CVar (default 0 = unlimited) that uses a high-resolution spin-wait based on `QueryPerformanceCounter` to enforce a target frame time when uncapped or in MAILBOX mode
- Update the swap chain creation path to query `DXGI_FEATURE_PRESENT_ALLOW_TEARING` and enable it when VRR mode is selected

## Impact
- Affected specs: `docs/specs/renderer.md`
- Affected code: `src/renderer/swap_chain.cpp`, `src/renderer/swap_chain.h`, `src/renderer/renderer.cpp`
- Breaking change: YES — the swap chain must be fully destroyed and recreated; existing code that holds raw `ID3D12Resource*` back buffer pointers must refresh them after the mode change
- User benefit: Players on VRR monitors get tear-free adaptive sync; all players get lower display latency from flip model; developers can lock to 30/60fps for testing with `r.frame_rate_limit`
