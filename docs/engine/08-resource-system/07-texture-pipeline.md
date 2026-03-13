# Texture Pipeline — Import, Compression, Mipmaps, and Streaming

> **Status**: Planned (Phase 1.5–2)
> **Source**: `resource_system/texture/`, `import_pipeline/texture/`
> **References**: UE5 FTexturePlatformData + TextureCompressorModule, Godot ResourceImporterTexture + CompressedTexture2D, DirectXTex, ispc_texcomp

## Purpose

The Texture Pipeline ingests raw images (PNG, TGA, HDR, EXR, JPEG), detects sRGB vs linear color space, generates mipmaps, compresses to GPU-friendly formats (BC7, BC5, BC6H), writes to `.uztexture` binary, and manages virtual or mip-stream resources. This pipeline is essential for memory-efficient rendering and visual quality at all viewing distances.

## Architecture

```
Source Texture (PNG, HDR, EXR, JPEG, TGA)
    │
    ├── 1. Load & Decode
    │   ├── stb_image (PNG, TGA, BMP, JPEG)
    │   ├── stb_image_hdr (OpenEXR, Radiance HDR)
    │   └── Metadata detection (dimensions, alpha, color depth)
    │
    ├── 2. Color Space Inference
    │   ├── Heuristic: filename pattern
    │   │   ├── *_albedo, *_diffuse → sRGB
    │   │   ├── *_normal, *_roughness, *_metallic → Linear
    │   │   └── *_ao, *_height → Linear
    │   ├── Or explicit metadata in .import.json sidecar
    │   └── Color space affects mipmap filter and compression
    │
    ├── 3. Resize (if needed)
    │   ├── Downscale to max_size (e.g., 4096 or platform limit)
    │   ├── Preserve aspect ratio
    │   └── Use high-quality Kaiser filter
    │
    ├── 4. Mipmap Chain Generation
    │   ├── Recursive downsampling (Kaiser filter for sRGB, box for linear)
    │   ├── Preserve coverage for alpha textures
    │   ├── Stop at 1×1 or user-specified min_mip
    │   └── Output: texture + mip0, mip1, mip2, ... mipN
    │
    ├── 5. Detect Compression Format
    │   ├── If has alpha: BC7 (best quality, slow)
    │   ├── If no alpha: BC1 (fast, lower quality)
    │   ├── If normal map: BC5 (two-channel, reconstruct Z in shader)
    │   ├── If HDR: BC6H (unsigned half-float)
    │   └── Fallback: uncompressed RGBA for < 256KB
    │
    ├── 6. Compress (ispc_texcomp or DirectXTex)
    │   ├── BC7: Intel ISPC (SIMD, multi-threaded, ~5–10s for 4K)
    │   ├── BC5: DXT5 alpha channel repurposed for two channels
    │   ├── BC6H: Unsigned half-float compression
    │   └── Preserve per-mip quality settings
    │
    ├── 7. Virtual Texture Preparation (optional)
    │   ├── Tile texture into 128×128 or 256×256 page tiles
    │   ├── Generate streamer feedback buffer descriptor
    │   └── Build page index for runtime lookup
    │
    └── 8. Write .uztexture Binary
        └── Binary format: header + mip offsets + compressed mip data + metadata
```

## File Format: .uztexture

Binary format with full mipmap chain for direct GPU upload. All mipmaps are stored uncompressed (RGBA8) or in hardware-native compressed formats (BC1–BC7).

### Binary Layout

All values are little-endian.

```
[Header]           64 bytes
  magic:             4 bytes   "UZTX" (0x5A585455)
  version:           4 bytes   uint32 (1)
  width:             4 bytes   uint32
  height:            4 bytes   uint32
  mipCount:          4 bytes   uint32
  format:            4 bytes   uint32 (TextureFormat enum)
  flags:             4 bytes   uint32 (reserved, must be 0)
  reserved[9]:      36 bytes   (must be 0)

[Mip Levels]   mipCount * sizeof(UzTextureMipLevel)
[Mip Data]     concatenated mip blobs (mip 0 first, largest)
```

### Header Structure (64 bytes, packed)

```cpp
#pragma pack(push, 1)
struct UzTextureHeader {
    uint32_t magic = 0x5A585455;      // Magic: "UZTX"
    uint32_t version = 1;              // Format version (currently 1)
    uint32_t width = 0;                // Base mip width in pixels
    uint32_t height = 0;               // Base mip height in pixels
    uint32_t mipCount = 0;             // Number of mip levels
    uint32_t format = 0;               // TextureFormat cast to uint32
    uint32_t flags = 0;                // Reserved, must be 0
    uint32_t reserved[9] = {};         // Reserved for future use
};
// static_assert(sizeof(UzTextureHeader) == 64)
#pragma pack(pop)
```

