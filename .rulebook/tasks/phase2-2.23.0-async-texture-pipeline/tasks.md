## Phase 0: Fix Immediate Bottlenecks (zero-stall async for existing PNG/JPG)

- [x] 0.1 Route PAK/VFS texture loads through `AsyncTextureDecoder` — Added `DecodeRequest` struct (path + memoryData) to replace `std::deque<std::string>` queue. Added `SubmitFromMemory(vector<uint8_t>&&, path)` method. WorkerLoop branches on memoryData: non-empty uses `stbi_load_from_memory()`, empty uses `stbi_load()`. PAK branch in `LoadTexture()` now calls `SubmitFromMemory()` with sync fallback.
- [ ] 0.2 Remove per-texture `WaitIdle()` from `TextureLoader::UploadPixels()` — `texture_loader.cpp:419`. Replace with deferred upload path (`UploadPixelsDeferred`). The sync `Load()`/`LoadFromMemory()` methods should use deferred internally and let the caller decide when to flush. (Note: PAK path now bypasses this entirely via async, so lower priority.)
- [x] 0.3 Increase batch flush threshold from 4 to 32 — Changed `kFlushThreshold` from 4 to 32 in `scene_assets.cpp`. Reduces GPU submissions from 12 to 2 for 47 textures.
- [ ] 0.4 Add `FlushAllPending()` convenience method to `SceneAssets` — after all textures submitted, single `End+Execute+WaitIdle` to upload everything in one GPU batch.
- [x] 0.5 Preload materials — added `SceneRenderer::PreloadMaterialTextures(mat, vfs, cmdList)` that iterates all known slot names and submits to async decoder. Generic slot-based approach, works for any shading model.
- [x] 0.6 Add timing instrumentation — Added `UZ_PROFILE_SCOPE` for Meshes, TextureUpload, and BatchFlush sections in `FlushPendingLoads()`. Both batch flush paths (normal + final) now log wall-clock time in milliseconds via `std::chrono::high_resolution_clock`.
- [x] 0.7 Build + measure: PBR demo verified — 32 textures in 1 WaitIdle (47.5ms), down from 8+ WaitIdle calls. VFS textures now route through async decoder (confirmed via log: "submitted async texture decode (VFS)").
- [x] 0.8 Make VFS thread-safe + add SubmitVFS() for zero main-thread I/O — Added `std::mutex` to PakVFS and LooseFileVFS (protects Read/Exists/GetCacheStats). Added `AsyncTextureDecoder::SubmitVFS(IVFS*, path, isSRGB)` that enqueues a VFS logical path; worker thread calls `vfs->Read()` on background thread. LoadTexture() PAK fallback now uses SubmitVFS() instead of main-thread `vfs->Read()` + `SubmitFromMemory()`. PakVFS move ops deleted (mutex not movable; class is always heap-allocated via unique_ptr).

## Phase 0.5: sRGB-Aware Texture Creation (correctness fix)

