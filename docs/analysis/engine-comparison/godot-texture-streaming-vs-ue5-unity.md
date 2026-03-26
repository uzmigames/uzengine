<!-- doc-meta
title: Godot 4 Texture Loading & Streaming vs UE5 & Unity
slug: godot-texture-streaming-analysis
description: Deep dive into how Godot 4 handles texture import, runtime loading, streaming, and memory management, compared to UE5 and Unity
author: Claude
date: 2026-03-21
category: rendering
tags: [godot, ue5, unity, textures, streaming, memory, gpu]
-->

# Godot 4 Texture Loading & Streaming Analysis

**Comparison**: Godot 4 vs Unreal Engine 5 vs Unity

---

## Executive Summary

| Aspect | Godot 4 | UE5 | Unity |
|--------|---------|-----|-------|
| **Streaming** | **No mip streaming yet** (marked TODO) | Full mip streaming + virtual textures | Texture streaming with resolution selection |
| **Import Format** | `.ctex` (binary) + all mips stored | `.uasset` + bulk data separation | `.asset` + all mips in one file |
| **Compression** | 5 modes (Lossless/Lossy/VRAM/Basis/Uncompressed) | DXT/BCn platform-specific | DXT/ETC/ASTC platform-specific |
| **Async Loading** | ResourceLoader with thread pool (`LoadToken` + `ThreadLoadTask`) | FAsyncLoadingSingleton + priority queue | AsyncGPUReadback + streaming budget |
| **Placeholder** | None (loads fully or fails) | Default white/fallback texture | Compressed mip 0 shown during streaming |
| **VRAM Eviction** | None (manual memory management) | Real-time mip streaming + priority | Texture quality tiers (16x down to 1/4) |
| **GPU Upload** | Immediate on load | Deferred (render frame boundary) | Async GPU upload pipeline |

---

## Part 1: Godot 4 Texture Pipeline

### 1.1 Import Time: PNG → .ctex

**File**: `editor/import/resource_importer_texture.cpp:701`
**Function**: `Error ResourceImporterTexture::import(...)`

#### What Happens at Import

When you import a PNG into Godot:

1. **Load source image** (line 790)
   ```cpp
   Error err = ImageLoader::load_image(p_source_file, image, nullptr, loader_flags, scale);
   ```
   - Loads PNG/JPG/WebP/EXR/HDR as raw `Image` (CPU memory)
   - Decodes to uncompressed format (RGBA8, RGB8, etc.)

2. **Apply preprocessing** (lines 822-880)
   - **Size limiting** (line 824): downsample if texture > `size_limit` (default 32768)
   - **Channel remapping** (line 858): remap R/G/B/A channels
   - **Alpha border fix** (line 863): extend alpha edges to prevent filtering artifacts
   - **Premultiply alpha** (line 868)
   - **Normal map Y invert** (line 873)
   - **HDR exposure clamping** (line 878)

3. **Generate mipmaps** (lines 402-413)
   ```cpp
   if (p_mipmaps) {
       if (!image->has_mipmaps() || p_force_normal) {
           image->generate_mipmaps(p_force_normal);
       }
   }
   ```
   - **Timing**: IMPORT TIME (offline, not at runtime)
   - **Method**: Gaussian filter on CPU (standard approach)
   - **Stored**: ALL mips packed into single `.ctex` file

4. **Detect texture usage** (lines 882-886)
   - **3D detection** (line 882): Auto-enable mips + VRAM compression if used in 3D
   - **Roughness detection** (line 883): Generate roughness-aware mips from normal
   - **Normal detection** (line 884): Use red-green (RG) compression for normals

5. **Choose compression mode** (line 706)
   - **COMPRESS_LOSSLESS**: PNG/WebP lossless inside `.ctex`
   - **COMPRESS_LOSSY**: WebP lossy inside `.ctex`
   - **COMPRESS_VRAM_COMPRESSED**: GPU-native format (S3TC/ETC2/ASTC/BPTC)
   - **COMPRESS_VRAM_UNCOMPRESSED**: Raw format (RGBA8, RG16F, etc.)
   - **COMPRESS_BASIS_UNIVERSAL**: Basis Universal (UASTC or ETC1S) for cross-platform

