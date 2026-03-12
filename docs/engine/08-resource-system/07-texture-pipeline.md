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

Binary format with mip chain for direct GPU upload and streaming.

```cpp
/// Texture file header.
struct UzTextureHeader {
    uint32_t magic = 'UZTX';     // Magic number (0x5A544958)
    uint32_t version = 1;         // Format version
    uint16_t width, height;       // Base texture dimensions
    uint8_t mip_count;            // Number of mip levels
    uint8_t format;               // Compression format (BC1, BC3, BC5, BC7, BC6H, etc.)
    uint32_t flags;               // sRGB? Alpha? Cubemap? Normal map?
};

/// Compression format enum.
enum class CompressionFormat : uint8_t {
    Uncompressed_RGBA8,           // No compression (rarely used)
    BC1_RGB,                      // DXT1, RGB only, 6:1 compression
    BC1_RGBA,                     // DXT1 with 1-bit alpha
    BC3_RGBA,                     // DXT5, RGBA with BC4 alpha
    BC4_R,                        // Two-channel R (height maps)
    BC5_RG,                       // Two-channel RG (normal maps, reconstruct Z)
    BC6H_UFLOAT,                  // Unsigned half-float (HDR, 16:1)
    BC7_RGBA,                     // High-quality RGBA (10:1)
};

/// Per-mip metadata.
struct MipLevel {
    uint32_t offset;              // Byte offset in file
    uint32_t size;                // Compressed size (bytes)
    uint16_t width, height;       // Dimensions at this mip
};

// Layout in file:
// [Header: 32 bytes]
// [MipLevel[mip_count]: ~12 bytes each]
// [Mip0 data]
// [Mip1 data]
// [Mip2 data]
// ...
```

## Import Pipeline API

```cpp
namespace uz::asset {
    /// Texture import configuration (usually from .import.json sidecar).
    struct TextureImportConfig {
        /// Force sRGB color space (gamma-corrected for display).
        bool force_srgb = false;

        /// Force linear color space (raw data, no gamma correction).
        bool force_linear = false;
        // If both false, auto-detect from filename or assume linear

        /// Max texture dimension (downscale if larger).
        uint16_t max_size = 4096;

        /// Generate mipmap chain.
        bool generate_mipmaps = true;

        /// Minimum mip dimension (stop at 1×1 or user-specified).
        uint8_t min_mip = 1;  // 1×1

        /// Prefer streaming (virtual texture) over all-in-memory.
        bool virtual_texture = false;

        /// If virtual, page tile size (128 or 256).
        uint16_t page_tile_size = 128;

        /// Alpha is coverage (opacity-weighted MSAA). Preserve coverage in mipmaps.
        bool alpha_is_coverage = false;

        /// Compression quality (0–2: fast, medium, high).
        uint8_t quality = 1;  // medium
    };

    /// Result of texture import.
    struct ImportResult {
        bool success;
        std::string error_message;
        std::vector<std::string> warnings;
        size_t output_bytes;      // Size of .uztexture
        std::vector<uint32_t> mip_sizes;  // Per-mip compressed size
    };

    /// High-level import function.
    ImportResult ImportTexture(
        StringView input_path,           // e.g., "res://textures/stone_albedo.png"
        StringView output_path,          // e.g., ".cache/textures/stone_albedo.uztexture"
        const TextureImportConfig& config
    );
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
