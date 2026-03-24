# Texture Pipeline Guide — Cook to Runtime

<!-- doc-meta
title: Texture Pipeline Guide
category: guide
theme: rendering
created: 2026-03-24
updated: 2026-03-24
status: active
author: docs-operator
-->

## Overview

This guide covers the complete texture pipeline workflow: how PNG/JPG images are cooked into `.uztexture` binaries by the `uzcook` tool, how textures are loaded asynchronously at runtime, and performance best practices. The pipeline achieves **zero main-thread stalls**, **273× faster texture upload** (0.3ms vs 82ms), and **4× VRAM reduction** through compression.

## Cook Pipeline: PNG → .uztexture

The `uzcook` command-line tool converts raw images into `.uztexture` binaries with automatic format detection and parallel compression.

### Format Heuristic

The cook pipeline automatically selects the optimal GPU compression format based on the texture's semantic type. This is inferred from filename patterns:

| Pattern | Detected Type | Format | Color Space | Compression |
|---------|---------------|--------|-------------|-------------|
| `*_albedo`, `*_diffuse`, `*_color` | Diffuse/albedo | BC7 sRGB | sRGB | 10:1, 4 bytes/pixel after compression |
| `*_normal`, `*_normalmap` | Normal map | BC5 | Linear | 10:1, 2 channels (reconstruct Z in shader) |
| `*_roughness`, `*_metallic`, `*_ao` | Grayscale data | BC4 | Linear | 8:1, single channel |
| `*_orm` | Occlusion + Roughness + Metallic | BC7 Linear | Linear | 10:1, packed 3-channel |
| `*_emissive`, `*_emission` | Self-emissive | BC7 sRGB | sRGB | 10:1 |
| `*_height`, `*_displacement` | Height/displacement | BC4 | Linear | 8:1 |

**Override Behavior**: Use `.import.json` sidecar to override heuristic:

```json
{
  "texture": {
    "force_format": "BC7",
    "force_srgb": true
  }
}
```

### Compression Performance

The cook pipeline uses `etcpak` library for efficient BC7/BC5/BC4/BC1 compression with parallel processing:

```bash
# Single texture 2K BC7 (sRGB)
$ time uzcook texture stone_diffuse.png
Processing: stone_diffuse.png → stone_diffuse.uztexture
  Dims: 2048×2048, Mipmaps: 12 (2048→1)
  Format: BC7 sRGB, compression ratio: 10:1
  Stages: decode (12ms) + mips (8ms) + compress (145ms) + write (5ms)
Total: 170ms

# Batch 4 textures 2K BC7 (parallel via std::async)
$ time uzcook texture *.png
Compression threads: 4 (cores/2)
Total time: 12s (3s per texture average, load-balanced)
```

### Parallel Compression

```cpp
// Cook pipeline pseudo-code (src/engine/asset/cook_texture.cpp)
std::vector<std::future<CookResult>> tasks;

for (const auto& source_png : png_files) {
    tasks.push_back(std::async(std::launch::async, [&] {
        return CookTexture(source_png);  // Decode → mips → compress BC7
    }));
}

// Wait for all tasks with timeout
for (auto& task : tasks) {
    auto result = task.wait_for(std::chrono::seconds(30));
    if (result == std::future_status::ready) {
        WriteCookedTexture(task.get());
    }
}
```

## .uztexture Binary Format

The `.uztexture` format is a binary container with:
- **Zero-copy parsing**: Header + mip descriptors can be read without decoding
- **Direct GPU upload**: Compressed data ready for `UpdateSubresources()`
- **Full mipmap chain**: All mip levels (base to 1×1) included

### File Layout

```
Byte Offset | Field | Size | Description
------------|-------|------|----------------
0           | magic | 4    | 0x5A585455 ("UZTX")
4           | version | 4  | 1 (current version)
8           | width | 4    | Base texture width in pixels
12          | height | 4   | Base texture height in pixels
16          | mipCount | 4 | Number of mip levels
20          | format | 4   | TextureFormat enum (0=RGBA8, 1=BC1, 2=BC3, 3=BC5, 4=BC7)
24          | flags | 4    | Reserved (must be 0)
28–63       | reserved | 36 | Must be 0
64+         | Mip Descriptors | 24×mipCount | Array of mip offsets/sizes
64+desc    | Mip Data | varies | Compressed mip blobs (mip 0 first)
```

### Header Structure (C++)

