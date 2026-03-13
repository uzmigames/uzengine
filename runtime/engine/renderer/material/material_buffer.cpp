/// MaterialBuffer implementation.
///
/// Upload-heap StructuredBuffer with CPU-side dirty tracking.
/// Maps the buffer once at Init and keeps the pointer for the lifetime.

#include "material_buffer.h"
#include "rhi.h"
#include "log.h"
#include "profiler.h"

#include <cstring>
#include <utility>

namespace uz {

MaterialBuffer::~MaterialBuffer() {
    Shutdown();
}

MaterialBuffer::MaterialBuffer(MaterialBuffer&& other) noexcept
    : m_device(other.m_device)
    , m_buffer(other.m_buffer)
    , m_mappedPtr(other.m_mappedPtr)
    , m_srvIndex(other.m_srvIndex)
    , m_capacity(other.m_capacity)
    , m_count(other.m_count)
    , m_materials(std::move(other.m_materials))
    , m_dirty(other.m_dirty) {
    other.m_device    = nullptr;
    other.m_buffer    = {};
    other.m_mappedPtr = nullptr;
    other.m_srvIndex  = UINT32_MAX;
    other.m_capacity  = 0;
    other.m_count     = 0;
    other.m_dirty     = false;
}

MaterialBuffer& MaterialBuffer::operator=(MaterialBuffer&& other) noexcept {
    if (this != &other) {
        Shutdown();

        m_device    = other.m_device;
        m_buffer    = other.m_buffer;
        m_mappedPtr = other.m_mappedPtr;
        m_srvIndex  = other.m_srvIndex;
        m_capacity  = other.m_capacity;
        m_count     = other.m_count;
        m_materials = std::move(other.m_materials);
        m_dirty     = other.m_dirty;

        other.m_device    = nullptr;
        other.m_buffer    = {};
        other.m_mappedPtr = nullptr;
        other.m_srvIndex  = UINT32_MAX;
        other.m_capacity  = 0;
        other.m_count     = 0;
        other.m_dirty     = false;
    }
    return *this;
}

bool MaterialBuffer::Init(IRHIDevice* device, uint32_t maxMaterials) {
    UZ_PROFILE_FUNCTION();

    if (!device || maxMaterials == 0) {
        UZ_LOG(Renderer, Error, "MaterialBuffer::Init: invalid arguments (device={}, maxMaterials={})",
               static_cast<const void*>(device), maxMaterials);
        return false;
    }

    m_device   = device;
    m_capacity = maxMaterials;

    RHIBufferDesc desc{};
    desc.sizeBytes = static_cast<uint64_t>(sizeof(PBRMaterial)) * maxMaterials;
    desc.stride    = sizeof(PBRMaterial);
    desc.usage     = RHIBufferUsage::StructuredBuffer;
    desc.memory    = RHIMemoryType::Upload;
    desc.debugName = "MaterialBuffer";

    m_buffer = device->CreateBuffer(desc);
    if (!m_buffer.IsValid()) {
        UZ_LOG(Renderer, Error, "MaterialBuffer: failed to create StructuredBuffer ({} bytes)",
               desc.sizeBytes);
        m_device = nullptr;
        return false;
    }

    m_mappedPtr = static_cast<uint8_t*>(device->MapBuffer(m_buffer));
    if (!m_mappedPtr) {
        UZ_LOG(Renderer, Error, "MaterialBuffer: failed to map upload buffer");
        device->DestroyBuffer(m_buffer);
        m_buffer   = {};
        m_device   = nullptr;
        m_capacity = 0;
        return false;
    }

    m_srvIndex = device->GetBufferSRVIndex(m_buffer);

    // Reserve index 0 as the default material (PBRMaterial default ctor values).
    m_materials.reserve(maxMaterials);
    m_materials.emplace_back(); // Default: white, metallic=0, roughness=0.5, ao=1, no emissive
    m_count = 1;
    m_dirty = true;

    UZ_LOG(Renderer, Info, "MaterialBuffer: capacity={} materials ({} bytes), srvIndex={}",
           maxMaterials, desc.sizeBytes, m_srvIndex);
    return true;
}

void MaterialBuffer::Shutdown() {
    if (m_device && m_buffer.IsValid()) {
        m_device->UnmapBuffer(m_buffer);
        m_device->DestroyBuffer(m_buffer);
    }
    m_buffer    = {};
    m_mappedPtr = nullptr;
    m_srvIndex  = UINT32_MAX;
    m_capacity  = 0;
    m_count     = 0;
    m_dirty     = false;
    m_materials.clear();
    m_device    = nullptr;
}

uint32_t MaterialBuffer::AddMaterial(const PBRMaterial& material) {
    if (m_count >= m_capacity) {
        UZ_LOG(Renderer, Error, "MaterialBuffer: at capacity ({}/{}), cannot add material",
               m_count, m_capacity);
        return UINT32_MAX;
    }

    const uint32_t index = m_count;
    m_materials.push_back(material);
    ++m_count;
    m_dirty = true;
    return index;
}

void MaterialBuffer::UpdateMaterial(uint32_t index, const PBRMaterial& material) {
    if (index >= m_count) {
        UZ_LOG(Renderer, Error, "MaterialBuffer::UpdateMaterial: index {} out of range (count={})",
               index, m_count);
        return;
    }

    m_materials[index] = material;
    m_dirty = true;
}

const PBRMaterial& MaterialBuffer::GetMaterial(uint32_t index) const {
    return m_materials[index];
}

void MaterialBuffer::Upload() {
    if (!m_dirty) {
        return;
    }

    UZ_PROFILE_FUNCTION();

    const uint64_t uploadSize = static_cast<uint64_t>(sizeof(PBRMaterial)) * m_count;
    std::memcpy(m_mappedPtr, m_materials.data(), uploadSize);

    // UnmapBuffer is a no-op on DX12/Vulkan (persistently mapped), but
    // required on DX11 to flush writes. We re-map immediately after.
    m_device->UnmapBuffer(m_buffer);
    m_mappedPtr = static_cast<uint8_t*>(m_device->MapBuffer(m_buffer));

    m_dirty = false;
}

} // namespace uz
