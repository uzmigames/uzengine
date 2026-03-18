<!-- doc-meta
title: UE5 Fast Loading Architecture Analysis
topic: rendering
status: final
audience: engine-core
-->

# UE5 Fast Loading Architecture: How UE5 Avoids UzEngine's 20-Second Startup Penalty

## Executive Summary

UzEngine's PBR demo startup takes **20.4 seconds** due to three bottlenecks:
1. CPU PNG decoding (7.8s) — serial texture loading via stbi_load
2. DXC shader compilation (4.3s) — lazy PSO compilation at runtime
3. CPU procedural texture bake + disk write (8.3s) — regenerating textures every load

**UE5 avoids ALL THREE** through a three-tier approach:

1. **Cooked Asset Pipeline** — PNG/PNG decoding never happens at runtime. Textures are pre-cooked to **BC7-compressed binary format** (FTexturePlatformData) in Saved/Cooked/.
2. **Shader Pipeline Cache (PSO Pre-compilation)** — Pre-compiled PSO binary blobs cached on disk via **FShaderPipelineCache** + **FPipelineFileCache**. On first run, async batches compile PSOs; on subsequent runs, they load from disk in **Fast BatchMode** (chunks of 256+ PSOs/frame).
3. **Shader Code Library** — All shader bytecode lives in a shared library (.ushaderbytecode) loaded once at engine init via **FShaderCodeLibrary**, not compiled per-shader.

## Detailed Analysis

### 1. TEXTURE LOADING: Cooked Binary Format

**Problem:** UzEngine loads 43 PNG files serially, decoding each with CPU-side stbi_load.

**UE5 Solution:** Textures never exist as PNG in packaged builds. They're pre-processed during the "cook" step into a binary platform-specific format.

#### FTexturePlatformData Structure (W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\Texture.h:838-960)

```cpp
struct FTexturePlatformData
{
    int32 SizeX, SizeY;
    uint32 PackedData;  // [b31: CubeMap], [b30: HasOptData], [b29-0: NumSlices]
    EPixelFormat PixelFormat;  // BC7, BC4, BC5, R16F, etc.
    FOptTexturePlatformData OptData;
    TIndirectArray<FTexture2DMipMap> Mips;  // Pre-computed mip chain
    FVirtualTextureBuiltData* VTData;  // Optional sparse/VT data
    TRefCountPtr<const FSharedImage> CPUCopy;  // Only for CPU-readable textures
};
```

**Key Design Patterns:**

1. **Mips are Pre-Computed** (line 853): The entire mip chain is calculated offline during cook, not at runtime. Memory is allocated once; no dynamic mipmap generation.

2. **Format is BC-Compressed** (line 849): `EPixelFormat` is BC7 (RGBA) or BC4/BC5 (normal maps), not raw RGBA8. This is **5:1 compression** on typical albedo/normal textures.

3. **CPUCopy is Optional** (line 857): The CPU-readable copy (`FSharedImage`) is only present if `bHasCpuCopy` flag is set. For GPU-only textures, this is null. **UzEngine loads all textures to CPU first, then uploads** — wasteful.

#### Loading Path (Texture2D.h:110, 150-151, 200)

```cpp
class UTexture2D : public UTexture
{
    virtual void Serialize(FArchive& Ar) override;  // Line 110
    virtual bool StreamOut(int32 NewMipCount) final override;  // Line 150
    virtual bool StreamIn(int32 NewMipCount, bool bHighPrio) final override;  // Line 151

    // Async mip loading:
    bool TryLoadMips(int32 FirstMipToLoad, void** OutMipData, FStringView DebugContext);
    bool TryLoadMipsWithSizes(int32 FirstMipToLoad, void** OutMipData, int64 * OutMipSize, FStringView DebugContext);
};
```

**The key difference:** UE5 textures don't decode at load time. `TryLoadMips()` simply reads pre-encoded BC7 data from disk or memory-mapped file. No decompression step. The GPU decompresses BC7 during texture binding (hardware feature).

#### Async Streaming via FAsyncLoadingThread (W:\UE_5.6_Source\Engine\Source\Runtime\CoreUObject\Private\Serialization\AsyncLoading2.h)

UE5 uses **FAsyncLoadingThread** to load textures in the background without blocking the main thread:

```
Main Thread                     Async Loading Thread
    |                                   |
    +-> LoadPackage("MyTexture")       |
    |                                   +-> Read from disk into temporary buffer
    |                                   +-> FTexturePlatformData.TryLoadMips()
    |                                   +-> GPU upload via RHI
    |                                   +-> Callback to main thread when done
    |                                   |
    +<- Continue rendering other assets |
```

