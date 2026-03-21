## Phase 0: Fix Immediate Bottlenecks (zero-stall async for existing PNG/JPG)

- [ ] 0.1 Route PAK/VFS texture loads through `AsyncTextureDecoder` — in `scene_assets.cpp:153-170`, replace synchronous `TextureLoader::LoadFromMemory()` with: VFS read → memory buffer → `AsyncTextureDecoder::SubmitFromMemory(buffer, size, path)`. New method on AsyncTextureDecoder that accepts pre-read bytes instead of file path.
- [ ] 0.2 Remove per-texture `WaitIdle()` from `TextureLoader::UploadPixels()` — `texture_loader.cpp:419`. Replace with deferred upload path (`UploadPixelsDeferred`). The sync `Load()`/`LoadFromMemory()` methods should use deferred internally and let the caller decide when to flush.
- [ ] 0.3 Increase batch flush threshold from 4 to 32 — `scene_assets.cpp:504` change `kFlushThreshold = 4` to `kFlushThreshold = 32`. Reduces GPU submissions from 12 to 2 for 47 textures.
- [ ] 0.4 Add `FlushAllPending()` convenience method to `SceneAssets` — after all textures submitted, single `End+Execute+WaitIdle` to upload everything in one GPU batch.
- [ ] 0.5 Preload materials during `OnStart()` instead of lazy-load in `SyncWorldToDrawList()` — `scene_sync.cpp:68-84`. Call `PreloadMaterialTextures(material)` which submits all texture paths to async decoder immediately, before the first render frame.
- [ ] 0.6 Add timing instrumentation — `UZ_PROFILE_SCOPE("TextureLoad")` around decode, mip gen, and GPU upload stages. Log total scene load time.
- [ ] 0.7 Build + measure: compare load time before/after on PBR demo (47 textures). Target: <200ms total stall (down from 1.4s).

## Phase 0.5: sRGB-Aware Texture Creation (correctness fix)

- [ ] 0.5.1 Add `bool isSRGB` parameter to `TextureLoader::Load()`, `LoadFromMemory()`, `UploadPixels()`, `UploadPixelsDeferred()`, `UploadPixelsWithMipsDeferred()` — when true, create texture with `RHIFormat::RGBA8Srgb` instead of `RGBA8Unorm`. Currently hardcoded to `RGBA8Unorm` at `texture_loader.cpp:215` and `:340`.
- [ ] 0.5.2 Add `bool isSRGB` to `AsyncTextureDecoder::Submit()` and `SubmitFromMemory()` — propagated through `DecodeResult` so the upload path knows which format to use.
- [ ] 0.5.3 Add `TextureSlotType` enum or per-slot sRGB flag to material system — albedo, emissive, and detail albedo are sRGB; normal, roughness, metallic, AO, ORM, heightmap, opacity, thickness are linear. Determined by slot name in `LoadTexture()` call site (`scene_assets.cpp`).
- [ ] 0.5.4 Update `scene_assets.cpp` `LoadTexture()` to pass `isSRGB` based on texture slot — add second parameter: `LoadTexture(path, vfs, cmdList, isSRGB)`. Scene sync calls it with `isSRGB=true` for albedo/emissive, `isSRGB=false` for normal/roughness/metallic/AO/ORM.
- [ ] 0.5.5 Add `isSRGB` field to `.uzmat` per-texture slot — allows artist override. Default: auto-detected from slot name. Example: `"textures": { "albedo": { "path": "gold_basecolor.jpg", "srgb": true } }`.
- [ ] 0.5.6 Verify RHI backends handle `RGBA8Srgb` correctly — DX12 (`DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`), Vulkan (`VK_FORMAT_R8G8B8A8_SRGB`), DX11 (`DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`) all already mapped in `rhi_types.h:140`. Metal needs verification.
- [ ] 0.5.7 Build + visual verification: A/B screenshot comparison. Albedo sRGB vs Unorm — sRGB should produce brighter, more saturated colors (correct). ORM must remain Unorm (no double-gamma).
- [ ] 0.5.8 Unit test: load same PNG as sRGB and linear, verify GPU texture format differs. Load ORM as linear, verify no gamma shift on readback.

## Phase 1: Cook-Time BC7 Compression (.uztex format)