### Mip Level Descriptor (24 bytes per mip, packed)

Mip 0 is the largest (base) texture level. Each mip descriptor contains the byte offset and size of the mip data blob.

```cpp
#pragma pack(push, 1)
struct UzTextureMipLevel {
    uint64_t offset = 0;   // Byte offset from file start to mip data
    uint64_t size = 0;     // Mip data size in bytes
    uint32_t width = 0;    // Mip width in pixels
    uint32_t height = 0;   // Mip height in pixels
};
// static_assert(sizeof(UzTextureMipLevel) == 24)
#pragma pack(pop)
```

### Texture Format Enum

```cpp
enum class TextureFormat : uint32_t {
    Uncompressed_RGBA8 = 0,  // 4 bytes/pixel, uncompressed
    BC1                = 1,  // Block-compressed, 4:1 ratio (RGB or 1-bit alpha)
    BC3                = 2,  // Block-compressed, 4:1 ratio (interpolated alpha)
    BC5                = 3,  // Block-compressed, 2-channel (normal maps)
    BC7                = 4,  // Block-compressed, highest quality (all channels)
};
```

### Example: Loading a .uztexture File

```cpp
// Parse header (first 64 bytes)
UzTextureHeader header;
file.read(reinterpret_cast<char*>(&header), 64);

// Validate
if (header.magic != 0x5A585455 || header.version != 1) {
    return error("Invalid .uztexture magic or version");
}

// Parse mip level descriptors
std::vector<UzTextureMipLevel> mips(header.mipCount);
file.read(reinterpret_cast<char*>(mips.data()),
          header.mipCount * sizeof(UzTextureMipLevel));

// Load all mip data
std::vector<std::vector<uint8_t>> mipData(header.mipCount);
for (uint32_t i = 0; i < header.mipCount; ++i) {
    file.seekg(mips[i].offset);
    mipData[i].resize(mips[i].size);
    file.read(reinterpret_cast<char*>(mipData[i].data()), mips[i].size);
}

// Create GPU texture with mip chain
D3D12_RESOURCE_DESC desc{
    .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
    .Width = header.width,
    .Height = header.height,
    .DepthOrArraySize = 1,
    .MipLevels = header.mipCount,
    .Format = ConvertTextureFormat(static_cast<TextureFormat>(header.format)),
    .SampleDesc{1, 0},
    .Usage = D3D12_USAGE_DEFAULT,
};
```

## Texture Importer: PNG/JPG/TGA to .uztexture

The built-in `TextureImporter` class converts common image formats into .uztexture binary format with a full uncompressed RGBA8 mipmap chain.

### Supported Formats
- `.png` — Portable Network Graphics
- `.jpg`, `.jpeg` — JPEG (lossy, decoded to RGBA8)
- `.tga` — Targa/TrueVision (uncompressed or RLE)

### Import Pipeline

```
1. Decode source bytes with stb_image (force RGBA 4-channel output)
2. Generate mipmap chain via 2x2 box filter downsampling
3. Write via WriteUzTexture as Uncompressed_RGBA8
```

### C++ API

```cpp
class TextureImporter : public IAssetImporter {
public:
    /// Returns "texture" — the unique importer name.
    const char* Name() const override;

    /// Returns current importer version (bump if import logic changes).
    uint32_t Version() const override;

    /// Returns {".png", ".jpg", ".jpeg", ".tga"} — supported extensions.
    const std::vector<std::string>& Extensions() const override;

    /// Imports an image file into .uztexture binary format.
    ///
    /// # Arguments
    /// * sourceData - Raw bytes of the image file
    /// * sourceName - Filename for error messages (e.g., "diffuse.png")
    /// * options - Ignored (texture import has no configurable options)
    ///
    /// # Returns
    /// On success: result.data contains the complete .uztexture binary with
    ///           full mipmap chain.
    /// On failure: result.error describes the issue (decode failure, zero-size
    ///           image, etc.).
    /// Logs Asset::Error on failure, Asset::Info on success (with dimensions
    /// and mip count).
    AssetImportResult Import(
        const std::vector<uint8_t>& sourceData,
        const std::string& sourceName,
        const AssetImportOptions& options) override;
};
```

