# Proposal: Async Texture Pipeline — Zero-Stall Loading, Cook-Time Compression & Mip Streaming (Phase 2)

## Why

UzEngine's PBR demo takes 1.4+ seconds to load 47 textures, completely blocking the main thread. The root causes are:

1. **PAK/VFS mode is 100% synchronous** — `scene_assets.cpp:153-170` calls `TextureLoader::LoadFromMemory()` which does LZ4 decompress + stbi_load + CPU mip gen + GPU upload + `WaitIdle()` **per texture** on the main thread. 47 textures × ~30ms = 1.4s stall.

2. **`WaitIdle()` per texture** — `texture_loader.cpp:419` calls `device->WaitIdle()` after each individual texture upload, causing 47 sequential GPU stalls instead of a single batched submit.

3. **Batch flush threshold too low** — `scene_assets.cpp:504` has `kFlushThreshold = 4`, causing 12 separate GPU submissions + WaitIdle calls for 47 textures instead of 1.

4. **Runtime PNG/JPG decode** — All textures stored as PNG/JPG, requiring `stbi_load` CPU decode (5-15ms per 2K texture). Industry standard is cook-time compression to BC7/DDS where runtime cost is a memcpy (~0.1ms).

5. **No mip streaming** — All mip levels loaded at once. UE5 loads mip 0 (tiny placeholder) instantly and streams higher mips async. UzEngine has no equivalent.

6. **No placeholder textures** — Scene is invisible until ALL textures finish loading. UE5/Unity show blurry versions immediately and progressively sharpen.

The async decode infrastructure (AsyncTextureDecoder with worker threads + SIMD mip gen) already exists but is **only used in dev mode**. PAK mode bypasses it entirely. This is the single largest UX problem for any scene with more than 10 textures.

**Reference engines**:
- UE5: Cook → .uasset (BC7 + pre-baked mips + bulk data separation). Runtime: mip 0 instant, streaming manager loads higher mips per-frame based on screen coverage + VRAM budget.
- Unity: Cook → compressed (DXT5/BC7/ETC2). Runtime: AsyncTextureUpload DMA, quality tiers, mip streaming with budget.
- Godot 4: Import → .ctex (S3TC/BPTC + pre-baked mips). Runtime: async ResourceLoader. Streaming infrastructure exists but loop not implemented.

## What Changes

### New Systems

1. **Cook-time texture compression pipeline** — `uzcook` converts PNG/JPG → `.uztex` format (BC7 GPU-compressed + pre-baked mip chain). Runtime loads GPU-ready blocks with zero CPU decode.
2. **Unified async texture load** — All paths (dev mode, PAK mode, loose files) route through `AsyncTextureDecoder`. Zero synchronous texture loads outside of explicit `LoadBlocking()`.
3. **Mip streaming manager** — Per-frame loop: calculate required mip level per texture (screen coverage), queue async mip loads, upload via copy queue, swap SRV. VRAM budget enforcement.
4. **Placeholder mip system** — Mip 0 (16×16 or 32×32) embedded in asset header, loaded instantly. Scene visible in <50ms. Higher mips streamed progressively.
5. **Copy queue texture upload** — Dedicated DX12 copy queue (or equivalent) for texture uploads that does not stall the graphics queue.
6. **`.uztex` asset format** — Binary format: header (dims, format, mip count, mip offsets) + mip 0 inline + mips 1-N in bulk section. Supports BC1/BC3/BC7/R8/RGBA8.

### Refactoring

1. `scene_assets.cpp` PAK branch (lines 153-170) → route through async decoder instead of sync `LoadFromMemory()`.
2. `texture_loader.cpp` `WaitIdle()` per texture (line 419) → removed, replaced by batched fence.
3. `scene_assets.cpp` `kFlushThreshold` (line 504) → increased to 32 or dynamic.
4. `TextureLoader::Load()` and `LoadFromMemory()` sync paths → deprecated, replaced by `LoadAsync()`.

## Impact

- Affected specs: `docs/specs/rendering/asset-pipeline.md`, `docs/specs/rendering/ibl-pipeline.md`
- Affected code: `runtime/engine/renderer/upload/texture_loader.h/.cpp`, `runtime/engine/renderer/upload/async_texture_decoder.h/.cpp`, `runtime/engine/renderer/scene/scene_assets.cpp`, `runtime/engine/renderer/scene/scene_sync.cpp`, `runtime/tools/uzcook.cpp`
- Breaking change: NO — `.uztex` is a new format alongside existing PNG/JPG support. Async path is transparent to callers. Fallback to sync path preserved for debugging.
- User benefit: Scene visible in <100ms (placeholder mips). Full quality in 1-3 seconds (streamed async). Zero main thread stall. 10× faster texture loading.
- Enables: Large open-world scenes (1000+ textures), real-time editor with asset hot-reload, level streaming, LOD cross-fade without hitch.
