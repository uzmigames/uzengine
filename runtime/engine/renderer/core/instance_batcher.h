/// UzEngine Instance Batcher
///
/// Groups sorted DrawList commands into instanced batches. Consecutive
/// commands with the same mesh ID and material textures are collapsed
/// into a single batch that can be rendered with DrawIndexedInstanced.

#pragma once

#include "draw_list.h"
#include "mat4.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// InstanceData — per-instance GPU payload for StructuredBuffer
// ─────────────────────────────────────────────────────────────────────────────

/// Per-instance transform data uploaded to a GPU StructuredBuffer.
///
/// Each instance carries its own world and normal matrices. Material
/// texture indices are shared per batch and remain in PerObjectCB.
/// The vertex shader reads transforms via
/// `StructuredBuffer<InstanceData>[SV_InstanceID]`.
///
/// # GPU Layout
/// Must match the HLSL struct exactly (32 floats = 128 bytes per instance):
/// ```hlsl
/// struct InstanceData {
///     row_major float4x4 world;         // 64 bytes
///     row_major float4x4 normalMatrix;  // 64 bytes
/// };
/// ```
struct InstanceData {
    Mat4 world;            ///< Object-to-world transform.
    Mat4 normalMatrix;     ///< transpose(inverse(model)) for lighting.
};

static_assert(sizeof(InstanceData) == 128, "InstanceData must be 128 bytes (two Mat4)");

/// Maximum number of instances per frame across all batches.
///
/// If the total draw command count exceeds this limit, remaining commands
/// are silently dropped and a warning is logged.
static constexpr uint32_t kMaxInstances = 16384;

// ─────────────────────────────────────────────────────────────────────────────
// InstanceBatch — one DrawIndexedInstanced invocation
// ─────────────────────────────────────────────────────────────────────────────

/// A single instanced draw batch — all instances share the same mesh and material.
///
/// The renderer issues one `DrawIndexedInstanced(indexCount, instanceCount, 0, 0, firstInstance)`
/// per batch, where `firstInstance` indexes into the global instance buffer.
struct InstanceBatch {
    uint32_t meshId        = 0; ///< Mesh resource index (shared by all instances in this batch).
    uint32_t firstInstance  = 0; ///< Offset into the global InstanceData buffer.
    uint32_t instanceCount  = 0; ///< Number of instances in this batch.

    /// Shared material SRV indices (all instances in a batch share the same textures).
    uint32_t albedoSrvIndex    = 0;
    uint32_t normalSrvIndex    = 0;
    uint32_t roughnessSrvIndex = 0;
    uint32_t metallicSrvIndex  = 0;
    uint32_t aoSrvIndex        = 0;

    /// SM5 texture handles for binding (all instances in a batch share the same material).
    RHITextureHandle albedoTexture;
    RHITextureHandle normalTexture;
    RHITextureHandle roughnessTexture;
    RHITextureHandle metallicTexture;
    RHITextureHandle aoTexture;
};

// ─────────────────────────────────────────────────────────────────────────────
// BuildInstanceBatches — the core batching function
// ─────────────────────────────────────────────────────────────────────────────

/// Groups a sorted DrawList into instanced batches.
///
/// Scans the sorted command list and merges consecutive commands that share
/// the same mesh ID and material texture SRV indices into a single batch.
/// Commands with different meshes or materials start a new batch.
///
/// The batcher also fills `outInstances` with per-instance data
/// (transforms + material indices) that must be uploaded to a GPU
/// StructuredBuffer before rendering.
///
/// # Batching Criteria
/// Two consecutive commands are merged into the same batch if ALL of these match:
/// - `meshId`
/// - `albedoSrvIndex`, `normalSrvIndex`, `roughnessSrvIndex`, `metallicSrvIndex`, `aoSrvIndex`
///
/// # Arguments
/// * `drawList`     — Sorted draw list to scan. Must have been `Sort()`ed.
/// * `outBatches`   — Receives the generated batches. Cleared before filling.
/// * `outInstances` — Receives per-instance data for GPU upload. Cleared before filling.
///
/// # Limits
/// If the total instance count exceeds `kMaxInstances`, remaining commands
/// are silently dropped and a warning is logged.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the render thread.
void BuildInstanceBatches(const DrawList& drawList,
                          std::vector<InstanceBatch>& outBatches,
                          std::vector<InstanceData>& outInstances);

} // namespace uz
