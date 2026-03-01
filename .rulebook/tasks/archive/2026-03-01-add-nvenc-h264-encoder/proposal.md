# Proposal: Add NVENC H.264 Hardware Encoder

## Why
For low-latency real-time streaming of GPU-rendered frames to VSCode, hardware encoding is essential. NVIDIA's NVENC provides hardware-accelerated H.264 encoding directly on the GPU, delivering sub-millisecond encoding times with minimal CPU overhead. On the target RTX 4090 GPU, NVENC can encode 720p frames at hundreds of FPS. This is the primary encoding path for the PoC, with FFmpeg/libx264 as a fallback for non-NVIDIA hardware. The encoder subsystem uses a strategy pattern (IFrameEncoder interface) to allow transparent switching between encoding backends.

## What Changes
- Define the IFrameEncoder abstract interface for all encoder implementations
- Download and integrate NVIDIA Video Codec SDK headers (nvEncodeAPI.h)
- Implement NvEncEncoder class that dynamically loads nvEncodeAPI64.dll at runtime
- Configure H.264 Baseline profile, CBR 4 Mbps, no B-frames, low-latency preset
- Accept RGBA input directly (NVENC handles internal color conversion)
- Output raw H.264 NAL units (SPS, PPS, IDR, P-frames) in Annex B format
- Implement static availability check (isAvailable()) for runtime detection
- Add CMake option UZENGINE_ENABLE_NVENC with SDK path configuration

## Impact
- Affected specs: encoder/spec.md (new)
- Affected code: src/encoder/encoder.h, src/encoder/nvenc_encoder.h, src/encoder/nvenc_encoder.cpp, CMakeLists.txt
- Breaking change: NO (new module, no existing code modified)
- User benefit: Hardware-accelerated H.264 encoding with minimal latency for real-time streaming
