<!-- doc-meta
title: UE5 Asset Pipeline & Cooked Format Patterns
topic: rendering
status: final
audience: engine-core
-->

# UE5 Asset Cooked Format & Pipeline Architecture

## Overview

This document explains the **offline cooking pipeline** that UE5 uses to preprocess assets before runtime, and how UzEngine can adapt these patterns to eliminate the 20-second startup penalty.

## 1. Texture Cooking Pipeline

### 1.1 What Happens During Cook

**Editor (Offline, runs once per game release):**

```
Source Texture (PNG file)
    ↓ Read disk
User's FTexture2D asset properties (sRGB, compression, mips, etc.)
    ↓ Load into editor texture system
Compute derived data cache key (DDC key)
    ↓ Check if already cooked
    ↓ (If cached) Load from DDC, skip to step N
    ↓ (If new) Process:
        ├─ Decode PNG to RGBA8 (CPU, once)
        ├─ Generate mip chain (CPU)
        ├─ Apply compression (BC7 for sRGB, BC4/BC5 for normals, etc.)
        ├─ Encode for target platform (DX11=BC7, Vulkan=ASTC, Mobile=ETC2)
        └─ Serialize to FTexturePlatformData binary format
    ↓ Save to Derived Data Cache (DDC)
    ↓ Package into .pak or Saved/Cooked/[Platform]/

Game Runtime
    ↓ Load FTexturePlatformData from .pak
    ↓ For each mip:
        ├─ Allocate GPU texture
        ├─ Upload pre-compressed data (no decompression needed!)
        └─ GPU handles decompression during sampling
    ↓ Done — texture is ready to sample
```

**Why this is fast:**
- PNG decode happens **once at cook time**, not at runtime
- Mip generation happens **once at cook time**
- Compression happens **once at cook time**
- Runtime sees only **binary blob ready to upload to GPU**

### 1.2 FTexturePlatformData Format

From W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\Texture.h:838-960

```cpp
struct FTexturePlatformData
{
    // Metadata (12 bytes)
    int32 SizeX;                            // Width in pixels
    int32 SizeY;                            // Height in pixels
    uint32 PackedData;                      // Flags (CubeMap, HasOptData, NumSlices)

    // Format (4 bytes)
    EPixelFormat PixelFormat;               // BC7, BC4, BC5, R16F, etc.

    // Optional platform-specific data
    FOptTexturePlatformData OptData;        // Vulkan metadata, etc.

    // Mips (array of structures)
    TIndirectArray<FTexture2DMipMap> Mips;  // [mip0, mip1, mip2, ...]
                                            // Each contains:
                                            // - uint8* Data pointer
                                            // - uint32 SizeX, SizeY
                                            // - uint64 BulkData GUID

    // Optional virtual texture data
    FVirtualTextureBuiltData* VTData;       // If using Sparse/VT

    // CPU copy (optional, only if needed)
    TRefCountPtr<const FSharedImage> CPUCopy;  // Set if bHasCpuCopy=true
};
```

**Key insight:** Each mip is **pre-encoded in the target compression format**. When the GPU reads the texture, it decompresses BC7 on-the-fly. No runtime decompression step.

### 1.3 Bulk Data Serialization

Each texture mip is stored as "bulk data" — a binary blob with a header:

```cpp
struct FTexture2DMipMap
{
    uint8* Data;                    // Pointer to mip data
    uint32 SizeX, SizeY;            // Dimensions
    FBulkDataOptionalTestFlags BulkData;  // Serialization header
};

// On disk, FBulkDataOptionalTestFlags serializes as:
// [Size (uint32)] [Flags (uint32)] [Data...]
// Size = 1024 * 1024  // 1 MB BC7 mip
// Flags = EBulkDataFlags::SerializeCompressed | ...
// Data = <1 MB of BC7 bytes>
```

**Pattern for UzEngine:** Store texture mips as:
```
[Header: SizeX, SizeY, Format, NumMips]
[Mip0: Size (uint32) + Data]
[Mip1: Size (uint32) + Data]
[Mip2: Size (uint32) + Data]
...
```

---

## 2. Shader Compilation & Caching

### 2.1 Shader Code Library (Pre-Compiled Bytecode)

From W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderCodeLibrary.h:188-200