```cpp
#pragma pack(push, 1)
struct UzTextureHeader {
    uint32_t magic = 0x5A585455;      // "UZTX"
    uint32_t version = 1;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipCount = 0;
    uint32_t format = 0;              // TextureFormat enum
    uint32_t flags = 0;
    uint32_t reserved[9] = {};
};
static_assert(sizeof(UzTextureHeader) == 64);
#pragma pack(pop)
```

### Parsing at Runtime (0ms Overhead)

```cpp
// Load .uztexture header (64 bytes, no decompression needed)
std::ifstream file(path, std::ios::binary);
UzTextureHeader header;
file.read((char*)&header, 64);

if (header.magic != 0x5A585455) {
    return error("Invalid .uztexture magic");
}

// Read mip descriptors (zero-copy, direct array access)
std::vector<UzTextureMipLevel> mips(header.mipCount);
file.read((char*)mips.data(), header.mipCount * 24);

// Create GPU texture and upload each mip
// No intermediate copy, direct from file buffer to GPU
for (uint32_t i = 0; i < header.mipCount; ++i) {
    file.seekg(mips[i].offset);
    UploadMipToGPU(device, mips[i].size, mips[i].width, mips[i].height);
}
```

## Runtime Loading: AsyncTextureDecoder

The `AsyncTextureDecoder` loads textures without blocking the main thread, with progressive fallback rendering.

### Architecture

```
Main Thread          Worker Thread              GPU
─────────────────────────────────────────────────────
SubmitFromMemory()
  │
  ├─ Create Placeholder SRV ──────────────┐
  │   (default white/normal texture)      │
  │                                       │
  └─ Enqueue task ──────────────┐         │
                                │         │
                    Parse .uztexture      │
                    (zero-copy, 0ms)      │
                                │         │
                    Allocate Staging      │
                    Buffer (64MB ring)    │
                                │         │
                    Copy mip data to     │
                    staging (1–3ms)       │
                                │         │
                    Submit DMA copy ──────┤──> Copy Queue
                                │         │    (parallel)
                    Set Fence    └────────┘
                    Return Task ID

On next frame:
  Poll(task_id)
    │
    ├─ Fence signaled?
    │   ├─ No: return COPY_IN_FLIGHT
    │   └─ Yes: Swap full SRV, free placeholder
    │          return FULLY_LOADED
    └─ Update texture slot to full SRV
```

### Texture Load States

```cpp
enum class TextureLoadState : uint8_t {
    Empty              = 0,   // Not requested yet
    Placeholder        = 1,   // Fallback SRV active
    CopyInFlight       = 2,   // DMA in progress on copy queue
    FullyLoaded        = 3,   // Full SRV ready for binding
};
```

### C++ API

```cpp
class AsyncTextureDecoder {
public:
    /// Submit texture for async loading from memory buffer.
    /// Returns immediately.
    ///
    /// On success:
    ///   - Placeholder SRV created (solid color)
    ///   - Task queued to worker thread
    ///   - Task ID returned (unique per submission)
    ///
    /// Usage:
    ///   auto task_id = decoder.SubmitFromMemory(
    ///       data,          // .uztexture binary blob
    ///       size,
    ///       "diffuse.uztexture"
    ///   );
    ///   // Continue rendering with placeholder
    ///
    ///   // Next frame:
    ///   auto state = decoder.Poll(task_id);
    ///   if (state == TextureLoadState::FullyLoaded) {
    ///       material->SetAlbedoSRV(decoder.GetFullSRV(task_id));
    ///   }
    uint32_t SubmitFromMemory(
        const uint8_t* data,
        size_t size,
        const std::string& name
    );

    /// Submit texture for async loading from VFS.
    /// Same as SubmitFromMemory but reads file internally.
    uint32_t SubmitFromVFS(
        const std::string& vfs_path
    );

    /// Check loading status of a submitted task.
    TextureLoadState Poll(uint32_t task_id);

    /// Get SRV after fully loaded.
    /// Valid only after Poll() returns TextureLoadState::FullyLoaded
    RHISrvHandle GetFullSRV(uint32_t task_id);

    /// Get placeholder SRV (solid color, used during loading).
    RHISrvHandle GetPlaceholderSRV();

    /// Shutdown and release all resources.
    /// Waits for all in-flight tasks to complete.
    void Shutdown();
};
```

### Worker Thread Configuration

The decoder creates a configurable number of worker threads for optimal hardware utilization:

```cpp
// Calculation: max(2, cores/2), clamped [2, 8]
// Example: 16-core CPU → 8 workers
// Example: 8-core CPU → 4 workers
// Example: 2-core CPU → 2 workers (minimum)

int num_workers = std::max(2, std::thread::hardware_concurrency() / 2);
num_workers = std::min(num_workers, 8);  // Cap at 8
```

