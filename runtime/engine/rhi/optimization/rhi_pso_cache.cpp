/// UzEngine PSO Cache — Implementation
///
/// Open-addressing hash map for deduplicating pipeline state objects by content hash.

#include "rhi_pso_cache.h"

#include "fnv_hash.h"
#include "log.h"
#include "rhi_types.h"

UZ_DECLARE_LOG_CATEGORY(RHI);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Key Computation — anonymous namespace for internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Mixes a range of bytes into an FNV-1a hash.
uint64_t FnvMixBytes(uint64_t hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash = FnvMix(hash, static_cast<uint64_t>(bytes[i]));  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
    return hash;
}

/// Mixes a value of arbitrary size into the hash.
template <typename T>
uint64_t FnvMixValue(uint64_t hash, const T& value) {
    return FnvMixBytes(hash, &value, sizeof(T));
}

}  // anonymous namespace

PsoCacheKey ComputePsoCacheKey(const RHIGraphicsPipelineDesc& desc) {
    uint64_t h = kFnv64OffsetBasis;

    // Shader handles (index + generation)
    h = FnvMixValue(h, desc.vertexShader.index);
    h = FnvMixValue(h, desc.vertexShader.generation);
    h = FnvMixValue(h, desc.pixelShader.index);
    h = FnvMixValue(h, desc.pixelShader.generation);

    // Topology
    h = FnvMixValue(h, desc.topology);

    // Rasterizer state
    h = FnvMixValue(h, desc.rasterizer.cullMode);
    h = FnvMixValue(h, desc.rasterizer.fillMode);
    h = FnvMixValue(h, desc.rasterizer.frontCounterClockwise);
    h = FnvMixValue(h, desc.rasterizer.depthBias);
    h = FnvMixValue(h, desc.rasterizer.slopeScaledDepthBias);

    // Depth-stencil state
    h = FnvMixValue(h, desc.depthStencil.depthEnable);
    h = FnvMixValue(h, desc.depthStencil.depthWrite);
    h = FnvMixValue(h, desc.depthStencil.depthFunc);

    // Blend state per RT (up to 8 RTs)
    for (uint32_t i = 0; i < 8; ++i) {
        const auto& blend = desc.renderTargetBlend[i];
        h = FnvMixValue(h, blend.blendEnable);
        h = FnvMixValue(h, blend.srcColor);
        h = FnvMixValue(h, blend.dstColor);
        h = FnvMixValue(h, blend.colorOp);
        h = FnvMixValue(h, blend.srcAlpha);
        h = FnvMixValue(h, blend.dstAlpha);
        h = FnvMixValue(h, blend.alphaOp);
    }

    // Render target formats (up to 8 RTs)
    for (uint32_t i = 0; i < 8; ++i) {
        h = FnvMixValue(h, desc.renderTargetFormats[i]);
    }

    // RT count and depth format
    h = FnvMixValue(h, desc.renderTargetCount);
    h = FnvMixValue(h, desc.depthStencilFormat);

    // Input layout (SM5 path)
    h = FnvMixValue(h, desc.inputElementCount);
    h = FnvMixValue(h, desc.vertexStride);

    if (desc.inputElementCount > 0 && desc.inputElements != nullptr) {
        for (uint32_t i = 0; i < desc.inputElementCount; ++i) {
            const auto& elem = desc.inputElements[i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            h = FnvMixValue(h, elem.semantic);
            h = FnvMixValue(h, elem.semanticIndex);
            h = FnvMixValue(h, elem.format);
            h = FnvMixValue(h, elem.inputSlot);
            h = FnvMixValue(h, elem.byteOffset);
        }
    }

    PsoCacheKey key;
    key.hash = h;
    return key;
}

// ─────────────────────────────────────────────────────────────────────────────
// PsoCache Implementation
// ─────────────────────────────────────────────────────────────────────────────

void PsoCache::Init(uint32_t capacity) {
    if (m_slots != nullptr) {
        UZ_LOG(RHI, Warning, "PsoCache::Init called while already initialized");
        return;
    }

    if (capacity == 0) {
        capacity = 256;
    }

    // Round up to next power-of-2
    uint32_t c = capacity;
    c--;
    c |= c >> 1U;
    c |= c >> 2U;
    c |= c >> 4U;
    c |= c >> 8U;
    c |= c >> 16U;
    c++;

    m_capacity = c;
    m_mask = m_capacity - 1;
    m_count = 0;
    m_slots = new Slot[m_capacity];  // NOLINT(cppcoreguidelines-owning-memory)

    UZ_LOG(RHI, Info, "PsoCache initialized with capacity {}", m_capacity);
}

void PsoCache::Shutdown() {
    if (m_slots == nullptr) {
        return;
    }

    delete[] m_slots;  // NOLINT(cppcoreguidelines-owning-memory)
    m_slots = nullptr;
    m_capacity = 0;
    m_mask = 0;
    m_count = 0;

    UZ_LOG(RHI, Info, "PsoCache shut down");
}

RHIPipelineHandle PsoCache::Lookup(const PsoCacheKey& key) const {
    if (m_slots == nullptr || key.hash == 0) {
        return RHIPipelineHandle::Invalid();
    }

    auto index = static_cast<uint32_t>(key.hash & m_mask);
    uint32_t steps = 0;

    while (steps < m_capacity) {
        const Slot& slot = m_slots[index];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

        if (!slot.occupied) {
            return RHIPipelineHandle::Invalid();
        }

        if (slot.key == key) {
            return slot.pipeline;
        }

        index = (index + 1) & m_mask;
        steps++;
    }

    return RHIPipelineHandle::Invalid();
}

void PsoCache::Insert(const PsoCacheKey& key, RHIPipelineHandle pipeline) {
    if (m_slots == nullptr || key.hash == 0) {
        return;
    }

    // Rehash at 70% load factor
    if (m_count >= (m_capacity * 7) / 10) {
        Rehash();
    }

    InsertIntoSlots(m_slots, m_mask, key, pipeline);
}

void PsoCache::InsertIntoSlots(Slot* slots, uint32_t mask, const PsoCacheKey& key,
                               RHIPipelineHandle pipeline) {
    auto index = static_cast<uint32_t>(key.hash & mask);
    uint32_t steps = 0;

    while (steps < (mask + 1)) {
        Slot& slot = slots[index];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

        if (!slot.occupied) {
            slot.key = key;
            slot.pipeline = pipeline;
            slot.occupied = true;
            m_count++;
            return;
        }

        if (slot.key == key) {
            // Overwrite existing (hot-reload)
            slot.pipeline = pipeline;
            return;
        }

        index = (index + 1) & mask;
        steps++;
    }

    UZ_LOG(RHI, Error, "PsoCache::Insert failed — slot array full");
}

void PsoCache::Rehash() {
    if (m_slots == nullptr) {
        return;
    }

    uint32_t oldCapacity = m_capacity;
    Slot* oldSlots = m_slots;

    m_capacity *= 2;
    m_mask = m_capacity - 1;
    m_count = 0;
    m_slots = new Slot[m_capacity];  // NOLINT(cppcoreguidelines-owning-memory)

    for (uint32_t i = 0; i < oldCapacity; ++i) {
        if (oldSlots[i].occupied) {  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            InsertIntoSlots(m_slots, m_mask, oldSlots[i].key, oldSlots[i].pipeline);
        }
    }

    delete[] oldSlots;  // NOLINT(cppcoreguidelines-owning-memory)

    UZ_LOG(RHI, Debug, "PsoCache rehashed: {} -> {} slots", oldCapacity, m_capacity);
}

uint32_t PsoCache::InvalidateByShader(RHIShaderHandle /*shader*/) {
    if (m_slots == nullptr) {
        return 0;
    }

    // Conservative: full clear on any shader invalidation.
    // Future: store shader handles in Slot for targeted eviction.
    if (m_count > 0) {
        uint32_t oldCount = m_count;
        Clear();
        UZ_LOG(RHI, Info, "PsoCache invalidated by shader: {} entries cleared", oldCount);
        return oldCount;
    }

    return 0;
}

void PsoCache::Clear() {
    if (m_slots == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < m_capacity; ++i) {
        m_slots[i].occupied = false;  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
    m_count = 0;
}

}  // namespace uz