- [ ] 1.1 Define `.uztex` binary format — header: magic (`UZTX`), version (1), width, height, format (enum: BC1/BC3/BC7/RGBA8/R8), mipCount, mip0Offset, mipOffsets[16], flags (sRGB, normalMap, streaming). Body: mip data contiguous, largest first.
- [ ] 1.2 Add `ispc_texcomp` (Intel ISPC Texture Compressor) to `scripts/setup.ps1` — high-quality BC7 compression, 10-50× faster than reference encoder. Alternative: `bc7enc_rdo` (single header, no ISPC dependency).
- [ ] 1.3 Implement `TextureCompressor` in `runtime/tools/texture_compressor.h/.cpp` — `Compress(pixels, w, h, format) → CompressedResult{blocks, size, mipChain[]}`. Supports BC1 (1-channel: roughness, metallic, AO), BC3 (RGBA with alpha), BC7 (high quality albedo, normal).
- [ ] 1.4 Integrate into `uzcook` pipeline — for each PNG/JPG texture referenced by `.uzmat`: decode via stbi → generate mip chain → compress each mip to BC7 → write `.uztex`. Normal maps use BC5 (RG) or BC7 with special quality settings.
- [ ] 1.5 Implement `TextureLoader::LoadUzTex()` — read `.uztex` header, mmap or read mip blocks, upload GPU-ready data directly via `CreateTexture(format=BC7)` + `CopyBufferToTexture()`. Zero stbi_load, zero CPU mip gen.
- [ ] 1.6 Update `AsyncTextureDecoder` to support `.uztex` — worker thread reads file, parses header, returns mip blocks as-is (no decode needed). Upload path uses `UploadCompressedDeferred()`.
- [ ] 1.7 Add format selection heuristic — albedo/emissive → BC7 (sRGB), normal → BC5 (RG16), roughness/metallic/AO → BC4 (R8) or BC1, opacity → BC4 (R8). Configurable per-texture in `.uzmat` or auto-detected.
- [ ] 1.8 Update PBR demo build to cook textures — `demos/pbr_test/build.zig` runs `uzcook --textures` before linking demo. Assets deployed as `.uztex`.
- [ ] 1.9 Build + measure: compare decode time PNG vs BC7. Target: <0.5ms per texture (down from 15ms).
- [ ] 1.10 Visual verification: A/B screenshot comparison PNG vs BC7 — PSNR > 40dB (visually identical).

## Phase 2: Placeholder Mip System (instant scene visibility)

- [ ] 2.1 Embed mip 0 (smallest mip, e.g. 16×16 or 32×32) inline in `.uztex` header — always loaded with asset metadata, zero async latency. Size: 256B-4KB per texture (BC7 compressed).
- [ ] 2.2 Create placeholder SRV on first `LoadTexture()` call — immediately create a GPU texture from embedded mip 0, return valid SRV index. Scene renders with blurry but correctly colored textures.
- [ ] 2.3 Implement progressive mip swap — when higher mips finish async loading, create full texture with all mips, swap SRV index in `m_textures[]` array. Draw commands automatically use new SRV next frame.
- [ ] 2.4 Add `TextureLoadState` enum: `Placeholder`, `Streaming`, `FullyLoaded`. Queryable per texture for UI/debug.
- [ ] 2.5 Visual verification: capture screenshot at frame 1 (placeholders visible), frame 5, frame 15 (full quality). Verify progressive sharpening.

## Phase 3: Copy Queue (non-blocking GPU upload)

- [ ] 3.1 Create dedicated DX12 copy queue in RHI — `rhi_dx12.c`: `ID3D12CommandQueue` (type COPY), dedicated command allocator + command list. Fence for completion tracking.
- [ ] 3.2 Add `RHICopyQueue` abstraction to `rhi.h` — `BeginCopyBatch()`, `CopyBufferToTexture()`, `EndCopyBatch()`, `IsCopyComplete(fenceValue)`. Does NOT stall graphics queue.
- [ ] 3.3 Route all texture uploads through copy queue — `scene_assets.cpp` `FlushPendingLoads()` uses copy queue instead of graphics command list. Graphics queue only waits on copy fence when textures are needed (next frame).
- [ ] 3.4 Implement double-buffer staging — two staging heaps alternate per frame. Frame N uploads via staging A while frame N-1's staging B is still in flight on copy queue.
- [ ] 3.5 Add Vulkan copy queue equivalent — `vkGetDeviceQueue` with transfer-only queue family.
- [ ] 3.6 Build + measure: verify zero graphics queue stall during texture upload. GPU profile shows no gaps in render timeline.