### Output Format

The importer always produces:
- **Uncompressed_RGBA8 format** (no compression, 4 bytes/pixel per mip)
- **Full mipmap chain** (downsampled via 2×2 box filter until 1×1)
- **stb_image decoding** (auto-converts all formats to RGBA)

Example output for a 512×512 PNG texture:

```
UzTextureHeader:
  magic:           0x5A585455 ("UZTX")
  version:         1
  width:           512
  height:          512
  mipCount:        10 (512→256→128→64→32→16→8→4→2→1)
  format:          0 (Uncompressed_RGBA8)
  flags:           0

UzTextureMipLevel[0]:
  offset:          64 + (10 × 24) = 304
  size:            512 × 512 × 4 = 1,048,576 bytes
  width:           512
  height:          512

UzTextureMipLevel[1]:
  offset:          304 + 1,048,576 = 1,048,880
  size:            256 × 256 × 4 = 262,144 bytes
  width:           256
  height:          256

... (continuing for each mip level down to 1×1)
```

### Mipmap Generation Details

Mipmaps are generated using a simple 2×2 box filter:

```cpp
// For each mip level, sample 2×2 block from previous mip
for (uint32_t y = 0; y < next_height; ++y) {
    for (uint32_t x = 0; x < next_width; ++x) {
        // Sample 2×2 block
        uint8_t tl[4], tr[4], bl[4], br[4];
        GetPixel(prev, 2*x,   2*y,   tl);
        GetPixel(prev, 2*x+1, 2*y,   tr);
        GetPixel(prev, 2*x,   2*y+1, bl);
        GetPixel(prev, 2*x+1, 2*y+1, br);

        // Average all 4 samples per channel
        uint8_t avg[4];
        for (int i = 0; i < 4; ++i) {
            avg[i] = (uint32_t(tl[i]) + tr[i] + bl[i] + br[i]) / 4;
        }
        SetPixel(next, x, y, avg);
    }
}
```

## Compression Formats

### BC1 (DXT1)

6:1 compression, RGB only or 1-bit alpha. Use for opaque color textures or simple transparency.

```
Block: 16 bytes
├── 2 × color endpoints (565 bits each)
├── 2 bits per pixel (4×4 block)
└── Interpolates between 4 colors derived from endpoints
```

**Best for**: Uncompressed art (sprites, UI textures).

### BC3 (DXT5)

10:1 compression, RGBA. BC4 for alpha channel, BC1 for RGB. Use for textures with smooth alpha gradients.

```
Block: 16 bytes
├── BC4 block (8 bytes): alpha channel
│   ├── 2 × alpha endpoints (8-bit each)
│   └── 3 bits per pixel
└── BC1 block (8 bytes): RGB
```

**Best for**: Diffuse with alpha masks.

### BC4 (DXT4/5 Alpha)

8:1 compression, single-channel (grayscale). Used by BC3 for alpha, but also standalone for height/AO.

**Best for**: Grayscale height maps, occlusion maps.

### BC5 (3Dc)

10:1 compression, two-channel (RG). RGB is reconstructed as `normal.z = sqrt(1 - dot(normal.xy, normal.xy))` in shader. Essential for normal maps on DirectX 11+.

```
Block: 16 bytes
├── BC4 for R channel (8 bytes)
└── BC4 for G channel (8 bytes)
```

**Best for**: Normal maps (industry standard, saves memory).

### BC6H (BPTC Float)

16:1 compression, unsigned half-float (16-bit per channel). Use for HDR environment maps, lightmaps.

**Best for**: HDR cubemaps, bloom textures, reflection probes.

### BC7 (BPTC)

10:1 compression, highest quality RGBA. Slow to compress (Intel ISPC), fast to decompress. Use for critical art assets.

**Best for**: Character diffuse, environment albedo, any sRGB texture where quality matters.

## Color Space Detection

Auto-detect sRGB vs Linear based on filename or metadata.

```cpp
bool InferSRGB(StringView filename) {
    // Patterns indicating sRGB (color data)
    if (filename.contains("_albedo") || filename.contains("_diffuse") ||
        filename.contains("_color") || filename.contains("_emissive")) {
        return true;
    }

    // Patterns indicating Linear (data)
    if (filename.contains("_normal") || filename.contains("_rough") ||
        filename.contains("_metallic") || filename.contains("_ao") ||
        filename.contains("_height") || filename.contains("_opacity")) {
        return false;
    }

    // Default: assume linear (safer for data textures)
    return false;
}
```