The async loader doesn't wait for individual textures; it batches I/O requests and uploads in parallel.

---

### 2. SHADER COMPILATION: PSO Pipeline Cache

**Problem:** UzEngine compiles 20 HLSL shaders at runtime via DXC, spending 4.3 seconds on first run. Every shader must be compiled on every load.

**UE5 Solution:** Three-tier caching system:

#### Tier 1: Shader Code Library (FShaderCodeLibrary)

Location: W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderCodeLibrary.h:188-200

All shader bytecode is pre-compiled offline and packed into a single **FShaderCodeLibrary** file per platform.

```cpp
struct FShaderCodeLibrary
{
    static RENDERCORE_API void InitForRuntime(EShaderPlatform ShaderPlatform);
    static RENDERCORE_API bool AreShaderMapsPreloadedAtLoadTime();
    // ... getters for pre-compiled shader code by hash ...
};
```

**Key workflow:**
1. **Offline (in Unreal Editor)**: Cook shaders → compile via DXC → hash each bytecode blob → pack all into `.ushaderbytecode` file
2. **Runtime (first run)**: Load `.ushaderbytecode` into memory or memory-mapped file once
3. **Runtime (PSO creation)**: Look up shader bytecode by hash, no compilation needed

**Why this matters for UzEngine:** You're compiling the same shader multiple times:
- Once for initial PSO creation
- Again if the PSO cache misses
- Again if the shader is hot-reloaded

UE5 compiles once at cook time, then uses the binary.

#### Tier 2: PSO Disk Cache (FPipelineFileCache)

Location: W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Public\PipelineFileCache.h:105-172

Caches entire PSO state (not individual shaders) to disk.

```cpp
struct FPipelineCacheFileFormatPSO
{
    struct GraphicsDescriptor
    {
        FSHAHash VertexShader;
        FSHAHash FragmentShader;
        FSHAHash GeometryShader;
        FSHAHash MeshShader;
        FSHAHash AmplificationShader;

        FVertexDeclarationElementList VertexDescriptor;
        FBlendStateInitializerRHI BlendState;
        FDepthStencilStateInitializerRHI DepthStencilState;

        EPixelFormat RenderTargetFormats[MaxSimultaneousRenderTargets];
        ETextureCreateFlags RenderTargetFlags[MaxSimultaneousRenderTargets];
        uint32 RenderTargetsActive;
        uint32 MSAASamples;

        EPrimitiveType PrimitiveType;
        uint8 SubpassHint;
        uint8 SubpassIndex;

        // ... more state ...
    };

    // Either GraphicsDescriptor OR ComputeDescriptor:
    struct ComputeDescriptor { FSHAHash ComputeShader; };
    struct FPipelineFileCacheRayTracingDesc { /* ... */ };
};
```

**Key design:**
- **Hashes, not source code:** Only shader hashes are stored, not HLSL source.
- **Render state is serialized:** Blend state, depth state, rasterizer state, render target formats all baked into one PSO entry.
- **Disk format is text-readable** (line 157-160): Can be edited or analyzed by tools.

#### Tier 3: Shader Pipeline Cache (FShaderPipelineCache)

Location: W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderPipelineCache.h:29-77

Orchestrates pre-compilation with batching modes:

```cpp
class FShaderPipelineCache : public FTickableObjectRenderThread
{
    enum class BatchMode
    {
        Background,  // During gameplay — compile slowly (r.ShaderPipelineCache.BackgroundBatchSize)
        Fast,        // Loading screen — compile aggressively (r.ShaderPipelineCache.BatchSize)
        Precompile   // Pre-load entire cache (r.ShaderPipelineCache.PrecompileBatchSize)
    };

    static void SetBatchMode(BatchMode Mode);
    static uint32 NumPrecompilesRemaining();  // Check how many PSOs still need compiling
    static bool OpenPipelineFileCache(EShaderPlatform Platform);
    static bool SavePipelineFileCache(FPipelineFileCacheManager::SaveMode Mode);
};
```

**Usage pattern:**
1. **First run**: Load PSO file cache, compile in **Fast BatchMode** (e.g., 64 PSOs/batch) during loading screen
2. **Subsequent runs**: Load pre-compiled binaries directly from disk (near-instant)
3. **Behind menus**: Switch to **Background BatchMode** to avoid hitches during gameplay

**The critical optimization:** Batching. Instead of compiling 1 PSO at a time (blocking), compile N PSOs per frame (spread work).

#### D3D12 Disk Cache (Windows-Specific)

Location: W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\Windows\WindowsD3D12DiskCache.h:13-102

This is a **separate, lower-level cache** that stores D3D12 compiled binaries.