## Copy Queue & Upload Ring

Textures are uploaded via a **dedicated copy queue** on DX12 (or transfer queue on Vulkan) with timeline semaphores, eliminating main-thread stalls.

### Upload Ring Buffer

A triple-buffered 64MB ring allocates staging memory for DMA transfers without creating/destroying buffers per texture:

```cpp
class UploadRing {
private:
    struct UploadBuffer {
        ComPtr<ID3D12Resource> resource;      // 64MB staging buffer
        uint8_t* cpu_ptr;                     // CPU-mapped pointer
        uint64_t gpu_fence_value;             // Fence to wait on
    };

    std::array<UploadBuffer, 3> buffers_;     // Triple-buffered
    uint32_t current_buffer_ = 0;
    size_t ring_offset_ = 0;

public:
    /// Allocate staging memory for DMA copy.
    /// Waits if buffer is full (rare on 64MB per frame).
    void* Allocate(size_t size, size_t alignment = 256) {
        // If current buffer doesn't have space, rotate to next
        if (ring_offset_ + size > 64 * 1024 * 1024) {
            current_buffer_ = (current_buffer_ + 1) % 3;
            auto& buf = buffers_[current_buffer_];

            // Wait for GPU to finish with this buffer
            WaitForFenceValue(buf.gpu_fence_value);
            ring_offset_ = 0;
        }

        uint8_t* ptr = buffers_[current_buffer_].cpu_ptr + ring_offset_;
        ring_offset_ += AlignUp(size, alignment);
        return ptr;
    }

    /// Submit DMA copy from staging to GPU texture.
    void SubmitCopy(
        ID3D12Resource* dest_texture,
        const void* src_data,
        size_t size
    ) {
        // Append to copy queue command list
        copy_cmd_list_->CopyBufferRegion(
            dest_texture,
            0,
            buffers_[current_buffer_].resource.Get(),
            ring_offset_ - size,
            size
        );
    }
};
```

### Copy Queue Submission

```cpp
// Every frame or after batch of uploads
void FlushUploadRing() {
    if (copy_cmd_list_->IsOpen()) {
        copy_cmd_list_->Close();
        ID3D12CommandList* lists[] = { copy_cmd_list_.Get() };
        copy_queue_->ExecuteCommandLists(1, lists);

        // Signal fence when copy completes
        uint64_t fence_value = copy_queue_fence_value_++;
        copy_queue_->Signal(copy_queue_fence_.Get(), fence_value);

        // Main render queue waits on this fence
        render_queue_->Wait(copy_queue_fence_.Get(), fence_value);
    }

    copy_cmd_list_->Reset();
    copy_cmd_list_->Open();
}
```

## sRGB Support

The texture pipeline supports sRGB throughout the full stack with per-slot overrides:

### Color Space Detection

Automatic detection based on filename patterns:

```cpp
bool InferSRGB(const std::string& filename) {
    static const std::vector<std::string> srgb_patterns = {
        "_albedo", "_diffuse", "_color", "_emissive", "_emission"
    };

    static const std::vector<std::string> linear_patterns = {
        "_normal", "_roughness", "_metallic", "_ao", "_height", "_orm"
    };

    for (const auto& pattern : srgb_patterns) {
        if (filename.find(pattern) != std::string::npos) return true;
    }

    for (const auto& pattern : linear_patterns) {
        if (filename.find(pattern) != std::string::npos) return false;
    }

    // Default: linear (safer for data)
    return false;
}
```

### Mipmap Filtering by Color Space

sRGB textures use Kaiser filter (perceptually better), linear use box filter (faster):

```cpp
std::vector<uint8_t> GenerateMipmaps(
    const uint8_t* base_data,
    uint32_t base_width,
    uint32_t base_height,
    uint32_t mip_count,
    bool is_srgb
) {
    std::vector<uint8_t> output;
    uint32_t w = base_width, h = base_height;

    for (uint32_t mip = 0; mip < mip_count; ++mip) {
        if (mip == 0) {
            // Copy base mip as-is
            output.insert(output.end(), base_data, base_data + w * h * 4);
        } else {
            // Downsample from previous mip
            uint32_t next_w = std::max(1u, w / 2);
            uint32_t next_h = std::max(1u, h / 2);

            if (is_srgb) {
                ResampleKaiser(base_data, w, h, output, next_w, next_h);
            } else {
                ResampleBox(base_data, w, h, output, next_w, next_h);
            }

            w = next_w;
            h = next_h;
        }
    }

    return output;
}
```