## Mipmap Generation

Recursive downsampling with appropriate filter per color space.

```cpp
/// Generate mipmap chain from base texture.
/// sRGB textures use Kaiser filter (perceptually better).
/// Linear textures use box filter (faster, fine for data).
std::vector<uint8_t> GenerateMipmaps(
    const uint8_t* base_data,
    uint16_t base_width,
    uint16_t base_height,
    uint8_t mip_count,
    bool is_srgb,
    bool preserve_alpha_coverage  // For alpha-to-coverage materials
) {
    std::vector<uint8_t> output;

    uint8_t* current = base_data;
    uint16_t w = base_width, h = base_height;

    for (uint8_t mip = 0; mip < mip_count; ++mip) {
        // Copy or filter current mip
        if (mip == 0) {
            output.insert(output.end(), current, current + w * h * 4);
        } else {
            // Downsample from previous mip
            uint8_t* next = output.data() + output.size();
            uint16_t next_w = std::max(1u, w / 2);
            uint16_t next_h = std::max(1u, h / 2);

            if (is_srgb) {
                ResampleKaiser(current, w, h, next, next_w, next_h);
            } else {
                ResampleBox(current, w, h, next, next_w, next_h);
            }

            output.resize(output.size() + next_w * next_h * 4);
            w = next_w;
            h = next_h;
        }
    }

    return output;
}
```

## Normal Map Compression

BC5 (two-channel) is standard for desktop; reconstruct Z in shader.

```glsl
// Fragment shader: reconstruct Z from BC5-compressed XY
vec3 DecodeNormalBC5(vec2 encoded_xy) {
    // BC5 encodes normal.xy as-is, reconstruct Z
    float z = sqrt(max(0.0, 1.0 - dot(encoded_xy, encoded_xy)));
    return normalize(vec3(encoded_xy, z));
}
```

## Virtual Texturing (Optional, Phase 2+)

Streaming large textures on-demand instead of loading all to VRAM.

```cpp
struct VirtualTextureDesc {
    uint16_t page_tile_size;   // 128 or 256 pixels
    uint16_t mip_bias;         // For streaming: biased LOD

    /// Request pages based on shader feedback (per-frame).
    void RequestPages(std::vector<uint2> page_coords);

    /// Prepare GPU page buffer (sparse texture descriptor).
    void UpdatePageBuffer(RHI::CommandList& cmd);
};

// Usage: in material shader, sample with feedback
// layout(binding = FEEDBACK_BINDING, r32ui) uniform uimage2D feedback_buffer;
// vec4 color = sample_virtual_texture(page_buffer, page_feedback, ...);
// imageAtomicExchange(feedback_buffer, coord, new_page_request);
```

## Import Settings (.import.json Sidecar)

```json
{
    "version": 1,
    "texture": {
        "source": "stone_albedo.png",
        "force_srgb": true,
        "max_size": 2048,
        "compression": "BC7",
        "quality": "high",
        "virtual_texture": false,
        "generate_mipmaps": true
    }
}
```

## Hot-Reload in Editor

Detect file change → recompress → update GPU resource.

```cpp
void OnTextureSourceChanged(StringView asset_path) {
    // Recompute mip chain
    ImportTexture(asset_path + ".png", asset_path + ".uztexture", config);

    // Reload GPU resource
    auto* texture = ResourceSystem::Get().Reload(asset_path);

    // Update all dependent materials
    auto dependents = HotReloadSystem::GetDependents(asset_path);
    for (auto& mat_path : dependents) {
        ResourceSystem::Reload(mat_path);  // Material shader recompiles with new texture
    }

    // Notify editor viewport to refresh
    EditorViewport::RequestRedraw();
}
```

## GPU Resource Creation

```cpp
class Texture {
private:
    uint16_t width_, height_;
    std::vector<uint8_t> mip_data_;
    CompressionFormat format_;

public:
    /// Upload texture and mipmap chain to GPU.
    bool UploadToGPU(RHI::Device& device) {
        // Create D3D12 texture with mip levels
        D3D12_RESOURCE_DESC desc = {
            .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            .Width = width_,
            .Height = height_,
            .DepthOrArraySize = 1,
            .MipLevels = static_cast<uint16_t>(mip_data_.size()),
            .Format = ConvertCompressionFormat(format_),  // DXGI_FORMAT_BC7_UNORM, etc.
            .SampleDesc = {1, 0},
            .Usage = D3D12_USAGE_DEFAULT,
        };

        ID3D12Resource* resource;
        HRESULT hr = device->CreateResource(&desc, ..., &resource);
        if (FAILED(hr)) return false;

        // Upload mip data via UpdateSubresources (multi-threaded command list)
        // ... upload each mip to GPU memory ...

        return true;
    }
};
```