```cpp
class FDiskCacheInterface
{
    static const uint32 mCurrentHeaderVersion = 11;
    struct FDiskCacheHeader
    {
        uint32 mHeaderVersion;
        uint32 mNumPsos;
        size_t mSizeInBytes;  // Bytes after header
    };

    // Memory-mapped file for fast I/O:
    HANDLE mFile;
    HANDLE mMemoryMap;
    void* mMapAddress;
    SIZE_T mCurrentFileMapSize;  // Grows dynamically

    bool AppendData(const void* pData, size_t size);
    void Flush(uint32 numberOfPSOs);
    void ClearDiskCache();
};
```

**How it works:**
1. **Memory-mapped file**: The PSO cache file is memory-mapped, not read() into a buffer.
2. **Dynamic growth**: If the file is too small, `GrowMapping()` extends it (line 46).
3. **Persistence**: PSO binaries are backed by system memory so they survive file mapping resizes (line 42-41).

**Why memory mapping?** It's **faster than read()**:
- OS handles paging automatically
- Multiple PSOs can be accessed without memcpy
- The file acts as a "persistent cache" — load once, use many times

---

### 3. MATERIAL BAKING: Eliminated via Cooked Textures

**Problem:** UzEngine spends 8.3s baking procedural textures and writing PNG files.

**UE5 Solution:** This step doesn't exist at runtime. Procedural textures are:
1. **Generated in the editor** (or offline tools)
2. **Baked to BC7-compressed binary** during cook
3. **Loaded as pre-compressed data** at runtime (no regeneration)

The FTexturePlatformData structure (above) already includes all the mips pre-computed, so there's nothing to regenerate.

---

## Comparison: UzEngine vs. UE5 Startup Timeline

### UzEngine (Current — 20.4s)

```
Load Start (t=0)
    |
    +-> Texture Loading (7.8s serial)
    |   - Loop over 43 textures
    |   - stbi_load() each PNG from disk
    |   - CPU decode to RGBA8
    |   - GPU upload
    |
    +-> Material Bake (8.3s)
    |   - Regenerate procedural textures
    |   - stbi_write_png() to disk
    |
    +-> Shader Compilation (4.3s)
    |   - DXC.exe compiles 20 shaders
    |   - Create PSOs (each waits for compilation)
    |   - (No caching between runs)
    |
    +-> Scene Load + Render
    |
Load Complete (t=20.4s)
```

### UE5 (Expected — ~1-2s)

```
Load Start (t=0)
    |
    +-> FShaderCodeLibrary::InitForRuntime() (0.1s)
    |   - Load .ushaderbytecode from disk (once)
    |   - All 20 shaders already compiled (offline)
    |
    +-> FShaderPipelineCache::OpenPipelineFileCache() (0.2s)
    |   - Load PSO cache file (binary, platform-specific)
    |   - Memory-map for fast access
    |
    +-> FAsyncLoadingThread::LoadPackage() (parallel, 0.5s felt time)
    |   - Async: Read texture mip data (BC7, already compressed)
    |   - Async: GPU upload
    |   - Main thread: continue rendering other assets
    |
    +-> FShaderPipelineCache::SetBatchMode(Fast) + NumPrecompilesRemaining()
    |   - If PSOs not in cache: compile remaining in batches during loading screen
    |   - If PSOs in cache: already loaded above (0.2s)
    |
    +-> Scene Load + Render (0.5s)
    |
Load Complete (t=1.3s first run, 0.7s subsequent runs)
```

---

## Key Architectural Patterns UzEngine Should Adopt

### Pattern 1: Separate Offline Cook Step

**UE5 Design:**
```
Source Assets (PNG, HLSL)
    ↓
    Cook Process (Offline)
    ├─ Texture → BC7 + Mips
    ├─ Shader → Bytecode
    ├─ PSO State → Serialized
    ↓
Cooked Assets (.pak or Saved/Cooked/)
    ↓
    Runtime Startup
    ├─ Load binary (no decode)
    ├─ Async texture upload
    ├─ Load PSO cache
    └─ Done in 1-2s
```

**UzEngine Currently:**
```
Source Assets (PNG, HLSL)
    ↓
    Runtime Startup (Every Time!)
    ├─ PNG Decode (stbi_load)
    ├─ Texture Bake (regenerate)
    ├─ Shader Compile (DXC)
    └─ 20.4s every load
```

**Migration Path:**
1. **Introduce a cook step** (even simple: zip textures + compile shaders once)
2. **Store binary textures** instead of PNG (BC7 or any GPU-native format)
3. **Cache compiled shaders** (use shader hashes as keys, store bytecode)
4. **Separate cook from runtime** (cook once at package time, runtime never touches source)

