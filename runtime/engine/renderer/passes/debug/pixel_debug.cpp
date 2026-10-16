/// UzEngine PixelDebug — GPU printf implementation.
///
/// @ref NVIDIA Falcor PixelDebug — InterlockedAdd + structured buffer readback.

#include "pixel_debug.h"
#include "engine/renderer/render_graph/render_graph_states.h"
#include "rhi_device.h"
#include "rhi_command_list.h"
#include "log.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

UZ_DEFINE_LOG_CATEGORY(PixelDebug, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

bool PixelDebugSystem::Init(IRHIDevice* device, const PixelDebugConfig& config) {
    m_config = config;

    if (!config.enabled) {
        return true; // Not an error — just disabled.
    }

    // Counter buffer: 4 bytes, raw UAV (shader writes via InterlockedAdd on RWByteAddressBuffer).
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = 4;
        desc.stride    = 0;  // stride=0 → raw byte address buffer (D3D12_BUFFER_UAV_FLAG_RAW)
        desc.usage     = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::CopySrc | RHIBufferUsage::CopyDst;
        desc.memory    = RHIMemoryType::Default;
        desc.debugName = "PixelDebug_Counter";
        m_counterBuffer = device->CreateBuffer(desc);
        if (!m_counterBuffer.IsValid()) {
            UZ_LOG(PixelDebug, Error, "Failed to create counter buffer");
            return false;
        }
    }

    // Records buffer: maxRecords x 16 bytes, raw UAV (shader writes via Store on RWByteAddressBuffer).
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(config.maxRecords) * 16;
        desc.stride    = 0;  // stride=0 → raw byte address buffer (D3D12_BUFFER_UAV_FLAG_RAW)
        desc.usage     = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::CopySrc;
        desc.memory    = RHIMemoryType::Default;
        desc.debugName = "PixelDebug_Records";
        m_recordsBuffer = device->CreateBuffer(desc);
        if (!m_recordsBuffer.IsValid()) {
            UZ_LOG(PixelDebug, Error, "Failed to create records buffer");
            return false;
        }
    }

    // Readback buffer: same size as records, for CPU read.
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(config.maxRecords) * 16;
        desc.stride    = 16;
        desc.usage     = RHIBufferUsage::CopyDst;
        desc.memory    = RHIMemoryType::Readback;
        desc.debugName = "PixelDebug_Readback";
        m_readbackBuffer = device->CreateBuffer(desc);
        if (!m_readbackBuffer.IsValid()) {
            UZ_LOG(PixelDebug, Error, "Failed to create readback buffer");
            return false;
        }
    }

    // Upload buffer: 4 bytes of zeros for clearing the counter.
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = 4;
        desc.stride    = 4;
        desc.usage     = RHIBufferUsage::CopySrc;
        desc.memory    = RHIMemoryType::Upload;
        desc.debugName = "PixelDebug_Upload";
        m_uploadBuffer = device->CreateBuffer(desc);
        if (!m_uploadBuffer.IsValid()) {
            UZ_LOG(PixelDebug, Error, "Failed to create upload buffer");
            return false;
        }
        // Zero-fill the upload buffer.
        void* mapped = device->MapBuffer(m_uploadBuffer);
        if (mapped) {
            std::memset(mapped, 0, 4);
            device->UnmapBuffer(m_uploadBuffer);
        }
    }

    // Cache UAV descriptor heap indices for bindless access from shaders.
    // The lighting pass pushes these as root constants so the shader can
    // use ResourceDescriptorHeap[idx] to access the UAV buffers.
    m_counterUavIndex = device->GetBufferUAVIndex(m_counterBuffer);
    m_recordsUavIndex = device->GetBufferUAVIndex(m_recordsBuffer);

    m_initialized = true;
    UZ_LOG(PixelDebug, Info, "PixelDebug initialized: pixel=({},{}), maxRecords={}, "
           "counterUAV={}, recordsUAV={}",
           config.pixelX, config.pixelY, config.maxRecords,
           m_counterUavIndex, m_recordsUavIndex);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginFrame — clear counter to 0
// ─────────────────────────────────────────────────────────────────────────────

void PixelDebugSystem::BeginFrame(IRHICommandList* cmdList) {
    if (!IsEnabled()) return;

    m_states->Transition(cmdList, m_counterBuffer, RHIResourceState::CopyDst);

    // Copy 4 zero bytes from upload buffer to counter.
    cmdList->CopyBuffer(m_uploadBuffer, 0, m_counterBuffer, 0, 4);

    m_states->Transition(cmdList, m_counterBuffer, RHIResourceState::UnorderedAccess);
    m_states->Transition(cmdList, m_recordsBuffer, RHIResourceState::UnorderedAccess);
}