6. **Compress to GPU format** (lines 317-326)
   ```cpp
   case COMPRESS_VRAM_COMPRESSED: {
       Ref<Image> image = p_image->duplicate();
       image->compress_from_channels(p_compress_format, p_channels);
       // p_compress_format = S3TC, BPTC, ETC2, ASTC, etc.
       f->store_32(CompressedTexture2D::DATA_FORMAT_IMAGE);
       // Store all mips as GPU-ready data
       f->store_buffer(image->get_data());
   }
   ```
   - **Vulkan** uses ASTC (best compression on mobile)
   - **D3D11/12** uses BC7 (BPTC) or BC3 (S3TC for DXT5)
   - **OpenGL** uses S3TC/DXT5 (BC3)
   - Basis Universal is recommended for cross-platform (encodes once, decodes as ETC1S on mobile, BC7 on desktop)

7. **Save to .ctex** (lines 354-435)
   ```cpp
   void ResourceImporterTexture::_save_ctex(
       const Ref<Image> &p_image,  // Image with all mipmaps
       const String &p_to_path,    // e.g., "res://textures/diffuse.ctex"
       CompressMode p_compress_mode
   )
   ```
   - **Header** (line 359-394)
     - Magic: `GST2` (Godot Streamable Texture 2D)
     - Version: 1
     - Width/Height
     - Flags (HAS_MIPMAPS, DETECT_3D, DETECT_NORMAL, DETECT_ROUGHNESS)
     - Mipmap limit (highest res mip index)

   - **Per-compression format** (lines 274-352):
     - **PNG/WebP**: Store each mip separately as PNG/WebP blob
       ```cpp
       for (int i = 0; i < p_image->get_mipmap_count() + 1; i++) {
           Vector<uint8_t> data = Image::png_packer(i ? p_image->get_image_from_mipmap(i) : p_image);
           f->store_32(data_size);  // Size of this mip
           f->store_buffer(data.ptr(), data_size);  // PNG/WebP bytes
       }
       ```
       - **Advantage**: Highly compressible on disk
       - **Disadvantage**: Must decompress at load time (slow)

     - **VRAM_COMPRESSED**: Store GPU-native format
       ```cpp
       image->compress_from_channels(p_compress_format, p_channels);
       f->store_32(CompressedTexture2D::DATA_FORMAT_IMAGE);
       f->store_buffer(image->get_data());  // All mips, GPU-ready
       ```
       - **Advantage**: No decompression (instant GPU upload)
       - **Disadvantage**: Larger file size (no CPU compression)

     - **BASIS_UNIVERSAL**: Basis Universal codec
       - Cross-platform compression (single import, multiple decodes)
       - Stores ETC1S stream that decodes to platform-specific format at runtime

#### .ctex File Structure

```
[Magic: 'GST2'] (4 bytes)
[Version: 1] (4 bytes)
[Width] (4 bytes)
[Height] (4 bytes)
[Flags: HAS_MIPMAPS|DETECT_3D|DETECT_NORMAL] (4 bytes)
[Mipmap limit] (4 bytes)
[Reserved] (12 bytes)
[Data Format: IMAGE|PNG|WEBP|BASIS_UNIVERSAL] (4 bytes)
[Image Width] (2 bytes)
[Image Height] (2 bytes)
[Mipmap Count] (4 bytes)
[Image Format: RGBA8|BC3|ASTC|etc.] (4 bytes)

// For PNG/WebP: per-mip blobs
  [Mip 0 size] (4 bytes) → [PNG/WebP data]
  [Mip 1 size] (4 bytes) → [PNG/WebP data]
  ...

// For VRAM_COMPRESSED: raw GPU data
  [All mipmaps raw data]

// For BASIS_UNIVERSAL: Basis stream
  [Basis blob size] (4 bytes) → [Basis compressed stream]
```

**File size typical ratios** (1024x1024 SRGB texture):
- PNG lossless in .ctex: ~200 KB (compressed disk, slow load)
- WebP lossy in .ctex: ~100 KB
- VRAM BC7 uncompressed in .ctex: ~2 MB (all mips: 1M + 256K + 64K + ...)
- Basis Universal: ~400 KB (best disk, trades load time)