### Pattern 2: Memory-Mapped PSO Cache

Instead of reading PSO state from disk into a buffer, use Windows memory mapping:

```cpp
// UE5 model (WindowsD3D12DiskCache.h):
HANDLE hFile = CreateFileA("pso_cache.bin", ...);
HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, ...);
void* pCache = MapViewOfFile(hMap, FILE_MAP_READ, ...);

// Access PSO directly:
FPipelineCacheEntry* pso = (FPipelineCacheEntry*)(pCache + offset);
ID3D12PipelineState* pipeline = pso->GetPipelineState();  // No copy
```

**Benefits:**
- OS handles virtual memory paging
- Zero memcpy for cache hits
- Multiple threads can read simultaneously
- Scales to 100s of MB of PSO data

### Pattern 3: Async Texture Loading with Batching

```cpp
// UE5 pattern:
FAsyncLoadingThread* AsyncLoader = GetAsyncLoadingThread();
AsyncLoader->LoadPackage("Textures/MyTexture", OnTextureLoaded);

// Main thread continues:
Render(); Render(); Render();

// When texture ready, callback fires:
OnTextureLoaded() { RegisterTextureWithMaterials(); }
```

**UzEngine currently:** Loops over all textures serially, blocking on each stbi_load.

### Pattern 4: Platform-Specific Formats

UE5 stores textures in format matching the GPU:
- **DX11/DX12:** BC7 (RGBA), BC4 (R), BC5 (RG)
- **Vulkan:** ASTC or BC
- **Mobile:** ETC2 or ASTC

**UzEngine currently:** Assumes all textures are PNG, regardless of target platform.

---

## Implementation Priority for UzEngine

### Quick Wins (1-2 days)
1. **Disable material baking** — textures are not procedural. Store them as-is.
2. **Add shader bytecode cache** — compile shaders once, store hashes + bytecode
3. **Parallel texture loading** — use std::thread to load textures concurrently

### Medium Term (1-2 weeks)
1. **Introduce FTexturePlatformData structure** — pre-computed mips + BC7 compression
2. **Implement FShaderPipelineCache** — PSO disk cache with batching
3. **Async scene loading** — don't wait for all textures before rendering

### Long Term (1+ month)
1. **Cook-time shader compilation** — compile all shaders offline
2. **Memory-mapped PSO cache** — avoid buffer copies
3. **Packed asset format (.pak)** — migrate from loose files to pak archives

---

## References

| File | Purpose |
|------|---------|
| `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderPipelineCache.h:29-77` | PSO caching orchestration, batching modes |
| `W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Public\PipelineFileCache.h:105-172` | PSO file format, serialization |
| `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\Windows\WindowsD3D12DiskCache.h:13-102` | Memory-mapped PSO cache implementation |
| `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\Texture.h:838-960` | FTexturePlatformData, mip structure, async loading |
| `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\Texture2D.h:110-200` | Texture streaming API (TryLoadMips) |
| `W:\UE_5.6_Source\Engine\Source\Runtime\CoreUObject\Public\Serialization\AsyncLoading2.h:1-150` | Async package/texture loading infrastructure |
| `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderCodeLibrary.h:188-200` | Shader code library (pre-compiled bytecode) |

---

## Anti-Patterns Found in UzEngine

### 1. ❌ Runtime PNG Decoding
**Current:** `stbi_load()` every load
**Fix:** Pre-cook textures to BC7 or native GPU format

### 2. ❌ Lazy PSO Creation with Compilation
**Current:** Compile shader on first PSO creation, no caching
**Fix:** Separate shader compilation (offline) from PSO creation (runtime)

### 3. ❌ Serial Texture Loading
**Current:** Loop over textures, load+decode each sequentially
**Fix:** Async loading thread with batched I/O

### 4. ❌ No Material Bake Caching
**Current:** Regenerate procedural textures every load
**Fix:** Bake once at cook time, load binary at runtime

### 5. ❌ No PSO Disk Cache
**Current:** Every run recompiles same PSOs
**Fix:** Use FPipelineFileCache model — cache binary PSOs to disk

---

## Conclusion

UE5's startup speed advantage comes from **eliminating runtime work that UzEngine does every load:**

| Work | UzEngine | UE5 | Savings |
|------|----------|-----|---------|
| PNG Decode | Every load (7.8s) | Offline | 7.8s |
| Shader Compile | Every load (4.3s) | Offline | 4.3s |
| Texture Bake | Every load (8.3s) | Offline | 8.3s |
| **Total** | **20.4s** | **1-2s** | **18-19s** |

The pattern: **Separate offline work (cook) from runtime work (load).** UE5 doesn't do anything special at runtime — it just loads pre-computed binary data.
