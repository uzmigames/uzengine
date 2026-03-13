#pragma once

/// Render Graph Resource Pool — Transient texture/buffer pooling and aliasing.
///
/// Physical GPU resources (textures and buffers) are expensive to create and destroy
/// every frame. The resource pool maintains a cache of previously allocated resources
/// keyed by their descriptor, allowing the render graph to reuse physical allocations
/// across frames.
///
/// # Transient Resource Aliasing
/// Resources whose lifetimes do not overlap in the topologically sorted pass order
/// can share the same physical allocation. The pool assigns physical resources based
/// on descriptor compatibility and lifetime intervals computed by the compiler.
///
/// # Lifetime
/// The pool is persistent (survives across frames). Physical resources are created
/// on first use and returned to the pool after the GPU finishes the frame (signaled
/// by fence). The pool can be trimmed to release unused resources.

#include <vector>
#include <unordered_map>

#include "../../rhi/rhi.h"
#include "render_graph_types.h"

namespace uz::rg {

// ─────────────────────────────────────────────────────────────────────────────
// RgResourcePool — cross-frame physical resource cache
// ─────────────────────────────────────────────────────────────────────────────

/// Manages a pool of physical RHI textures and buffers for reuse across frames.
///
/// During `Execute()`, the render graph acquires physical resources from the pool
/// (creating new ones if none are available). After the GPU completes the frame,
/// resources are returned to the pool for reuse by future frames.
///
/// # Aliasing
/// The pool supports transient resource aliasing: if two virtual resources have
/// non-overlapping lifetimes and compatible descriptors, they can share the same
/// physical allocation. The compiler computes aliasing assignments; the pool
/// simply provides and recycles the physical handles.
///
/// # Thread Safety
/// Not thread-safe. Only the render graph's `Execute()` path should call into the pool.
class RgResourcePool {
public:
    /// Constructs a resource pool that allocates from the given RHI device.
    ///
    /// # Arguments
    /// * `device` — The RHI device used to create/destroy physical resources.
    ///   Must outlive the pool.
    explicit RgResourcePool(IRHIDevice* device);

    /// Destroys the pool, releasing all cached physical resources.
    ~RgResourcePool();

    // Non-copyable, non-movable (owns GPU resources).
    RgResourcePool(const RgResourcePool&) = delete;
    RgResourcePool& operator=(const RgResourcePool&) = delete;

    /// Acquires a physical texture matching the given descriptor.
    ///
    /// Returns a cached texture if one with a compatible descriptor is available,
    /// otherwise creates a new one via `IRHIDevice::CreateTexture()`.
    ///
    /// # Arguments
    /// * `desc` — The virtual texture descriptor to match against.
    /// * `debugName` — Debug label for the physical texture.
    ///
    /// # Returns
    /// A valid `RHITextureHandle`. Asserts on allocation failure.
    [[nodiscard]] RHITextureHandle AcquireTexture(const RgTextureDesc& desc, const char* debugName);

    /// Acquires a physical buffer matching the given descriptor.
    ///
    /// Returns a cached buffer if one with a compatible descriptor is available,
    /// otherwise creates a new one via `IRHIDevice::CreateBuffer()`.
    ///
    /// # Arguments
    /// * `desc` — The virtual buffer descriptor to match against.
    /// * `debugName` — Debug label for the physical buffer.
    ///
    /// # Returns
    /// A valid `RHIBufferHandle`. Asserts on allocation failure.
    [[nodiscard]] RHIBufferHandle AcquireBuffer(const RgBufferDesc& desc, const char* debugName);

    /// Returns a physical texture to the pool for future reuse.
    ///
    /// # Arguments
    /// * `handle` — The texture handle to return. Must have been acquired from this pool.
    /// * `desc` — The descriptor used when the texture was acquired (for cache key).
    void ReleaseTexture(RHITextureHandle handle, const RgTextureDesc& desc);

    /// Returns a physical buffer to the pool for future reuse.
    ///
    /// # Arguments
    /// * `handle` — The buffer handle to return. Must have been acquired from this pool.
    /// * `desc` — The descriptor used when the buffer was acquired (for cache key).
    void ReleaseBuffer(RHIBufferHandle handle, const RgBufferDesc& desc);

    /// Destroys all cached (unused) resources to free VRAM.
    ///
    /// Does not affect resources currently acquired by the render graph.
    /// Call periodically or on memory pressure to reclaim unused pool entries.
    void Trim();

    /// Returns the number of textures currently cached (not acquired).
    [[nodiscard]] uint32_t GetCachedTextureCount() const;

    /// Returns the number of buffers currently cached (not acquired).
    [[nodiscard]] uint32_t GetCachedBufferCount() const;

private:
    /// Hash key combining descriptor fields for cache lookup.
    struct TextureKey {
        uint32_t width;
        uint32_t height;
        uint16_t depthOrArraySize;
        uint16_t mipLevels;
        RHIFormat format;
        RHITextureUsage usage;

        bool operator==(const TextureKey& other) const;
    };

    /// Hash key for buffer descriptor matching.
    struct BufferKey {
        uint64_t sizeBytes;
        uint32_t stride;
        RHIBufferUsage usage;

        bool operator==(const BufferKey& other) const;
    };

    struct TextureKeyHash {
        size_t operator()(const TextureKey& k) const;
    };

    struct BufferKeyHash {
        size_t operator()(const BufferKey& k) const;
    };

    /// Converts an `RgTextureDesc` to a cache key.
    [[nodiscard]] static TextureKey MakeKey(const RgTextureDesc& desc);

    /// Converts an `RgBufferDesc` to a cache key.
    [[nodiscard]] static BufferKey MakeKey(const RgBufferDesc& desc);

    IRHIDevice* m_device = nullptr;

    /// Pool of available textures, keyed by descriptor hash.
    std::unordered_map<TextureKey, std::vector<RHITextureHandle>, TextureKeyHash> m_texturePool;

    /// Pool of available buffers, keyed by descriptor hash.
    std::unordered_map<BufferKey, std::vector<RHIBufferHandle>, BufferKeyHash> m_bufferPool;
};

} // namespace uz::rg