- [x] 0.5.1 Add `bool isSRGB` parameter to `TextureLoader::Load()`, `LoadFromMemory()`, `UploadPixels()`, `UploadPixelsDeferred()`, `UploadPixelsWithMipsDeferred()` — when true, create texture with `RHIFormat::RGBA8Srgb` instead of `RGBA8Unorm`. Added `bool isSRGB = false` to all 5 public methods + both internal `Core` functions. Format selection: `isSRGB ? RGBA8Srgb : RGBA8Unorm`.
- [x] 0.5.2 Add `bool isSRGB` to `AsyncTextureDecoder::Submit()` and `SubmitFromMemory()` — propagated through `DecodeRequest.isSRGB` -> `DecodeResult.isSRGB` in WorkerLoop, so FlushPendingLoads passes it to upload functions.
- [x] 0.5.3 Refactored sRGB to generic slot-based system — `MaterialInterface::IsTextureSlotSRGB(name)` + `DefaultSlotIsSRGB(name)` static helper. Resolves by slot name convention (Albedo/Emissive/ScatterColor/FuzzColor → sRGB; everything else → linear). Works for PBR, Hair, Cloth, SSS, etc without changing the interface.
- [x] 0.5.4 scene_sync.cpp uses `mat->IsTextureSlotSRGB(slotName)` instead of hardcoded true/false. Lambda now takes `(slotName, path)`.
- [x] 0.5.5 Material asset supports per-slot sRGB override — `Material::srgbOverrides` (unordered_map<string,bool>). `Material::IsTextureSlotSRGB()` checks override map first, falls back to `DefaultSlotIsSRGB()`. Settable via `SetTextureSlotSRGB(slot, bool)`. Ready for .uzmat JSON parser integration.
- [x] 0.5.6 Verify RHI backends handle `RGBA8Srgb` correctly — DX12 confirmed: `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` mapped in all 3 backends (dx12, dx11, vulkan). PBR demo runs without errors. Albedo textures create with fmt=4 (RGBA8Srgb), linear textures with fmt=3 (RGBA8Unorm).
- [x] 0.5.7 Build + visual verification — PBR demo log confirms: checker.png→fmt=4(sRGB), _BaseColor/_COL→fmt=4(sRGB), _Normal→fmt=3(linear), _Roughness→fmt=3(linear), _Metallic→fmt=3(linear), _AO→fmt=3(linear). ORM stays fmt=3 (no double-gamma).
- [ ] 0.5.8 Unit test: load same PNG as sRGB and linear, verify GPU texture format differs. Load ORM as linear, verify no gamma shift on readback.

## Phase 1: Cook-Time BC7 Compression (.uztex format)

- [x] 1.1 Define `.uztex` binary format — Already implemented as `.uztexture` in `engine/asset/formats/uztexture_format.h`: 64-byte header (magic UZTX, version, w/h, mipCount, format enum BC1/BC3/BC4/BC5/BC7/RGBA8, flags with sRGB bit), 24-byte mip descriptors (offset/size/w/h), contiguous mip data largest first. ReadUzTexture()/WriteUzTexture() serialization.
- [x] 1.2 Add `etcpak` to `scripts/setup.ps1` — Clone-Repo added, setup.ps1 run, thirdparty/etcpak/ populated. README.md updated with entry.
- [x] 1.3 BC1/BC3/BC4/BC5/BC7 GPU texture compression via etcpak — Added `CompressMipToBC()` helper and `textureCompression`/`textureSRGB` fields to `AssetImportOptions`. `TextureImporter::Import()` compresses each mip level when a BC format is requested. etcpak sources (ProcessDxtc.cpp, bc7enc.cpp, Tables.cpp, Dither.cpp) integrated into build.zig. Version bumped to 2.
- [x] 1.4 Integrate into `uzcook` pipeline — Added `SelectTextureFormat()` heuristic to `uzcook.cpp` that auto-selects BC format from filename conventions (albedo→BC7 sRGB, normal→BC5, roughness/metallic/AO→BC4, ORM→BC7 linear). Added `--no-compress` CLI flag. Cache key includes compression options for invalidation. Sidecar records format+sRGB.
- [x] 1.5 Implement `TextureLoader::LoadUzTex()` — Parses .uztexture via ReadUzTexture(), converts TextureFormat+sRGB flag to RHIFormat via TextureFormatToRHI(), creates GPU texture with exact format/mips, uploads each mip blob directly via UploadContext, transitions to ShaderResource. Supports both sync (null outUpload) and deferred (batched) paths.
- [x] 1.6 Update `AsyncTextureDecoder` to support `.uztex` — Worker thread detects .uztexture magic (0x55 0x54 0x58 0x5A) in both memory and file-path modes, passes raw bytes through as `uzTexData` field in TextureDecodeResult (no stbi_load). FlushPendingLoads in scene_assets.cpp routes uzTexData to LoadUzTex() before the normal RGBA8 upload path.
- [x] 1.7 Add format selection heuristic — Implemented `SelectTextureFormat()` in `uzcook.cpp`: albedo/basecolor/diffuse/emissive→BC7 sRGB, normal/nrm/norm→BC5 linear, roughness/metallic/AO/opacity→BC4 linear, ORM packed→BC7 linear. Case-insensitive filename matching with extension stripping. Default fallback: BC7 sRGB.
- [x] 1.8 PBR demo cooks textures at build — `cook_textures: true` in build.zig. Added `cookTextures()` to build_common.zig (runs uzcook per asset_dir before pack). DXC init made optional. 157 files packed (originals + .uztexture).
- [x] 1.9 Benchmark: 42 BC7/BC5/BC4 textures uploaded in 0.2ms total (was 47.5ms for 32 RGBA8 textures = 237× improvement). Zero stbi_load, zero CPU mip gen.
- [x] 1.10 Visual verification: PBR demo screenshot with BC textures — all materials render correctly (gold, rattan, quartzite, grass, concrete, metal corroded). Fixed UploadContext to handle BC block row pitch (blocksX×blockBytes, numRows=blocksY).

