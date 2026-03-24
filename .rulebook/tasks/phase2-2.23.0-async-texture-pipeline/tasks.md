## Phase 0: Async Loading (zero main-thread I/O)

- [x] 0.1 PAK/VFS → AsyncTextureDecoder with SubmitFromMemory.
- [x] 0.3 Batch flush threshold 4→32.
- [x] 0.5 PreloadMaterialTextures — generic slot-based, any shading model.
- [x] 0.6 Timing instrumentation (UZ_PROFILE_SCOPE + std::chrono).
- [x] 0.7 Verified: 32 textures in 1 WaitIdle (47.5ms).
- [x] 0.8 VFS thread-safe (mutex + per-thread PAK handles) + SubmitVFS for zero main-thread I/O.

## Phase 0.5: sRGB Correctness

- [x] 0.5.1-0.5.7 Full isSRGB pipeline: TextureLoader → AsyncDecoder → UploadContext. Generic IsTextureSlotSRGB(name) on MaterialInterface. Material::srgbOverrides map. All backends verified.

## Phase 1: Cook-Time BC7 Compression

- [x] 1.1 .uztexture binary format (header + mip descriptors + TextureFormatToRHI).
- [x] 1.2 etcpak integrated (setup.ps1 + build.zig).
- [x] 1.3 TextureImporter BC compression via etcpak (BC1/BC3/BC4/BC5/BC7).
- [x] 1.4 uzcook pipeline: SelectTextureFormat heuristic, --no-compress, --textures-only.
- [x] 1.5 TextureLoader::LoadUzTex — GPU-direct upload from .uztexture.
- [x] 1.6 AsyncTextureDecoder .uztexture magic detection + passthrough.
- [x] 1.7 Format heuristic: BaseColor/COL→BC7 sRGB, Normal→BC5, Roughness/Metallic/AO→BC4, ORM→BC7 linear.
- [x] 1.8 build_common.zig cook_textures step. PBR + FBX demos enabled.
- [x] 1.9 Benchmark: 0.2ms for 42 textures (was 47.5ms). 237× faster.
- [x] 1.10 Visual verification: all materials correct. UploadContext BC block-row fix.
- [x] 1.11 Parallel uzcook via std::async — 4 textures 2K BC7 in 12s (was 120s single-threaded, 10× faster).

## Phase 2: Placeholder System

- [x] 2.1-2.2 Fallback SRV placeholders (FallbackWhite/FallbackNormal) from frame 1.
- [x] 2.3 Progressive SRV swap via PendingCopyUpload + fence finalize.
- [x] 2.4 TextureLoadState enum (Empty/Placeholder/CopyInFlight/FullyLoaded) + query API.
- [x] 2.5 Visual verification: progressive loading confirmed.

## Phase 3: Copy Queue + UploadRing + Zero-Copy Parse

- [x] 3.1-3.2 DX12 copy queue (already existed in RHI).
- [x] 3.3 Route uploads through copy queue — zero WaitIdle, fence sync.
- [x] 3.4 UploadRing (64MB/frame, triple-buffered) — zero CreateBuffer per mip.
- [x] 3.4.1 Zero-copy .uztexture parse — 0.0ms (was 37-82ms). LoadUzTexViaRing.
- [x] 3.6 Verified: 0.3ms per 2K BC7 texture (273× faster than baseline).
- [x] 3.5 Vulkan copy queue equivalent — vkGetDeviceQueue transfer-only family. Implemented SignalCopyQueue/GraphicsWaitForCopy using VkSemaphore timeline semaphores on the dedicated transfer queue.

## Phase 4: Mip Streaming Manager (future)

- [ ] 4.1 MipStreamingManager class with per-frame screen coverage calculation.
- [ ] 4.2-4.6 Priority queue, per-frame budget, mip transitions, VRAM enforcement.
- [ ] 4.7 .uztexture bulk data separation (mip 0-1 inline, 2+ in .bulk file).
- [ ] 4.8 Debug overlay (--texture-debug).

## Phase 5: Decoder Improvements

- [x] 5.1 Worker threads scale to max(2, cores/2), clamped [2,8].
- [ ] 5.2 Priority support (visible textures first).
- [ ] 5.3 Cancellation support.

## 6. Testing

- [x] 6.1 36 unit tests: .uztexture roundtrip, sRGB, TextureFormatToRHI, BC helpers. All passing.
- [ ] 6.2 Unit test: AsyncTextureDecoder::SubmitFromMemory round-trip.
- [ ] 6.3 Unit test: BC7 compression PSNR > 40dB.
- [ ] 6.7-6.10 Integration + performance + regression tests.
- [ ] 6.11 95%+ coverage on new code.

## 7. Documentation

- [ ] 7.1 docs/guides/texture-pipeline.md — cook workflow, format spec, streaming.
- [ ] 7.2 Update docs/specs/rendering/asset-pipeline.md with .uztexture.
- [ ] 7.3 docs/analysis/texture-loading-benchmarks.md — before/after.

## Summary

| Metric | Before | After |
|--------|--------|-------|
| Main thread stall | 11.6s | **0ms** |
| Per-texture upload | 82ms | **0.3ms** (273×) |
| GPU upload (WaitIdle) | 47.5ms | **0ms** (copy queue) |
| VRAM per 2K texture | 16MB (RGBA8) | **4MB** (BC7) |
| Cook time (4 tex 2K) | — | **12s** (parallel) |
| Staging allocation | 48× CreateBuffer | **0** (UploadRing) |
| Parse overhead | 82ms (double copy) | **0ms** (zero-copy) |
