/// ShaderTableBuilder Implementation — Constructs SBT buffers for DispatchRays.

#include "shader_table_builder.h"
#include "rhi_device.h"
#include "log.h"

#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Alignment helpers
// ─────────────────────────────────────────────────────────────────────────────

/// DXR shader table entry alignment: 32 bytes (D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT).
static constexpr uint64_t kShaderTableAlignment = 64;

/// Shader record size: shader identifier (32 bytes) + local root args (0 for now).
/// Rounded up to D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT (32 bytes).
static constexpr uint64_t kShaderRecordSize = 64; // 32-byte identifier, padded to 64 for alignment

/// Align a value up to the given power-of-two alignment.
static uint64_t AlignUp(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void ShaderTableBuilder::AddRayGenShader(const wchar_t* exportName) {
    m_rayGenExports.emplace_back(exportName);
}

void ShaderTableBuilder::AddMissShader(const wchar_t* exportName) {
    m_missExports.emplace_back(exportName);
}

void ShaderTableBuilder::AddHitGroup(const wchar_t* exportName) {
    m_hitGroupExports.emplace_back(exportName);
}

ShaderTableBuilder::ShaderTableBuffers ShaderTableBuilder::Build(
    IRHIDevice* device, RHIRTPipelineHandle pipeline) {
    ShaderTableBuffers result = {};

    if (!device || !pipeline.IsValid()) return result;

    // Build each table
    result.rayGenBuffer   = BuildTable(device, pipeline, m_rayGenExports, "SBT_RayGen");
    result.missBuffer     = BuildTable(device, pipeline, m_missExports, "SBT_Miss");
    result.hitGroupBuffer = BuildTable(device, pipeline, m_hitGroupExports, "SBT_HitGroup");

    // Fill ranges with GPU addresses
    if (result.rayGenBuffer.IsValid()) {
        result.rayGenRange.startAddress  = device->GetBufferGPUAddress(result.rayGenBuffer);
        result.rayGenRange.sizeInBytes   = AlignUp(kShaderRecordSize, kShaderTableAlignment);
        result.rayGenRange.strideInBytes = 0; // raygen has exactly 1 record, stride is 0
    }

    if (result.missBuffer.IsValid()) {
        result.missRange.startAddress  = device->GetBufferGPUAddress(result.missBuffer);
        result.missRange.sizeInBytes   = AlignUp(kShaderRecordSize * m_missExports.size(), kShaderTableAlignment);
        result.missRange.strideInBytes = kShaderRecordSize;
    }

    if (result.hitGroupBuffer.IsValid()) {
        result.hitGroupRange.startAddress  = device->GetBufferGPUAddress(result.hitGroupBuffer);
        result.hitGroupRange.sizeInBytes   = AlignUp(kShaderRecordSize * m_hitGroupExports.size(), kShaderTableAlignment);
        result.hitGroupRange.strideInBytes = kShaderRecordSize;
    }

    return result;
}

void ShaderTableBuilder::DestroyTables(IRHIDevice* device, ShaderTableBuffers& tables) {
    if (!device) return;
    if (tables.rayGenBuffer.IsValid())   device->DestroyBuffer(tables.rayGenBuffer);
    if (tables.missBuffer.IsValid())     device->DestroyBuffer(tables.missBuffer);
    if (tables.hitGroupBuffer.IsValid()) device->DestroyBuffer(tables.hitGroupBuffer);
    tables = {};
}

void ShaderTableBuilder::Reset() {
    m_rayGenExports.clear();
    m_missExports.clear();
    m_hitGroupExports.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Private
// ─────────────────────────────────────────────────────────────────────────────

RHIBufferHandle ShaderTableBuilder::BuildTable(
    IRHIDevice* device, RHIRTPipelineHandle pipeline,
    const std::vector<std::wstring>& exports, const char* debugName) {
    if (exports.empty()) return RHIBufferHandle::Invalid();

    uint64_t tableSize = AlignUp(kShaderRecordSize * exports.size(), kShaderTableAlignment);

    RHIBufferDesc bufDesc = {};
    bufDesc.sizeBytes = tableSize;
    bufDesc.usage     = RHIBufferUsage::ShaderBindingTable;
    bufDesc.memory    = RHIMemoryType::Upload;
    bufDesc.debugName = debugName;

    RHIBufferHandle buffer = device->CreateBuffer(bufDesc);
    if (!buffer.IsValid()) {
        UZ_LOG(Renderer, Error, "ShaderTableBuilder: Failed to create {} buffer", debugName);
        return RHIBufferHandle::Invalid();
    }

    auto* mapped = static_cast<uint8_t*>(device->MapBuffer(buffer));
    if (!mapped) {
        device->DestroyBuffer(buffer);
        return RHIBufferHandle::Invalid();
    }

    std::memset(mapped, 0, tableSize);

    for (size_t i = 0; i < exports.size(); ++i) {
        uint8_t identifier[kShaderIdentifierSizeInBytes] = {};
        if (!device->GetShaderIdentifier(pipeline, exports[i].c_str(), identifier)) {
            UZ_LOG(Renderer, Error, "ShaderTableBuilder: Failed to get identifier for export in {}",
                   debugName);
            device->UnmapBuffer(buffer);
            device->DestroyBuffer(buffer);
            return RHIBufferHandle::Invalid();
        }
        std::memcpy(mapped + i * kShaderRecordSize, identifier, kShaderIdentifierSizeInBytes);
    }

    device->UnmapBuffer(buffer);
    return buffer;
}

} // namespace uz