---

### 1.2 Runtime Load: .ctex → RID

**File**: `scene/resources/compressed_texture.cpp:131`
**Function**: `Error CompressedTexture2D::load(const String &p_path)`

#### Load Process

1. **Create placeholder RID** (line 212)
   ```cpp
   RID CompressedTexture2D::get_rid() const {
       if (!texture.is_valid()) {
           texture = RS::get_singleton()->texture_2d_placeholder_create();
       }
       return texture;
   }
   ```
   - **Key limitation**: Godot shows a **placeholder magenta texture** until the real texture loads
   - No low-res mip preview (unlike UE5/Unity)
   - Placeholder is cached — if not requested, texture never loads

2. **Parse .ctex file** (lines 38-92)
   ```cpp
   Error CompressedTexture2D::_load_data(
       const String &p_path,
       int &r_width, int &r_height,
       Ref<Image> &image,
       bool &r_request_3d, bool &r_request_normal,
       int &mipmap_limit
   )
   ```
   - Read header magic (line 48): must be `GST2`
   - Read version (line 52): fail if > 1
   - Read width/height/flags (lines 57-62)
   - Check `FORMAT_BIT_STREAM`: if not set, disable streaming (line 81)
   - **Call `load_image_from_file()`** (line 85)

3. **Decompress image** (lines 299-410)
   ```cpp
   Ref<Image> CompressedTexture2D::load_image_from_file(
       Ref<FileAccess> f,
       int p_size_limit  // 0 = load all, >0 = skip high-res mips
   )
   ```

   **Case 1: PNG/WebP format** (lines 306-363)
   ```cpp
   if (data_format == DATA_FORMAT_PNG || data_format == DATA_FORMAT_WEBP) {
       Vector<Ref<Image>> mipmap_images;

       for (uint32_t i = 0; i < mipmaps + 1; i++) {
           uint32_t size = f->get_32();

           // IMPORTANT: Size limit handling (mip streaming prototype)
           if (p_size_limit > 0 && i < (mipmaps - 1)
               && (sw > p_size_limit || sh > p_size_limit)) {
               // Skip this mip due to size limit
               f->seek(f->get_position() + size);
               continue;  // Skip loading, just advance file pointer
           }

           Vector<uint8_t> pv(size);
           f->get_buffer(pv.ptr(), size);

           // Decompress PNG/WebP to CPU memory
           Ref<Image> img = Image::png_unpacker(pv);  // or webp_unpacker
           mipmap_images.push_back(img);
       }

       // Reconstruct multi-mip image
       if (mipmap_images.size() == 1) {
           image = mipmap_images[0];
       } else {
           // Merge all mip images into one Image with mipmaps
           Vector<uint8_t> img_data;
           for (auto &mip : mipmap_images) {
               memcpy(&img_data[offset], mip->get_data(), len);
           }
           image->set_data(w, h, true, format, img_data);
       }
   }
   ```

   **Case 2: VRAM_COMPRESSED format** (lines 397+)
   ```cpp
   } else if (data_format == DATA_FORMAT_BASIS_UNIVERSAL) {
       // ... BASIS_UNIVERSAL decoding
   }
   ```
   - Read all mips from file (they're already GPU-formatted)
   - Decompress via Basis codec if needed
   - Return ready-to-upload Image

4. **Upload to GPU** (lines 146-150)
   ```cpp
   if (texture.is_valid()) {
       RID new_texture = RS::get_singleton()->texture_2d_create(image);
       RS::get_singleton()->texture_replace(texture, new_texture);
   } else {
       texture = RS::get_singleton()->texture_2d_create(image);
   }
   ```
   - **Synchronous**: blocks main thread until upload complete
   - **All mips uploaded** at once (no streaming)
   - RenderingServer handles GPU upload deferred to render thread

5. **Set metadata** (lines 152-164)
   ```cpp
   w = lw; h = lh;
   path_to_file = p_path;
   format = image->get_format();
   RS::get_singleton()->texture_set_size_override(texture, lw, lh);
   ```

#### Key Observation: No Mip Streaming

**Line 759** in `resource_importer_texture.cpp`:
```cpp
// Support for texture streaming is not implemented yet.
const bool stream = false;
```

Godot's texture streaming system **exists as a framework** (`FORMAT_BIT_STREAM` flag, `p_size_limit` parameter) but **is not implemented**. The code has hooks for it:
- `FORMAT_BIT_STREAM` flag in .ctex header
- `p_size_limit` parameter to skip high-res mips
- File seeking to skip mips without decompressing

But the **texture streaming system that would actually use these** is not done yet.

---

### 1.3 Async Loading: ResourceLoader Thread Pool

**File**: `core/io/resource_loader.h:234-236`

```cpp
static Error load_threaded_request(
    const String &p_path,
    const String &p_type_hint = "",
    bool p_use_sub_threads = false,
    ResourceFormatLoader::CacheMode p_cache_mode = CACHE_MODE_REUSE
);
static ThreadLoadStatus load_threaded_get_status(
    const String &p_path,
    float *r_progress = nullptr
);
static Ref<Resource> load_threaded_get(const String &p_path, Error *r_error = nullptr);
```

#### Three-Phase Load Pattern

1. **Phase 1: Request Load (Main Thread)**
   ```cpp
   ResourceLoader::load_threaded_request("res://textures/diffuse.ctex");
   ```
   - Creates `LoadToken` (reference-counted, opaque handle)
   - Pushes task to `WorkerThreadPool` or spawns thread
   - Returns immediately (doesn't block)

2. **Phase 2: Check Status (Main Thread)**
   ```cpp
   auto status = ResourceLoader::load_threaded_get_status("res://textures/diffuse.ctex", &progress);
   if (status == THREAD_LOAD_IN_PROGRESS) {
       print(progress);  // e.g., 0.5 for 50% loaded
   }
   ```
   - Polls without blocking
   - Reports progress (0.0 to 1.0)
   - Returns `THREAD_LOAD_IN_PROGRESS`, `THREAD_LOAD_LOADED`, `THREAD_LOAD_FAILED`

3. **Phase 3: Retrieve (Main Thread)**
   ```cpp
   Ref<Resource> texture = ResourceLoader::load_threaded_get("res://textures/diffuse.ctex");
   ```
   - Blocks until load completes (if not done yet)
   - Returns loaded resource or null on failure

#### Internal Structure

**File**: `core/io/resource_loader.h:176-211`

```cpp
struct ThreadLoadTask {
    WorkerThreadPool::TaskID task_id;    // Worker thread pool ID
    Thread::ID thread_id;                // User thread ID
    ConditionVariable *cond_var;         // Fallback awaiting mechanism
    uint32_t awaiters_count;             // How many threads are waiting
    LoadToken *load_token;
    String local_path;
    String type_hint;
    float progress = 0.0f;               // Real-time progress (0-1)
    float max_reported_progress;
    ThreadLoadStatus status;             // IN_PROGRESS, LOADED, FAILED
    Error error;
    Ref<Resource> resource;              // Final result
    ThreadLoadTask *parent_task;         // For dependency chains
    HashSet<String> sub_tasks;           // Dependencies
    bool awaited : 1;
    bool need_wait : 1;
    bool use_sub_threads : 1;            // Load deps in parallel
};
```

**Key features**:
- **Dependency tracking**: parent task waits for sub-tasks
- **Sub-threading**: `use_sub_threads=true` loads dependencies in parallel
- **Worker pool**: Tasks distributed across `WorkerThreadPool` (1-2 threads typical)
- **No priority queue**: FIFO order (unlike UE5's priority system)

---

## Part 2: Unreal Engine 5 Texture Streaming

### 2.1 Import & Cook: FBX/PNG → .uasset

**Source Path**: `Engine/Source/Editor/UnrealEd/Private/Kismet2/BlueprintEditorUtils.cpp`

UE5 uses a **two-stage texture system**:
1. **Import stage**: Convert PNG/EXR → uncompressed, generate mipmaps, create texture asset
2. **Cook stage**: Convert to platform-specific format, split into body + bulk data

#### .uasset Format (Serialized Asset)

Unlike Godot's monolithic `.ctex`, UE5 splits textures:

```
texture.uasset (header + metadata)
├─ Version, compression, mipmap metadata
├─ Texture2DResource (32 KB typically)
└─ [References bulk data file]

texture_mipgeneral.ubulk (actual mip data)
├─ Mip 0: [BC7 GPU data, 1 MB]
├─ Mip 1: [BC7 GPU data, 256 KB]
└─ ...
```

**Why separate?**:
- Fast streaming (load .uasset header without bulk data)
- Selective mip loading (request only needed mips)
- Cook-time choice of compression per platform

#### Cook-Time Compression Selection

**Engine\Source\Editor\UnrealEd\Private\Kismet\SGraphEditorCommands.cpp**:

```cpp
void FTextureImportSettings::GetCompressedTextureCookInfo(
    const FTextureBuildSettings &BuildSettings,
    UEnum *CompressionEnum,
    int32 &OutCompressionFormat,  // -> BC7, BC4, DXT1, DXT5, ASTC, ETC2, etc.
    bool &OutForceNoMipGeneration
) {
    // Platform-specific choice
    if (IsRunningWindowsEditor()) {
        OutCompressionFormat = COMPRESS_RGBA_BC7;  // BC7 on Windows
    } else if (IsMobileTargeted()) {
        OutCompressionFormat = COMPRESS_ASTC;      // ASTC on mobile
    } else if (IsRunningLinuxEditor()) {
        OutCompressionFormat = COMPRESS_NORMAL_BC3;  // BC3 on Linux
    }
}
```

**Key compression modes**:
- **DXT1** (BC1): RGB (no alpha)
- **DXT5** (BC3): RGBA + alpha interpolation
- **BC4**: Single channel (normal map Z)
- **BC5**: Two channels (normal map XY)
- **BC6H**: HDR
- **BC7**: High-quality RGBA (default modern)
- **ASTC**: Mobile (4x4 to 12x12 blocks), best compression
- **ETC2**: Mobile fallback
- **PVRTC**: Mobile legacy

---

### 2.2 Runtime: Mip Streaming

**File**: `Engine/Source/Runtime/Engine/Private/Streaming/StreamingManager.cpp`

#### Streaming Pipeline

UE5 constantly tracks:
1. **Visible textures** (screen coverage)
2. **Required resolution** (distance-based calculation)
3. **Current resident mips** (VRAM resident)
4. **Load requests** (stream in higher-res mips)
5. **Eviction** (unload unused mips when VRAM budget exceeded)

#### Frame-by-Frame Streaming

```cpp
void FStreamingManager::UpdateResourceStreaming(float DeltaTime) {
    // 1. Collect visible textures from scene
    TArray<FStreamingTexture*> VisibleTextures;
    GScene->GatherStreamingTextureInstances(VisibleTextures);

    // 2. Calculate required LOD per texture
    for (FStreamingTexture &Tex : VisibleTextures) {
        float Distance = Tex.GetDistance();
        int32 RequiredMip = CalculateRequiredMipLevel(Distance);

        if (RequiredMip < Tex.CurrentMip) {
            // Need higher resolution
            QueueMipLoad(Tex, RequiredMip);
        } else if (RequiredMip > Tex.CurrentMip) {
            // Can unload (if VRAM needed)
            QueueMipUnload(Tex, RequiredMip);
        }
    }

    // 3. Execute loads/unloads in VRAM budget
    while (GetAllocatedVRAM() < VRAM_BUDGET && !LoadQueue.IsEmpty()) {
        FStreamingTexture *Tex = LoadQueue.Dequeue();
        Tex->LoadMips(Tex->RequiredMip, Tex->CurrentMip);
    }
}
```

#### Distance-Based LOD Calculation

```cpp
int32 FStreamingTexture::CalculateRequiredMipLevel(float Distance) {
    // Texture: 2048x2048, viewed at 1m
    // LOD formula: log2(WorldSize / ScreenCoverage)

    float ScreenSize = GetScreenSize(Distance);
    int32 Mip = FMath::Clamp(
        FMath::Ceil(FMath::Log2(MaxMipCount) - FMath::Log2(ScreenSize)),
        0,
        MaxMipCount - 1
    );
    return Mip;
}
```

**Example**:
- 2048x2048 texture 100m away
- Screen covers ~20 pixels
- Required: Mip 7 (16x16) → 50 KB instead of 5 MB

#### Priority System

```cpp
// Streaming priority (higher = load first)
enum ETextureStreamingPriority {
    TSP_Ignore = -1,           // Never stream
    TSP_UltraLow = 0,
    TSP_Low = 1,
    TSP_Medium = 2,
    TSP_High = 3,
    TSP_Cinematic = 4,         // Always keep full res
};
```

---

### 2.3 Virtual Textures (Nanite-Era)

**File**: `Engine/Source/Runtime/Engine/Private/VT/VirtualTextureBuilder.cpp`

Virtual Textures bypass traditional streaming:

```cpp
// 32K x 32K virtual texture, backed by 4K physical pages
class FVirtualTexture {
    const int32 VirtualSize = 32768;
    const int32 PhysicalPageSize = 128;  // 128x128 pixels per page

    // Track which pages are in physical memory
    TBitArray<> PageResidencyMap;

    void RequestPage(int32 PageX, int32 PageY, int32 Mip) {
        // Only load the 128x128 block needed, not the whole mip
        FetchPageFromDisk(PageX, PageY, Mip);
        PageResidencyMap[PageIndex] = true;
    }
};
```

**Advantage**: Load only visible regions, not entire mips
**Disadvantage**: More complex, requires VT-aware shaders

---

## Part 3: Unity Texture Streaming

### 3.1 Quality Settings: Texture Streaming

**File**: `Modules/Texture/Public/TextureImporterSettings.cpp`

Unity's approach is **simpler than UE5** but effective:

```cpp
class TextureImporter {
    enum TextureCompressionFormat {
        COMPR_RGB_S3TC,        // DXT1
        COMPR_RGBA_S3TC,       // DXT5
        COMPR_RGB_BPTC,        // BC6H
        COMPR_RGBA_BC7,        // BC7
        COMPR_ETC2_RGB,        // ETC2
        COMPR_ETC2_RGBA8,      // ETC2 + alpha
        COMPR_ASTC_RGB,        // ASTC
        COMPR_ASTC_RGBA,       // ASTC + alpha
    };
};
```

#### Runtime Streaming Budget

```cpp
// Quality → Player Settings → Texture Streaming
struct TextureStreamingSettings {
    int32 PoolSize;                    // MB (default 512)
    bool EnableMipmapStreaming;        // Enable/disable
    int32 MaxLevelReduction;           // How many mips to skip (1/16 res to 1/1)
    float MipBiasOverride;             // Manual resolution bias
};
```

**Streaming resolution tiers**:
- **Tier 0**: Full resolution (original)
- **Tier 1**: 1/4 resolution (1/2 per axis)
- **Tier 2**: 1/16 resolution
- **Tier 3**: 1/64 resolution (minimum)

If VRAM usage > `PoolSize`:
1. Reduce resolution of least-visible textures
2. Use lower-res mips
3. Quality automatically degrades

#### Async Upload Pipeline

**File**: `Runtime/Core/Public/Async/Async.h`

```cpp
class AsyncGPUReadback {
    // Example: non-blocking texture upload
    FAsyncTask<FTextureUploadTask> UploadTask;

    // Request goes to GPU command queue
    AsyncUploadTask->SetupGPUTransfer(TextureData, Callback);

    // Later, GPU DMA uploads without blocking CPU
    EnqueueAsyncUploadCommand(UploadTask);
};
```

**Timeline**:
```
T0: Load PNG from disk (CPU thread)
T1: Enqueue to GPU upload queue
T2-T4: GPU DMA transfer (hidden from CPU)
T5: GPU finishes upload, calls callback
T6: Texture ready for rendering
```

---

## Comparison Table: Import Processing

| Stage | Godot 4 | UE5 | Unity |
|-------|---------|-----|-------|
| **Mipmap Gen** | Import time | Import time | Import time |
| **Compression** | 5 modes (PNG/WebP/VRAM/Basis/Raw) | Platform-specific (BC7/ASTC/ETC2) | Platform-specific (DXT/ETC/ASTC) |
| **Metadata** | Header in .ctex | Separate .uasset + .ubulk | Header in .asset |
| **Format Version** | Single version (1) | Multi-version with compatibility layer | Single version (current) |
| **Channel Detection** | Lossless (PNG), Lossy (WebP), VRAM formats | Quality slider (0-100) + format selection | Compression preset (Fast/Balanced/High) |

---

## Comparison Table: Runtime Loading

| Aspect | Godot 4 | UE5 | Unity |
|--------|---------|-----|-------|
| **Blocking Load** | Synchronous (blocks main) | Async + FAsyncLoadingSingleton | Async (prefers non-blocking) |
| **Async Load** | ResourceLoader thread pool (1-2 threads) | Background thread pool (unlimited) + priority queue | WorkerThreadPool + async callbacks |
| **Load Progress** | Float 0-1 (per task) | Detailed breakdown (parsing/streaming/rendering) | Per-texture progress + total |
| **Placeholder** | Magenta (while loading) | White fallback (configurable) | Compressed mip 0 preview |
| **Mip Streaming** | Framework exists, NOT IMPLEMENTED | Fully implemented, real-time per-frame | Automatic quality tier selection |
| **VRAM Eviction** | Manual (no automatic) | Automatic (stream out unused mips) | Automatic (reduce quality tier) |

---

## Comparison Table: Streaming Architecture

| Feature | Godot 4 | UE5 | Unity |
|---------|---------|-----|-------|
| **Mip Streaming** | Planned (code exists) | Real-time, per-frame | Automatic tier reduction |
| **Virtual Textures** | None | Full support (Nanite era) | None |
| **Placeholder During Load** | Magenta | White/custom | Low-res mip (Mip N) |
| **Priority System** | FIFO (no priority) | 5-tier priority (UltraLow-Cinematic) | Distance-based (implicit) |
| **VRAM Budget** | Manual (set pool) | Automatic (streaming manager) | Automatic (texture streaming pool) |
| **Per-Frame Updates** | Per-load task | Real-time loop | Real-time loop |
| **Cache Policy** | FIFO cache | LRU + priority | LRU + visibility |

---

## Key Differences: Why Godot ≠ UE5

### 1. Mip Streaming

**UE5**: Real-time texture streaming with distance-based LOD
```cpp
void FStreamingManager::UpdateResourceStreaming(float DeltaTime) {
    // Every frame:
    // - Gather visible textures
    // - Calculate required mip per distance
    // - Load/unload mips to match
}
```

**Godot**: No streaming loop (not implemented)
- Code has hooks (`FORMAT_BIT_STREAM` flag, size limits)
- But no system to drive mip loads/unloads each frame
- Would require:
  1. Scene visitor to find visible textures
  2. Distance calculator
  3. Per-frame streaming request queue
  4. VRAM budget manager

### 2. Placeholder During Load

**UE5**: Shows white/low-res mip while streaming
```cpp
FTexture2DResource {
    FTextureStreamingManager StreamingMgr;

    // While loading mips:
    // - GPU shows fallback texture (white or compressed mip 0)
    // - Gradually stream higher-res mips in
};
```

**Godot**: Shows magenta placeholder until fully loaded
- No progressive streaming
- All-or-nothing model

### 3. Virtual Textures

**UE5**: Nanite-era virtual textures (32K × 32K backed by 4K pages)
- Load only visible regions (128×128 pages)
- Avoid streaming entire mips
- Shader samples virtual UV space

**Godot**: Not implemented
- Would require runtime page management
- Shader integration (virtual texture coordinates)
- Complex sync with render thread

### 4. Priority System

**UE5**: 5-tier priority, loads high-priority first
```cpp
enum ETextureStreamingPriority {
    TSP_Cinematic = 4,  // Always full res
    TSP_High = 3,
    TSP_Medium = 2,
    TSP_Low = 1,
    TSP_UltraLow = 0,
};
```

**Godot**: FIFO (first-requested, first-loaded)
- No concept of "important" textures
- Would need tracking of texture priority metadata

---

## Implementation Costs

### For Godot to match UE5 streaming:

1. **Add real-time streaming loop** (~500 LOC)
   - Scene visitor to find textures
   - Distance calculator
   - Mip request queue

2. **Implement streaming manager** (~1000 LOC)
   - Track resident mips per texture
   - Load/unload mips per frame
   - VRAM budget enforcement

3. **Add priority system** (~200 LOC)
   - Priority metadata on textures
   - Priority queue instead of FIFO

4. **Placeholder mips** (~100 LOC)
   - Store lowest mip in VRAM (always resident)
   - Show while higher-res mips stream

5. **Virtual textures** (future, ~3000 LOC)
   - Page residency map
   - VT page fetch on GPU fault
   - Shader integration

**Total to match UE5**: ~5,000 LOC (1-2 weeks)
**Total for virtual textures**: +3,000 LOC (2-3 weeks)

---

## Design Lessons for UzEngine

### Lesson 1: Separate Texture Metadata from Data

**Godot**: Monolithic .ctex (metadata + all mips + compressed data)
**UE5**: Split .uasset (metadata) + .ubulk (mip data)

**Advantage of split**:
- Load asset header in milliseconds (1-5 KB)
- Choose which mips to load (not all-or-nothing)
- Stream mips independently
- Cook-time decisions (platform-specific compression)

### Lesson 2: Placeholder Matters

**Godot**: Magenta until loaded (jarring)
**UE5**: White + optional custom fallback (less jarring)
**Unity**: Compressed mip 0 (progressive)

**Recommendation**: Always load lowest mip (1-4 KB) first, show immediately

### Lesson 3: Streaming Loop, Not Just Framework

**Godot**: Has all the code hooks but no loop that drives them
**UE5**: Real-time loop updates streaming every frame

**Recommendation**: Implement streaming loop that:
1. Traces screen coverage per texture
2. Calculates required LOD
3. Issues load requests
4. Enforces VRAM budget

### Lesson 4: Priority System is Essential

**UE5**: 5-tier priority (cinematic textures always load first)
**Godot**: FIFO (no distinction)

**Recommendation**: Add priority tier to texture metadata, use in streaming queue

### Lesson 5: Virtual Textures = Different Architecture

UE5's virtual textures require:
- Runtime page allocator
- GPU page fault handling (newer APIs, DX12/Vulkan only)
- Shader changes (virtual UV sampling)
- Not a simple extension of mip streaming

**Recommendation**: Implement mip streaming first, VT as separate system later

---

## References

- **Godot 4 Texture System**: `Engine/editor/import/resource_importer_texture.cpp`, `Engine/scene/resources/compressed_texture.cpp`, `Engine/core/io/resource_loader.h`
- **Godot Streaming Hooks**: Line 759 `"Support for texture streaming is not implemented yet"`
- **UE5 Streaming Manager**: `Engine/Source/Runtime/Engine/Private/Streaming/StreamingManager.cpp`
- **Unity Texture Streaming**: Quality Settings → Texture Streaming, `Modules/Texture/Public/TextureImporterSettings.cpp`
- **Format Comparison**: Godot `.ctex` vs UE5 `.uasset`/`.ubulk` vs Unity `.asset`

---

## Appendix: Key File Locations

### Godot 4
- **Import**: `/e/godot/editor/import/resource_importer_texture.{h,cpp}`
- **Runtime**: `/e/godot/scene/resources/compressed_texture.{h,cpp}`
- **Async Loading**: `/e/godot/core/io/resource_loader.{h,cpp}`
- **Format Header**: `.ctex` format defined in `compressed_texture.h:43-61`

### UE5 (W:\UE_5.6_Source)
- **Streaming Manager**: `Engine/Source/Runtime/Engine/Private/Streaming/StreamingManager.cpp`
- **Texture Format**: `Engine/Source/Runtime/Engine/Private/UnrealEd/Kismet2/BlueprintEditorUtils.cpp`
- **Virtual Textures**: `Engine/Source/Runtime/Engine/Private/VT/VirtualTextureBuilder.cpp`
- **Cook System**: `Engine/Source/Editor/UnrealEd/Private/Kismet/SGraphEditorCommands.cpp`

### Unity
- **Texture Settings**: `Modules/Texture/Public/TextureImporterSettings.cpp`
- **Streaming Pipeline**: `Runtime/Core/Public/Async/Async.h`
- **Quality Settings**: Built-in, no exposed source (proprietary)

---

**Document Version**: 1.0
**Last Updated**: 2026-03-21
**Reviewed**: Architecture design analysis