## Runtime Texture Loading API

The `TextureLoader` class provides a high-level one-call API for loading image files from disk to GPU at runtime. It wraps stbi decoding, texture creation, upload, and synchronization into a single function call.

### TextureLoader::Load (Synchronous)

```cpp
/// Load a texture from disk to GPU memory (blocking).
///
/// This is the primary runtime texture loading function. It:
/// 1. Loads image from disk (PNG, TGA, JPEG via stb_image)
/// 2. Detects color space from filename pattern (e.g., *_albedo → sRGB)
/// 3. Generates mipmaps via box or Kaiser filter
/// 4. Uploads to GPU via command list
/// 5. Issues GPU barrier (transition to shader read)
/// 6. Waits for GPU idle before returning
///
/// # Arguments
/// * device - RHI device for texture creation
/// * cmd_list - RHI command list for GPU upload (should be open)
/// * path - File path relative to VFS root (e.g., "textures/stone_albedo.png")
///
/// # Returns
/// - Valid `RHITextureHandle` on success (GPU texture ready to use)
/// - `RHITextureHandle::Invalid()` on failure (logs reason to Asset::Error)
///
/// # Errors
/// Logs Asset::Error on:
/// - File not found in VFS
/// - stb_image decode failure (corrupted or unsupported format)
/// - GPU resource creation failure (out of VRAM)
/// - Device loss or command list error
///
/// # Performance
/// ~50–200ms on RTX 4090 depending on image size and mipmap chain.
/// For large batches, use `LoadAsync()` instead.
///
/// # Example
/// ```cpp
/// TextureLoader loader;
/// auto tex_handle = loader.Load(device, cmd_list, "textures/diffuse.png");
/// if (tex_handle.IsValid()) {
///     material.SetAlbedoTexture(tex_handle);
/// }
/// ```
RHITextureHandle Load(
    IRHIDevice* device,
    IRHICommandList* cmd_list,
    const std::string& path
);
```

### TextureLoader::LoadAsync (Asynchronous)

```cpp
/// Load a texture from disk to GPU memory (asynchronous).
///
/// Returns immediately; texture loading happens on a background thread.
/// The callback fires when loading completes (success or failure).
///
/// Useful for streaming assets during gameplay without frame hitches.
///
/// # Arguments
/// * device - RHI device for texture creation
/// * path - File path relative to VFS root
/// * on_complete - Callback: void(RHITextureHandle result, bool success)
///
/// # Errors
/// Callback receives `RHITextureHandle::Invalid()` and `success=false` on failure.
///
/// # Example
/// ```cpp
/// loader.LoadAsync(device, "textures/sky.png",
///     [this](RHITextureHandle h, bool ok) {
///         if (ok) sky_texture_ = h;
///     });
/// ```
void LoadAsync(
    IRHIDevice* device,
    const std::string& path,
    std::function<void(RHITextureHandle, bool)> on_complete
);
```

### Current Duplication Problem

Today, runtime texture loading is scattered across demos with ~30 lines of boilerplate per texture:

```cpp
// In pbr_test_demo.cpp — loading stone_diffuse.png
std::vector<uint8_t> image_data = ...;  // Read from disk
int width, height, channels;
uint8_t* pixels = stbi_load("textures/stone_diffuse.png", &width, &height, &channels, 4);
if (!pixels) return error(...);

D3D12_RESOURCE_DESC desc = {...};  // Set up descriptor
ID3D12Resource* gpu_texture = nullptr;
device_->CreateCommittedResource(&desc, ..., &gpu_texture);

// Upload to GPU via staging buffer + copy
D3D12_RESOURCE_BARRIER barrier = {...};
cmd_list_->ResourceBarrier(1, &barrier);  // Transition to shader read