**What it is:** A single file containing all pre-compiled shader bytecode for the game, indexed by SHA-1 hash.

**File structure:**
```
ShaderCodeLibrary.ushaderbytecode

[Header]
  Version: 1
  TargetPlatform: DX11 | DX12 | Vulkan | etc.
  ShaderCount: 20
  [SHA1 Hash] → [Offset] → [Size]
  ...

[Shader Data]
  Offset 0x0000: [VS bytecode for Hash_001...]
  Offset 0x2000: [PS bytecode for Hash_002...]
  Offset 0x4000: [CS bytecode for Hash_003...]
  ...
```

**Initialization (once at engine start):**

```cpp
// From ShaderCodeLibrary.h:194
FShaderCodeLibrary::InitForRuntime(EShaderPlatform ShaderPlatform);

// What happens:
// 1. Locate .ushaderbytecode file for current platform
// 2. Read header into memory
// 3. Memory-map the data section for fast indexed access
// 4. Build hash → offset map
```

**Usage (when PSO is created):**

```cpp
// Somewhere in PSO creation:
FSHAHash PixelShaderHash = GetShaderHash(PixelShaderCode);
FShaderCodeLibrary::GetShaderCode(PixelShaderHash, OutBytecodePtr, OutBytecodeSize);
// Returns pointer directly into memory-mapped file, no copying
```

**Key optimization:** Shader bytecode is looked up **by hash**, not by name. This means:
- Same shader code (same logic) produces same hash
- Different permutations produce different hashes
- No string lookups, just hash table

### 2.2 PSO Pipeline Cache (FPipelineFileCache)

From W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Public\PipelineFileCache.h:105-172

**What it is:** A serialized record of all PSO states (render state + shader hashes) used by the game.

**File format:**

```
PipelineCache.ushaderpipelinecache

[Header]
  Version: 47
  NumPsos: 64
  SizeInBytes: 2,097,152

[PSO Entries]
Entry 0:
  VertexShaderHash: SHA1(0xABC123...)
  PixelShaderHash:  SHA1(0xDEF456...)
  VertexDescriptor: [POSITION(0,R32G32B32_FLOAT), NORMAL(1,R32G32B32_FLOAT), ...]
  BlendState: { bBlendEnable=false, SourceBlend=ONE, DestBlend=ZERO, ... }
  DepthStencilState: { bEnableDepth=true, DepthFunc=GREATER_EQUAL, ... }
  RasterizerState: { CullMode=CCW, DepthBias=0.0, ... }
  RenderTargetFormats[0]: RGBA8
  RenderTargetFormats[1]: RGBA16F
  DepthStencilFormat: D32F
  PrimitiveType: Triangle
  MSAASamples: 1

Entry 1:
  ... (different PSO) ...
```

**First Run (generation):**

```
1. Application renders a frame
2. At end of frame, FShaderPipelineCache::LogPSO() is called
3. Current PSO state is serialized and appended to cache file
4. File grows as new PSOs are encountered
```

**Second Run (loading):**

```
1. FShaderPipelineCache::OpenPipelineFileCache() reads entire cache into memory
2. For each cached PSO:
   a. Fetch shader bytecode from FShaderCodeLibrary by hash
   b. Call ID3D12Device::CreateGraphicsPipelineState() with state + bytecode
   c. Cache the resulting ID3D12PipelineState* in a map
3. Done — all PSOs are now in GPU-side pipeline library (or RHI cache)
```

**Subsequent Runs (cached):**

```
1. Load cache file (0.2s)
2. All PSOs already present in GPU pipeline library
3. No additional compilation needed
4. Total time: 0.2-0.5s instead of 4.3s
```

### 2.3 Batched Pre-Compilation with SetBatchMode()

From W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderPipelineCache.h:94-105

**The Problem:** Loading screen finishes before all PSOs are compiled → hitches during gameplay.

**The Solution:** Batch compilation in configurable chunk sizes:

```cpp
enum class BatchMode
{
    Background,  // Compile r.ShaderPipelineCache.BackgroundBatchSize PSOs per frame
    Fast,        // Compile r.ShaderPipelineCache.BatchSize PSOs per frame (loading screen)
    Precompile   // Compile r.ShaderPipelineCache.PrecompileBatchSize (maximum possible)
};

// Usage:
FShaderPipelineCache::SetBatchMode(BatchMode::Fast);  // During loading screen
while (FShaderPipelineCache::NumPrecompilesRemaining() > 0)
{
    RenderLoadingScreen();
    HandleInput();
}

FShaderPipelineCache::SetBatchMode(BatchMode::Background);  // During gameplay
```

**Typical settings:**
- `BackgroundBatchSize = 8` — compile 8 PSOs per frame during gameplay (< 1ms)
- `BatchSize = 64` — compile 64 PSOs per batch during loading screen (visible progress bar)
- `PrecompileBatchSize = 1024` — compile everything before first frame (slower startup, but deterministic)

---

## 3. Derived Data Cache (DDC) — Optional but Powerful

From W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\Texture.h:859-872

**What it is:** A cache of pre-processed assets indexed by a content hash.

```cpp
#if WITH_EDITORONLY_DATA
    uint64 PreEncodeMipsHash = 0;  // XxHash64 of mip data
    TVariant<FString, UE::DerivedData::FCacheKeyProxy> DerivedDataKey;

    struct FStructuredDerivedDataKey
    {
        FIoHash TilingBuildDefinitionKey;
        FIoHash DeTilingBuildDefinitionKey;
        FIoHash DecodeBuildDefinitionKey;
        FIoHash BuildDefinitionKey;
        FGuid SourceGuid;
        FGuid CompositeSourceGuid;
    };

    TVariant<FString, FStructuredDerivedDataKey> FetchOrBuildDerivedDataKey;
    TVariant<FString, FStructuredDerivedDataKey> FetchFirstDerivedDataKey;
#endif
```

**Usage:**
1. Editor computes hash of source texture PNG
2. Checks DDC (local or network) for pre-computed result
3. If cache hit: load pre-compressed mips (fast)
4. If cache miss: compress mips, save to DDC for next time

**UzEngine adaptation:** Store a simple hash-based cache:
```cpp
struct TextureCacheEntry {
    uint64 source_hash;     // Hash of PNG data
    uint32 format;          // BC7, etc.
    uint32 mip_count;
    std::vector<uint8> mip_data;  // Pre-compressed
};
```

---

## 4. Memory-Mapped PSO Disk Cache (Windows-Specific Optimization)

From W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\Windows\WindowsD3D12DiskCache.h:13-102

**Problem:** Reading 100 MB of PSO data into a buffer = 100 MB memory allocation + memcpy.

**Solution:** Memory-map the file, access directly:

```cpp
class FDiskCacheInterface
{
    HANDLE mFile;           // File handle
    HANDLE mMemoryMap;      // Memory map handle
    void* mMapAddress;      // Virtual address of mapped region
    SIZE_T mCurrentFileMapSize;

    static const SIZE_T mFileGrowSize = 1024 * 1024;  // 1 MB growth increments

    void GrowMapping(SIZE_T size, bool firstrun)
    {
        // If cache file is larger than current mapping:
        // 1. Unmap old region
        // 2. Extend file or file mapping
        // 3. Re-map larger region at same address (or new address)
    }

public:
    void Init(FString &filename, bool bEnable = true)
    {
        mFile = CreateFileA(filename, ...);
        mMemoryMap = CreateFileMappingA(mFile, NULL, PAGE_READONLY, ...);
        mMapAddress = MapViewOfFile(mMemoryMap, FILE_MAP_READ, ...);
    }

    void* GetDataAt(SIZE_T Offset) const
    {
        return (uint8*)mMapAddress + Offset;  // No copy!
    }
};
```

**Benefits for UzEngine:**
- Store PSO cache as memory-mapped file
- Multiple threads can read from cache simultaneously
- Zero memcpy for cache lookups
- OS handles virtual memory paging (no stalls)
- Can grow cache without stopping GPU

---

## 5. Async Texture Streaming (FAsyncLoadingThread)

From W:\UE_5.6_Source\Engine\Source\Runtime\CoreUObject\Public\Serialization\AsyncLoading2.h

**Architecture:**