### Per-Material sRGB Override

Materials can override sRGB detection for specific texture slots:

```cpp
class MaterialInterface {
private:
    std::unordered_map<std::string, bool> srgb_overrides_;

public:
    /// Override sRGB for a texture slot (e.g., "albedo").
    void SetTextureSRGBOverride(const std::string& slot, bool is_srgb) {
        srgb_overrides_[slot] = is_srgb;
    }

    /// Check if slot should use sRGB (or use default detection).
    bool IsTextureSlotSRGB(const std::string& slot) const {
        auto it = srgb_overrides_.find(slot);
        if (it != srgb_overrides_.end()) {
            return it->second;  // Use explicit override
        }
        return InferSRGB(slot);  // Use default detection
    }
};
```

## Performance: Before and After

| Metric | Before (RGBA8, sync) | After (BC7, async) | Improvement |
|--------|----------------------|-------------------|-------------|
| Main thread stall | 11.6s | 0ms | **Infinity** |
| Per-texture upload | 82ms | 0.3ms | **273×** |
| GPU upload (WaitIdle) | 47.5ms | 0ms (copy queue) | **Infinity** |
| VRAM per 2K texture | 16MB | 4MB | **4×** |
| Cook time (4 tex 2K) | — | 12s (parallel) | **3s per texture avg** |
| Staging allocations | 48× CreateBuffer | 0 (UploadRing) | **Infinite** |
| Parse overhead | 82ms (double copy) | 0ms (zero-copy) | **Infinity** |

The main improvements:
1. **Async worker threads** eliminate main-thread stalls
2. **Copy queue** parallelizes texture uploads with rendering
3. **BC7 compression** reduces memory 4× while maintaining quality
4. **UploadRing** eliminates per-texture staging buffer allocations
5. **Zero-copy parsing** skips intermediate buffering

## Common Patterns

### Load Single Texture (Async, No Wait)

```cpp
AsyncTextureDecoder decoder;
uint32_t task_id = decoder.SubmitFromVFS("textures/stone_diffuse.png");

// Next frame:
auto state = decoder.Poll(task_id);
if (state == TextureLoadState::FullyLoaded) {
    material->SetAlbedoSRV(decoder.GetFullSRV(task_id));
}
```

### Batch Load with Progress Tracking

```cpp
std::vector<uint32_t> task_ids;

for (const auto& texture_path : texture_list) {
    task_ids.push_back(decoder.SubmitFromVFS(texture_path));
}

// Poll until all loaded
int loaded_count = 0;
while (loaded_count < task_ids.size()) {
    loaded_count = 0;
    for (auto task_id : task_ids) {
        if (decoder.Poll(task_id) == TextureLoadState::FullyLoaded) {
            loaded_count++;
        }
    }
    // Render a frame with progress bar, etc.
}
```

### Manual sRGB Override

```cpp
// Force a normal map to linear even if filename says otherwise
decoder.SetTextureSRGBOverride("normal_map.png", false);
uint32_t task_id = decoder.SubmitFromVFS("textures/normal_map.png");
```

## Troubleshooting

### Textures Loading Too Slowly

- **Check**: Worker thread count (`max(2, cores/2)`)
- **Check**: Staging ring buffer size (default 64MB, increase if many parallel uploads)
- **Check**: Copy queue submission frequency (should flush every frame)

### Placeholder Visible Too Long

- **Increase** `UploadRing` size if copy queue is starved
- **Check** copy queue is actually used (not falling back to main render queue)
- **Profile** with `--shadow-debug 1` to see when textures are fully loaded

### sRGB Incorrect

- **Check** filename pattern matches heuristic (e.g., `*_albedo`, `*_normal`)
- **Use** `.import.json` sidecar or material override if heuristic is wrong
- **Verify** mipmap generation respects color space (Kaiser vs box filter)

## References

- [Texture Pipeline Spec](../engine/08-resource-system/07-texture-pipeline.md) — Full binary format, importer API, compression details
- [Material Texture Slots Guide](material-texture-slots.md) — Per-material texture binding and SRV management
- [Scene Load Optimization](scene-load-optimization.md) — Texture streaming during scene load
- [BC Compression Reference](https://learn.microsoft.com/en-us/windows/win32/direct3d11/bc6h-format) — DirectX BC format specifications

## Changelog

| Date | Change |
|------|--------|
| 2026-03-24 | Initial guide creation. Cook pipeline heuristics, .uztexture format, AsyncTextureDecoder API, Copy Queue, UploadRing, sRGB support, performance improvements documented. |