WaitForGPU();  // Sync point
stbi_image_free(pixels);  // Cleanup
```

This pattern is **repeated 4+ times** across pbr_test, deferred_test, textured_quad demos with subtle variations causing maintenance bugs.

**Problems**:
- Manually managing stbi_load/stbi_image_free (leak risk)
- Manual D3D12 descriptor setup per texture
- No color space detection → manual sRGB/linear decision
- No mipmap generation at runtime → LOD bias required
- Manual synchronization (barrier, WaitForGPU) error-prone

### TextureLoader Implementation Notes

The engine already has the building blocks in `resource_system/texture/`:
- `texture_importer.h` — PNG/TGA/JPEG decoding + .uztexture writing
- `upload_context.h` — GPU staging buffer management
- `resource_manager.h` — caching and lifetime management

**TextureLoader bridges these**:

```cpp
class TextureLoader {
private:
    TextureImporter importer_;
    UploadContext upload_context_;
    ResourceManager& resource_mgr_;

public:
    RHITextureHandle Load(IRHIDevice* device, IRHICommandList* cmd_list,
                          const std::string& path) {
        // 1. Import PNG → .uztexture (in-memory)
        auto import_result = importer_.Import(ReadFile(path), path, {});
        if (!import_result.success) {
            UZ_LOG(Asset, Error, "Failed to import texture: path='{}'", path);
            return RHITextureHandle::Invalid();
        }

        // 2. Parse .uztexture header
        UzTextureHeader header = ParseHeader(import_result.data);

        // 3. Create GPU texture with mips
        RHITextureHandle handle = device->CreateTexture2D(header.width, header.height,
                                                          header.mipCount, DXGI_FORMAT_R8G8B8A8_UNORM);

        // 4. Upload all mips via staging buffer
        upload_context_.UploadTextureData(cmd_list, handle, import_result.data);

        // 5. Cache in resource manager
        resource_mgr_.CacheTexture(path, handle);

        UZ_LOG(Asset, Info, "Loaded texture: path='{}', size={}x{}, mips={}",
               path, header.width, header.height, header.mipCount);
        return handle;
    }
};
```

## Status

This API is **Planned** for Phase 1.5. Currently, the individual pieces (TextureImporter, UploadContext) exist but are not integrated into a single `TextureLoader::Load()` function. Demos must manually assemble the pipeline.

## Memory Budget & Streaming

Under memory pressure, reduce mip bias or switch to lower-quality compression.

```cpp
struct MemoryBudget {
    size_t vram_limit = 2_GB;         // Total VRAM available
    size_t texture_pool_limit = 1_GB; // Reserve for textures
    float mip_bias = 0.0f;            // Increased = lower detail (save memory)
};

void OnMemoryPressure(const MemoryBudget& budget) {
    if (total_texture_memory > budget.texture_pool_limit) {
        // Increase mip bias → lower mips sampled
        mip_bias += 0.5f;

        // Unload textures farthest from camera
        UnloadDistantTextures();

        UZ_LOG(Resource, Warning, "Texture memory pressure: mip_bias={}, unloaded {} textures",
               mip_bias, unloaded_count);
    }
}
```

## Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| Import formats | PNG, TGA, JPEG, EXR, OpenEXR | PNG, JPG, WebP via ResourceImporter | PNG, JPG, TGA, EXR | **PNG, TGA, HDR, EXR, JPEG** |
| Compression | DirectXTex (BC7/BC6H auto) | ResourceCompressor (simple) | texture compression presets | **ispc_texcomp + DirectXTex** |
| Normal maps | BC5 (two-channel) | BC5 (DirectXTex integration) | Normal compressed | **BC5 with Z reconstruction** |
| HDR support | BC6H (unsigned half-float) | Limited (EXR raw) | Float format | **BC6H for HDR cubemaps** |
| Mipmaps | Automatic Kaiser filter | Automatic box | Automatic | **Kaiser (sRGB) + Box (linear)** |
| Virtual texturing | Yes (Nanite + Tex Streaming) | No built-in | Streamable Assets | **Optional (Phase 2+)** |
| Color space | Explicit sRGB setting | Detect from extension | sRGB setting per texture | **Auto-detect + .import.json override** |
| Streaming | Texture streaming pool | Manual via GDScript | Streaming Assets | **Mip streaming or virtual textures** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 1.5** | PNG/TGA/JPEG loading, mipmap generation (box filter), BC1/BC7 compression (ispc_texcomp), GPU upload |
| **Phase 2** | HDR/EXR support, BC5 normal maps, Kaiser filter for sRGB, hot-reload in editor, import config (.import.json) |
| **Phase 2.5** | Virtual texturing framework, streaming page requests, memory budget tracking, mip bias adjustment |
| **Phase 3** | Advanced: cubemap processing (convolution, SH baking), texture atlasing, array textures for bindless rendering |
