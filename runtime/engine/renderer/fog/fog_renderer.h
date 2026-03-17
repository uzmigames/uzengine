/// UzEngine Fog Renderer — Height Fog ECS Extraction + Pass Setup
///
/// Reads HeightFogComponent from the ECS, builds HeightFogCB with UE5-style
/// pre-computed RayOriginTerms, and registers the height fog render graph pass.

#pragma once

#include "constant_buffers.h"
#include "cb_ring_allocator.h"
#include "render_graph_types.h"
#include "rhi_types.h"

namespace uz {

class World;
class Camera;
class PipelineLibrary;

namespace rg { class RgBuilder; }

/// Adds the height fog render graph pass if a HeightFogComponent exists in the world.
///
/// Iterates HeightFogComponent (first found), pre-computes UE5 RayOriginTerms
/// on CPU, builds HeightFogCB, and registers the fog pass in the render graph.
///
/// # Arguments
/// * `rgBuilder`      — Render graph builder.
/// * `hdrRes`         — HDR scene color render graph resource (read/write).
/// * `depthRes`       — Depth buffer render graph resource (read).
/// * `perFrameAlloc`  — PerFrameCB allocation (for camera data).
/// * `world`          — ECS World to query for HeightFogComponent.
/// * `camera`         — Current frame camera (for height-based fog computation).
/// * `pipelines`      — Pipeline library for fog PSO.
/// * `cbRing`         — CB ring allocator for uploading HeightFogCB.
/// * `depthSrvIndex`  — SRV index of the depth texture.
/// * `depthTexture`   — Physical depth texture handle.
/// * `bindless`       — Whether SM6.6 bindless path is active.
/// * `vpWidth`        — Viewport width.
/// * `vpHeight`       — Viewport height.
/// @param skyLightColorR/G/B — Sky light average color from SH L0 (for sky→fog injection).
void AddHeightFogFromECS(
    rg::RgBuilder& rgBuilder,
    rg::RgResourceId hdrRes,
    rg::RgResourceId depthRes,
    const CBAllocation& perFrameAlloc,
    World* world,
    const Camera& camera,
    PipelineLibrary* pipelines,
    CBRingAllocator& cbRing,
    uint32_t depthSrvIndex,
    RHITextureHandle depthTexture,
    bool bindless,
    float vpWidth,
    float vpHeight,
    float skyLightColorR = 0.f,
    float skyLightColorG = 0.f,
    float skyLightColorB = 0.f);

} // namespace uz