// ─────────────────────────────────────────────────────────────────────────────
// EndFrame — copy records to readback
// ─────────────────────────────────────────────────────────────────────────────

void PixelDebugSystem::EndFrame(IRHICommandList* cmdList) {
    if (!IsEnabled()) return;

    const uint64_t recordsSize = static_cast<uint64_t>(m_config.maxRecords) * 16;

    m_states->Transition(cmdList, m_recordsBuffer, RHIResourceState::CopySrc);

    // Copy records to readback.
    cmdList->CopyBuffer(m_recordsBuffer, 0, m_readbackBuffer, 0, recordsSize);

    m_states->Transition(cmdList, m_recordsBuffer, RHIResourceState::UnorderedAccess);
}

// ─────────────────────────────────────────────────────────────────────────────
// ReadResults — map readback, decode records
// ─────────────────────────────────────────────────────────────────────────────

std::vector<PixelDebugRecord> PixelDebugSystem::ReadResults(IRHIDevice* device) {
    std::vector<PixelDebugRecord> results;
    if (!IsEnabled()) return results;

    void* mapped = device->MapBuffer(m_readbackBuffer);
    if (!mapped) {
        UZ_LOG(PixelDebug, Warning, "Failed to map readback buffer");
        return results;
    }

    const auto* data = static_cast<const uint32_t*>(mapped);
    // Read all maxRecords slots and filter non-zero entries.
    // The counter is in a separate GPU buffer; we scan and skip empty slots.
    results.reserve(m_config.maxRecords);

    for (uint32_t i = 0; i < m_config.maxRecords; ++i) {
        const uint32_t offset = i * 4; // 4 uint32s per record
        const uint32_t rawId    = data[offset + 0];
        const uint32_t rawValue = data[offset + 1];
        const uint32_t px       = data[offset + 2];
        const uint32_t py       = data[offset + 3];

        // Skip unwritten slots (all zeros with id=0 and pixel=0).
        if (rawId == 0 && rawValue == 0 && px == 0 && py == 0) continue;

        PixelDebugRecord rec;
        rec.isUint    = (rawId & 0x80000000u) != 0;
        rec.messageId = rawId & 0x7FFFFFFFu;
        rec.uintValue = rawValue;
        float fval;
        std::memcpy(&fval, &rawValue, sizeof(float));
        rec.floatValue = fval;
        rec.pixelX = px;
        rec.pixelY = py;
        results.push_back(rec);
    }

    device->UnmapBuffer(m_readbackBuffer);
    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// ToJSON
// ─────────────────────────────────────────────────────────────────────────────

std::string PixelDebugSystem::ToJSON(const std::vector<PixelDebugRecord>& records,
                                      uint32_t pixelX, uint32_t pixelY,
                                      uint32_t frameNumber) {
    std::ostringstream os;
    os << "{";
    os << "\"pixel\":[" << pixelX << "," << pixelY << "],";
    os << "\"frame\":" << frameNumber << ",";
    os << "\"recordCount\":" << records.size() << ",";
    os << "\"records\":[";

    for (size_t i = 0; i < records.size(); ++i) {
        const auto& r = records[i];
        if (i > 0) os << ",";
        os << "{\"id\":" << r.messageId << ",";
        if (r.isUint) {
            os << "\"type\":\"uint\",\"value\":" << r.uintValue;
        } else {
            os << "\"type\":\"float\",\"value\":";
            if (std::isnan(r.floatValue))      os << "\"NaN\"";
            else if (std::isinf(r.floatValue)) os << "\"Inf\"";
            else {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.6f", r.floatValue);
                os << buf;
            }
        }
        os << "}";
    }

    os << "]}";
    return os.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void PixelDebugSystem::Shutdown(IRHIDevice* device) {
    if (!m_initialized) return;

    if (m_counterBuffer.IsValid())  device->DestroyBuffer(m_counterBuffer);
    if (m_recordsBuffer.IsValid())  device->DestroyBuffer(m_recordsBuffer);
    if (m_readbackBuffer.IsValid()) device->DestroyBuffer(m_readbackBuffer);
    if (m_uploadBuffer.IsValid())   device->DestroyBuffer(m_uploadBuffer);

    m_counterBuffer  = {};
    m_recordsBuffer  = {};
    m_readbackBuffer = {};
    m_uploadBuffer   = {};
    m_initialized = false;

    UZ_LOG(PixelDebug, Info, "PixelDebug shutdown");
}

} // namespace uz