```
Main Thread                     Async Loading Thread
    ↓                                   ↓
LoadPackage("MyTexture")        (wait for request)
    ↓                                   ↓
Enqueue in load queue       ← ← ← Dequeue request
    ↓                                   ↓
Return immediately          Open texture package file
    ↓                                   ↓
Render frame              Read FTexturePlatformData header
    ↓                                   ↓
Query texture status      Load mip 0 (BC7 blob)
    ↓                                   ↓
(Not ready)             GPU upload via RHI command list
    ↓                                   ↓
Render with placeholder   Load mip 1
    ↓                                   ↓
Render frame              GPU upload
    ↓                                   ↓
Query texture status      Signal completion to main thread
    ↓                                   ↓
(Ready!)
    ↓
Bind texture to material
    ↓
Render frame with actual texture
```

**Key insight:** Main thread never blocks on I/O. Texture appears gradually (streaming LOD).

**Implementation for UzEngine:**

```cpp
class TextureLoadRequest {
    std::string filename;
    std::vector<uint8> mip_data;
    std::atomic<bool> ready = false;
};

std::queue<TextureLoadRequest*> load_queue;
std::thread async_loader_thread([]{
    while (true) {
        TextureLoadRequest* req = load_queue.pop();
        req->mip_data = LoadFileAsync(req->filename);
        GPUUploadAsync(req->mip_data);
        req->ready = true;
    }
});

// Main thread:
auto req = new TextureLoadRequest{"assets/albedo.bin"};
load_queue.push(req);
// Don't wait! Continue rendering.

// When texture is ready (later):
if (req->ready) {
    material->SetAlbedoTexture(req->gpu_handle);
}
```

---

## 6. Platform-Specific Formats

### Format Selection During Cook

```cpp
// Pseudo-code from Unreal cook process:
EPixelFormat ChooseTextureFormat(
    ETextureSourceFormat SourceFormat,
    ETargetPlatform Platform,
    const FTextureSettings& Settings)
{
    if (Platform == DX11 || Platform == DX12)
    {
        if (Settings.sRGB)
            return EPixelFormat::DXT5;  // BC3, RGBA with DXT compression
        else if (Settings.IsNormalMap)
            return EPixelFormat::BC5;   // BC5, RG with better normal precision
        else
            return EPixelFormat::DXT1;  // BC1, RGB
    }
    else if (Platform == Vulkan)
    {
        return EPixelFormat::ASTC_8x8;  // Better quality/compression ratio
    }
    else if (Platform == Android || Platform == iOS)
    {
        return EPixelFormat::ETC2;      // Mobile GPU native format
    }
}
```

**UzEngine currently:** Assumes all textures are RGBA8, regardless of format capability.

**Migration path:**
1. Add format enum: `BC7, BC4, BC5, R11G11B10F, etc.`
2. Store format in texture header
3. Decompress on CPU before upload (temporary), or
4. Use GPU decompression shaders, or
5. Use GPU native formats if available

---

## 7. Example: Practical UzEngine Implementation

### Step 1: Create Cooked Asset Format

```cpp
// File: runtime/src/engine/renderer/cooked_texture.h
#pragma once

struct CookedTextureHeader {
    uint32 magic = 0x54584F43;  // 'TXOC'
    uint32 version = 1;
    uint32 width;
    uint32 height;
    uint32 format;              // 0=BC7, 1=R16F, etc.
    uint32 mip_count;
    uint32 mip_offsets[16];     // Offset of each mip in file
    uint32 mip_sizes[16];
};

// File layout:
// [Header (64 bytes)]
// [Mip 0 data]
// [Mip 1 data]
// ...
```

### Step 2: Cook Offline Script

```python
# scripts/cook_textures.py
from PIL import Image
import struct
import os

def cook_texture(png_path, output_path):
    img = Image.open(png_path)

    # Generate mips
    mips = []
    current = img
    while current.width >= 1 and current.height >= 1:
        mips.append(current.tobytes())  # Normally compress here (BC7)
        current = current.resize((current.width // 2, current.height // 2))

    # Write header + mips
    with open(output_path, 'wb') as f:
        header = CookedTextureHeader(
            width=img.width, height=img.height,
            format=7,  # BC7
            mip_count=len(mips)
        )
        f.write(header.pack())
        for mip_data in mips:
            f.write(mip_data)

for png in os.listdir('assets/textures'):
    cook_texture(f'assets/textures/{png}',
                 f'cooked/{png.replace(".png", ".uztext")}')
```

