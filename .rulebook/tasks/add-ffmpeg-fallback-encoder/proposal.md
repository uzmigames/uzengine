# Proposal: Add FFmpeg/libx264 Fallback Encoder

## Why
Not all systems have NVIDIA GPUs, so the PoC needs a universal fallback encoder that works on any hardware. FFmpeg's libavcodec with libx264 provides software-based H.264 encoding that runs purely on the CPU. While slower than NVENC, it guarantees the streaming pipeline works regardless of GPU vendor (AMD, Intel, or even virtual machines). The encoder factory pattern automatically selects the best available encoder at runtime: NVENC if available, FFmpeg otherwise.

## What Changes
- Implement FFmpegEncoder class implementing the IFrameEncoder interface
- Use libavcodec for H.264 encoding with libx264 codec
- Use libswscale for RGBA-to-YUV420P color space conversion (required by x264)
- Configure ultrafast preset and zerolatency tune for lowest encoding latency
- Implement EncoderFactory that tries NVENC first, falls back to FFmpeg automatically
- Add FFmpeg dependencies to vcpkg.json manifest (ffmpeg[x264,avcodec,swscale])

## Impact
- Affected specs: encoder/spec.md (modified)
- Affected code: src/encoder/ffmpeg_encoder.h, src/encoder/ffmpeg_encoder.cpp, src/encoder/encoder_factory.h, src/encoder/encoder_factory.cpp, vcpkg.json, CMakeLists.txt
- Breaking change: NO (additive - new encoder implementation + factory)
- User benefit: Streaming works on any hardware, not just NVIDIA GPUs