## Phase 4: Mip Streaming Manager

- [ ] 4.1 Create `MipStreamingManager` class — per-frame update loop: for each loaded texture, calculate required mip level based on screen coverage (texels-per-pixel from camera distance + UV density).
- [ ] 4.2 Implement screen coverage calculation — `requiredMip = log2(textureSize / screenPixels)` where `screenPixels = objectScreenArea * uvDensity`. Clamp to [0, maxMip].
- [ ] 4.3 Implement mip request queue — priority queue sorted by (currentMip - requiredMip) descending. Textures with largest quality gap get loaded first.
- [ ] 4.4 Implement per-frame mip budget — limit async mip loads to N MB/frame (default 16MB). Prevents IO saturation.
- [ ] 4.5 Implement mip level transition — when higher mip arrives, create new texture with updated mip range, swap SRV. Old texture freed after GPU fence confirms no in-flight references.
- [ ] 4.6 Implement VRAM budget enforcement — track total texture VRAM. When exceeding budget (configurable, default 1GB), force distant/occluded textures to lower mips. LRU eviction.
- [ ] 4.7 Implement `.uztex` bulk data separation — mip 0-1 inline (always loaded), mips 2+ in separate `.uztex.bulk` file. Streaming manager opens bulk file on demand.
- [ ] 4.8 Add debug overlay — `--texture-debug` shows per-texture: name, current mip, required mip, VRAM usage, streaming state. Color-coded (red=under-res, green=optimal, blue=over-res).
- [ ] 4.9 Build + visual verification: fly camera from far to near, observe textures progressively sharpening. No pops, no stalls.

## Phase 5: Async Texture Decoder Improvements

- [ ] 5.1 Increase default worker threads from 2 to `max(2, std::thread::hardware_concurrency() / 2)` — scale with CPU cores.
- [ ] 5.2 Add priority support to async decoder — `Submit(path, priority)`. Visible textures decode first, off-screen textures decode later.
- [ ] 5.3 Add cancellation support — `Cancel(handle)`. When camera moves, cancel pending loads for textures no longer needed.
- [ ] 5.4 Add memory pool for decoded pixel buffers — avoid per-texture malloc/free. Pre-allocate ring buffer sized for max concurrent decodes.
- [ ] 5.5 Add IO coalescing — if multiple textures from same PAK block, read the block once and split in memory.

## 6. Testing

- [ ] 6.1 Unit test: `.uztex` format write/read round-trip — header fields, mip data integrity, BC7 block alignment.
- [ ] 6.2 Unit test: `AsyncTextureDecoder::SubmitFromMemory()` — decode from memory buffer produces same result as file path.
- [ ] 6.3 Unit test: BC7 compression quality — compress reference image, decompress, compare PSNR > 40dB.
- [ ] 6.4 Unit test: `MipStreamingManager` screen coverage calculation — known camera setup produces expected mip levels.
- [ ] 6.5 Unit test: VRAM budget enforcement — exceed budget, verify lowest-priority textures downgraded.
- [ ] 6.6 Unit test: placeholder mip creation — load `.uztex`, verify mip 0 SRV created before full decode.
- [ ] 6.7 Integration test: PBR demo loads 47 textures with zero main-thread stall (measure via profiler).
- [ ] 6.8 Integration test: copy queue upload completes without graphics queue stall (fence timing).
- [ ] 6.9 Performance test: compare load times — baseline (current) vs Phase 0 (async fix) vs Phase 1 (BC7) vs Phase 2 (placeholder).
- [ ] 6.10 Regression test: visual output identical before/after (screenshot PSNR > 40dB).
- [ ] 6.11 Verify 95%+ coverage on all new code.

## 7. Documentation

- [ ] 7.1 Create `docs/guides/texture-pipeline.md` — cook workflow (PNG → .uztex), format spec, compression options, streaming behavior.
- [ ] 7.2 Update `docs/specs/rendering/asset-pipeline.md` with `.uztex` format specification.
- [ ] 7.3 Create `docs/analysis/texture-loading-benchmarks.md` — before/after measurements for each phase.
- [ ] 7.4 Update `docs/engine/08-resource-system/` with async texture loading architecture.
- [ ] 7.5 Document `--texture-debug` overlay and streaming manager console commands.
