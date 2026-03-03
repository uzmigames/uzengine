# Proposal: phase4-temporal-upsampling-dlss-fsr

## Why
Rendering at native 4K is extremely expensive and prevents mid-range hardware from hitting 60 fps targets. DLSS 3 (NVIDIA), FSR 3 (AMD, open source), and XeSS (Intel) each allow rendering at 50-67% resolution then upscaling to 4K with near-native visual quality, effectively doubling performance. Without temporal upsampling support, UzEngine games cannot deliver competitive performance on mid-range hardware at high resolutions. This feature is now a baseline expectation in shipped titles.

## What Changes
- DLSS 3 SDK integration via `nvsdk_ngx` for NVIDIA Ada/Ampere GPUs, feature flags gated by adapter vendor ID
- FSR 3 integration (AMD GPUOpen, MIT license) as the universal fallback path on all adapters
- XeSS SDK integration for Intel Arc GPUs
- Resolution scale factor setting in project settings (0.5 = Performance, 0.67 = Balanced, 0.77 = Quality, 1.0 = Native)
- Per-frame sub-pixel jitter offset (Halton sequence) injected into the camera projection matrix
- Screen-space motion vector buffer (`RG16F`) rendered each frame for temporal accumulation
- Automatic backend selection: query adapter LUID, pick DLSS/XeSS/FSR accordingly
- TAA pass replaced by the upsampler output; existing TAA spec updated to coexist as a fallback for unsupported hardware
- Editor UI toggle to switch quality modes at runtime without scene reload

## Impact
- Affected specs: `docs/specs/screen-space-effects.md`, `docs/specs/renderer.md`, `docs/specs/camera-system.md`
- Affected code: `src/renderer/renderer.cpp`, `src/renderer/upsampler/`, `src/camera/camera_system.cpp`, `CMakeLists.txt` (SDK fetch)
- Breaking change: NO
- User benefit: Mid-range GPU users gain near-doubling of frame rate at 1440p and 4K with minimal visual quality loss, making UzEngine games competitive with Unreal and Unity titles on the same hardware.
