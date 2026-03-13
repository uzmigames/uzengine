/// Render Graph Resource Pool — Texture/Buffer Cache Implementation
///
/// Caches transient GPU resources (textures, buffers) by descriptor and returns
/// them to the pool after render graph execution. Uses FNV hash for descriptor
/// matching and deferred allocation to avoid redundant GPU allocations.

#include "render_graph_resources.h"

#include "../../containers/fnv_hash.h"
#include "log.h"
#include "engine_assert.h"

namespace uz::rg {

// ─────────────────────────────────────────────────────────────────────────────
// RgResourcePool — Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

RgResourcePool::RgResourcePool(IRHIDevice* device)
    : m_device(device)
{
    UZ_CHECK(device != nullptr);
}

RgResourcePool::~RgResourcePool()
{
    Trim();
}

// ─────────────────────────────────────────────────────────────────────────────
// Key helpers
// ─────────────────────────────────────────────────────────────────────────────

bool RgResourcePool::TextureKey::operator==(const TextureKey& other) const
{
    return width == other.width
        && height == other.height
        && depthOrArraySize == other.depthOrArraySize
        && mipLevels == other.mipLevels
        && format == other.format
        && usage == other.usage;
}

bool RgResourcePool::BufferKey::operator==(const BufferKey& other) const
{
    return sizeBytes == other.sizeBytes
        && stride == other.stride
        && usage == other.usage;
}

size_t RgResourcePool::TextureKeyHash::operator()(const TextureKey& k) const
{
    uint64_t h = kFnv64OffsetBasis;
    h = FnvMix(h, k.width);
    h = FnvMix(h, k.height);
    h = FnvMix(h, k.depthOrArraySize);
    h = FnvMix(h, k.mipLevels);
    h = FnvMix(h, static_cast<uint64_t>(k.format));
    h = FnvMix(h, static_cast<uint64_t>(k.usage));
    return static_cast<size_t>(h);
}

size_t RgResourcePool::BufferKeyHash::operator()(const BufferKey& k) const
{
    uint64_t h = kFnv64OffsetBasis;
    h = FnvMix(h, k.sizeBytes);
    h = FnvMix(h, k.stride);
    h = FnvMix(h, static_cast<uint64_t>(k.usage));
    return static_cast<size_t>(h);
}

RgResourcePool::TextureKey RgResourcePool::MakeKey(const RgTextureDesc& desc)
{
    return { desc.width, desc.height, desc.depthOrArraySize, desc.mipLevels, desc.format, desc.usage };
}

RgResourcePool::BufferKey RgResourcePool::MakeKey(const RgBufferDesc& desc)
{
    return { desc.sizeBytes, desc.stride, desc.usage };
}

// ─────────────────────────────────────────────────────────────────────────────
// Acquire / Release
// ─────────────────────────────────────────────────────────────────────────────

RHITextureHandle RgResourcePool::AcquireTexture(const RgTextureDesc& desc, const char* debugName)
{
    auto key = MakeKey(desc);
    auto it = m_texturePool.find(key);

    if (it != m_texturePool.end() && !it->second.empty()) {
        auto handle = it->second.back();
        it->second.pop_back();
        UZ_LOG(Renderer, Debug, "RgResourcePool: reused texture '{}' from pool", debugName ? debugName : "?");
        return handle;
    }

    // No cached texture available — create a new one.
    RHITextureDesc rhiDesc{};
    rhiDesc.width = desc.width;
    rhiDesc.height = desc.height;
    rhiDesc.depthOrArraySize = desc.depthOrArraySize;
    rhiDesc.mipLevels = desc.mipLevels;
    rhiDesc.format = desc.format;
    rhiDesc.usage = desc.usage;
    rhiDesc.debugName = debugName;

    auto handle = m_device->CreateTexture(rhiDesc);
    UZ_CHECK(handle.IsValid());
    UZ_LOG(Renderer, Debug, "RgResourcePool: created new texture '{}'", debugName ? debugName : "?");
    return handle;
}

RHIBufferHandle RgResourcePool::AcquireBuffer(const RgBufferDesc& desc, const char* debugName)
{
    auto key = MakeKey(desc);
    auto it = m_bufferPool.find(key);

    if (it != m_bufferPool.end() && !it->second.empty()) {
        auto handle = it->second.back();
        it->second.pop_back();
        UZ_LOG(Renderer, Debug, "RgResourcePool: reused buffer '{}' from pool", debugName ? debugName : "?");
        return handle;
    }

    RHIBufferDesc rhiDesc{};
    rhiDesc.sizeBytes = desc.sizeBytes;
    rhiDesc.stride = desc.stride;
    rhiDesc.usage = desc.usage;
    rhiDesc.memory = RHIMemoryType::Default;
    rhiDesc.debugName = debugName;

    auto handle = m_device->CreateBuffer(rhiDesc);
    UZ_CHECK(handle.IsValid());
    UZ_LOG(Renderer, Debug, "RgResourcePool: created new buffer '{}'", debugName ? debugName : "?");
    return handle;
}

void RgResourcePool::ReleaseTexture(RHITextureHandle handle, const RgTextureDesc& desc)
{
    if (!handle.IsValid()) return;
    m_texturePool[MakeKey(desc)].push_back(handle);
}

void RgResourcePool::ReleaseBuffer(RHIBufferHandle handle, const RgBufferDesc& desc)
{
    if (!handle.IsValid()) return;
    m_bufferPool[MakeKey(desc)].push_back(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Trim / Queries
// ─────────────────────────────────────────────────────────────────────────────

void RgResourcePool::Trim()
{
    uint32_t textureCount = 0;
    uint32_t bufferCount = 0;

    for (auto& [key, handles] : m_texturePool) {
        for (auto h : handles) {
            m_device->DestroyTexture(h);
            ++textureCount;
        }
    }
    m_texturePool.clear();

    for (auto& [key, handles] : m_bufferPool) {
        for (auto h : handles) {
            m_device->DestroyBuffer(h);
            ++bufferCount;
        }
    }
    m_bufferPool.clear();

    if (textureCount > 0 || bufferCount > 0) {
        UZ_LOG(Renderer, Info, "RgResourcePool: trimmed {} textures, {} buffers", textureCount, bufferCount);
    }
}

uint32_t RgResourcePool::GetCachedTextureCount() const
{
    uint32_t count = 0;
    for (const auto& [key, handles] : m_texturePool) {
        count += static_cast<uint32_t>(handles.size());
    }
    return count;
}

uint32_t RgResourcePool::GetCachedBufferCount() const
{
    uint32_t count = 0;
    for (const auto& [key, handles] : m_bufferPool) {
        count += static_cast<uint32_t>(handles.size());
    }
    return count;
}

} // namespace uz::rg
