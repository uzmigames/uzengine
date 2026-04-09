#pragma once

/// ShaderTableBuilder — Constructs shader binding tables (SBT) for DispatchRays.
///
/// Builds the three shader tables (ray generation, miss, hit group) required
/// by `DispatchRays`. Each table entry starts with a 32-byte shader identifier
/// (from `GetShaderIdentifier`) optionally followed by local root arguments.
///
/// @ref Microsoft DXR spec — D3D12_DISPATCH_RAYS_DESC, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT.
///
/// # Thread Safety
/// NOT thread-safe. Build one table set at a time.

#include "rhi_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

class IRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// ShaderTableBuilder
// ─────────────────────────────────────────────────────────────────────────────

/// Builds shader binding tables for ray tracing dispatch.
///
/// # Usage
/// ```cpp
/// ShaderTableBuilder builder;
/// builder.AddRayGenShader(L"RayGen");
/// builder.AddMissShader(L"Miss");
/// builder.AddHitGroup(L"HitGroup");
///
/// auto tables = builder.Build(device, rtPipeline);
/// // tables.rayGen/miss/hitGroup contain GPU addresses for DispatchRays
/// ```
class ShaderTableBuilder {
public:
    /// Adds a ray generation shader export name to the raygen table.
    void AddRayGenShader(const wchar_t* exportName);

    /// Adds a miss shader export name to the miss table.
    void AddMissShader(const wchar_t* exportName);

    /// Adds a hit group export name to the hit group table.
    void AddHitGroup(const wchar_t* exportName);

    /// Result of building shader tables — GPU buffer handles and address ranges.
    struct ShaderTableBuffers {
        RHIBufferHandle rayGenBuffer;
        RHIBufferHandle missBuffer;
        RHIBufferHandle hitGroupBuffer;
        RHIShaderTableRange rayGenRange;
        RHIShaderTableRange missRange;
        RHIShaderTableRange hitGroupRange;
    };

    /// Builds the shader tables from the added exports and an RT pipeline.
    ///
    /// Creates upload-heap buffers, copies shader identifiers from the pipeline,
    /// and returns the GPU address ranges for DispatchRays.
    ///
    /// # Arguments
    /// * `device` — RHI device for buffer creation and shader identifier retrieval.
    /// * `pipeline` — RT pipeline to query shader identifiers from.
    ///
    /// # Returns
    /// Populated `ShaderTableBuffers` on success. On failure, all handles are Invalid.
    [[nodiscard]] ShaderTableBuffers Build(IRHIDevice* device,
                                           RHIRTPipelineHandle pipeline);

    /// Destroys shader table GPU buffers.
    static void DestroyTables(IRHIDevice* device, ShaderTableBuffers& tables);

    /// Resets builder state for reuse.
    void Reset();

private:
    std::vector<std::wstring> m_rayGenExports;
    std::vector<std::wstring> m_missExports;
    std::vector<std::wstring> m_hitGroupExports;

    /// Builds a single shader table buffer from a list of export names.
    static RHIBufferHandle BuildTable(IRHIDevice* device, RHIRTPipelineHandle pipeline,
                                      const std::vector<std::wstring>& exports,
                                      const char* debugName);
};

} // namespace uz