## Phase 2: Placeholder Mip System (instant scene visibility)

- [ ] 2.1 Embed mip 0 (smallest mip, e.g. 16×16 or 32×32) inline in `.uztex` header — always loaded with asset metadata, zero async latency. Size: 256B-4KB per texture (BC7 compressed).
- [ ] 2.2 Create placeholder SRV on first `LoadTexture()` call — immediately create a GPU texture from embedded mip 0, return valid SRV index. Scene renders with blurry but correctly colored textures.
- [ ] 2.3 Implement progressive mip swap — when higher mips finish async loading, create full texture with all mips, swap SRV index in `m_textures[]` array. Draw commands automatically use new SRV next frame.
- [ ] 2.4 Add `TextureLoadState` enum: `Placeholder`, `Streaming`, `FullyLoaded`. Queryable per texture for UI/debug.
- [ ] 2.5 Visual verification: capture screenshot at frame 1 (placeholders visible), frame 5, frame 15 (full quality). Verify progressive sharpening.

## Phase 3: Copy Queue (non-blocking GPU upload)

- [x] 3.1 Create dedicated DX12 copy queue in RHI — Already existed: `IRHIDevice::CreateCommandList(RHIQueueType::Copy)`, `SignalCopyQueue()`, `GraphicsWaitForCopy()` with `m_copyQueue`, `m_copyFence`, `m_copyFenceValue` in DX12 backend.
- [x] 3.2 Add `RHICopyQueue` abstraction to `rhi.h` — Already existed: `SignalCopyQueue()` and `GraphicsWaitForCopy(value)` virtual methods on `IRHIDevice`. `StagingBatch` class uses copy queue for batched uploads.
- [x] 3.3 Route all texture uploads through copy queue — Added `m_copyCmdList` (copy queue command list), `m_copyFenceValue`, `PendingCopyUpload` struct, `m_pendingCopyUploads` to SceneRenderer. FlushPendingLoads records uploads on `m_copyCmdList` with `skipBarrier=true`, submits via `SignalCopyQueue()`. Next frame: `GraphicsWaitForCopy()` + barriers on graphics queue + SRV slot update. Added `skipBarrier` parameter to `UploadPixelsDeferred`, `UploadPixelsWithMipsDeferred`, `LoadUzTex`. Zero WaitIdle during texture loading. Fallback to graphics queue if copy queue unavailable.
- [x] 3.3.1 1x1 placeholder textures for loading slots — LoadTexture() and QueueTextureLoad() now initialize texture slots with fallback SRV indices (white for color maps, flat normal for normal maps) so draw commands have valid descriptors from the first frame. Uses existing FallbackWhite/FallbackNormal textures.
- [ ] 3.4 Implement double-buffer staging — two staging heaps alternate per frame. Frame N uploads via staging A while frame N-1's staging B is still in flight on copy queue.
- [ ] 3.5 Add Vulkan copy queue equivalent — `vkGetDeviceQueue` with transfer-only queue family.
- [x] 3.6 Build + measure: verify zero graphics queue stall during texture upload — PBR demo confirmed: zero WaitIdle calls during loading, copy queue submissions with fence signaling, textures finalized next frame. Visual output identical (PBR spheres with all materials correct).

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