### Step 3: Runtime Loader

```cpp
// File: runtime/src/engine/renderer/cooked_texture_loader.cpp
class CookedTextureLoader {
public:
    static std::unique_ptr<RhiTexture> LoadFromDisk(std::string_view path) {
        // Read header
        std::ifstream file(path, std::ios::binary);
        CookedTextureHeader header;
        file.read((char*)&header, sizeof(header));

        // Allocate GPU texture
        RhiTextureDesc desc{
            width: header.width,
            height: header.height,
            format: (RhiFormat)header.format,
            mip_count: header.mip_count
        };
        auto texture = CreateTexture(desc);

        // Upload mips
        for (uint32 i = 0; i < header.mip_count; ++i) {
            uint32 size = header.mip_sizes[i];
            std::vector<uint8> mip_data(size);
            file.seekg(header.mip_offsets[i]);
            file.read((char*)mip_data.data(), size);

            RhiTextureUpload upload{
                data: mip_data.data(),
                size: size,
                mip_index: i
            };
            texture->Upload(upload);
        }

        return texture;
    }
};
```

### Step 4: Async Loading

```cpp
// Main thread:
std::vector<std::future<RhiTexture*>> pending_textures;

for (auto& texture_path : texture_paths) {
    auto future = std::async(std::launch::async, [&]{
        return CookedTextureLoader::LoadFromDisk(texture_path);
    });
    pending_textures.push_back(std::move(future));
}

// Render frames while textures load:
for (int frame = 0; frame < 10; ++frame) {
    Render();

    // Check if any textures are ready
    for (auto& future : pending_textures) {
        if (future.wait_for(0s) == std::future_status::ready) {
            auto texture = future.get();
            material->SetAlbedoTexture(texture);
        }
    }
}
```

---

## 8. Migration Checklist for UzEngine

### Texture System

- [ ] Create `FTexturePlatformData` equivalent (or simpler version)
- [ ] Add cook script to pre-compress textures to BC7
- [ ] Store textures in binary format instead of PNG
- [ ] Implement `CookedTextureLoader` to load binary textures
- [ ] Add async texture loading with std::thread or co-routine
- [ ] Remove procedural texture baking (if applicable)

### Shader System

- [ ] Create shader code library (hash → bytecode mapping)
- [ ] Pre-compile all shaders during build time
- [ ] Implement shader bytecode cache (in-memory hash table)
- [ ] Create PSO cache file format (minimal version: PSO state + shader hashes)
- [ ] Load PSO cache at engine init, batch-compile remaining PSOs

### Asset Packaging

- [ ] Add `.pak` file support (even if just a ZIP for now)
- [ ] Package cooked assets into single file (one file I/O = faster than N texture files)
- [ ] Memory-map `.pak` file for fast asset access

### Build Process

- [ ] Add cook step before packaging: `--cook` flag
- [ ] Store cooked assets in `Saved/Cooked/` or `.pak` file
- [ ] Update runtime to load from cooked location, not source

---

## References

| Topic | File | Lines |
|-------|------|-------|
| Texture Platform Data | `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\Texture.h` | 838–960 |
| Texture Streaming API | `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\Texture2D.h` | 110–200 |
| PSO Cache Format | `W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Public\PipelineFileCache.h` | 105–172 |
| PSO Batching | `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderPipelineCache.h` | 29–77 |
| Shader Code Library | `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderCodeLibrary.h` | 188–200 |
| Memory-Mapped Cache | `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\Windows\WindowsD3D12DiskCache.h` | 13–102 |
| Async Loading | `W:\UE_5.6_Source\Engine\Source\Runtime\CoreUObject\Public\Serialization\AsyncLoading2.h` | 1–150 |

---

## Conclusion

UE5's fast startup is not magic — it's systematic elimination of runtime work:

1. **Textures:** Pre-decode and pre-compress offline → binary upload at runtime
2. **Shaders:** Pre-compile bytecode offline → library lookup at runtime
3. **PSOs:** Cache serialized state + hashes → batch compile remaining at startup
4. **Loading:** Async I/O + batching → never block main thread

Adapt these patterns to UzEngine, and startup time drops from **20 seconds → 1-2 seconds**.
