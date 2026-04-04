/// UzEngine Pipeline Library — VFX pipelines (particle compute, billboard, decal, trail).

#include "pipeline_library.h"
#include "engine_shaders.h"
#include "log.h"
#include "rhi.h"
#include "shader_manager.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Shader Ensure
// ─────────────────────────────────────────────────────────────────────────────

/// Compiles the particle billboard vertex and pixel shaders if not already cached.
///
/// Both shaders are compiled with `UZ_BINDLESS` when the device supports SM6.6.
/// The vertex shader uses SV_VertexID-only input (no vertex buffer), so no
/// input layout is needed even on the SM5 path.
///
/// # Returns
/// `true` if both shaders are valid, `false` on compilation failure (logged).
bool PipelineLibrary::EnsureParticleBillboardShaders() {
    if (!m_particleBillboardVS.IsValid()) {
        m_particleBillboardVS = m_shaderMgr->CreateShader(
            EngineShader::ParticleBillboardVS, RHIShaderStage::Vertex);
        if (!m_particleBillboardVS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ParticleBillboardVS");
            return false;
        }
    }
    if (!m_particleBillboardPS.IsValid()) {
        m_particleBillboardPS = m_shaderMgr->CreateShader(
            EngineShader::ParticleBillboardPS, RHIShaderStage::Pixel);
        if (!m_particleBillboardPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ParticleBillboardPS");
            return false;
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Particle Billboard Pipeline (Alpha Blend)
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the standard alpha-blended particle billboard pipeline.
///
/// PSO configuration:
/// - No vertex input layout (SV_VertexID only, even on SM5)
/// - SrcAlpha / InvSrcAlpha blending for standard alpha compositing
/// - Depth read enabled (particles are depth-tested against scene)
/// - Depth write disabled (particles do not occlude each other or geometry)
/// - No backface culling (billboard quads may face either direction)
/// - Renders to HDR scene color (RGBA16Float)
///
/// Draw call: `DrawInstanced(aliveCount * 6, 1, 0, 0)` via DrawIndirect.
/// The indirect args buffer is written by `particle_indirect_args_cs.hlsl`.
RHIPipelineHandle PipelineLibrary::GetParticleBillboard() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetParticleBillboard called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_particleBillboard.IsValid()) {
        return m_particleBillboard;
    }

    if (!EnsureParticleBillboardShaders()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_particleBillboardVS;
    desc.pixelShader   = m_particleBillboardPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;

    // No vertex input layout — VS uses SV_VertexID to index structured buffers
    desc.inputElementCount = 0;

    // Two-sided: billboard quads may face either direction due to rotation
    desc.rasterizer.cullMode              = RHICullMode::None;

    // Read scene depth for occlusion, do NOT write (particles are transparent).
    desc.depthStencil.depthEnable = true;
    desc.depthStencil.depthWrite  = false;
    desc.depthStencil.depthFunc   = RHICompareFunc::GreaterEqual; // Reversed-Z: closer = greater

    // Standard alpha blend: SrcAlpha / InvSrcAlpha
    desc.renderTargetBlend[0].blendEnable = true;
    desc.renderTargetBlend[0].srcColor    = RHIBlendFactor::SrcAlpha;
    desc.renderTargetBlend[0].dstColor    = RHIBlendFactor::InvSrcAlpha;
    desc.renderTargetBlend[0].colorOp     = RHIBlendOp::Add;
    desc.renderTargetBlend[0].srcAlpha    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].dstAlpha    = RHIBlendFactor::InvSrcAlpha;
    desc.renderTargetBlend[0].alphaOp     = RHIBlendOp::Add;

    desc.renderTargetFormats[0] = RHIFormat::RGBA16Float; // HDR scene color
    desc.renderTargetCount      = 1;
    desc.depthStencilFormat     = RHIFormat::D32Float;
    desc.debugName              = "ParticleBillboard";

    m_particleBillboard = CreateCachedPipeline(desc);
    if (!m_particleBillboard.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ParticleBillboard pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ParticleBillboard pipeline");
    }

    return m_particleBillboard;
}

// ─────────────────────────────────────────────────────────────────────────────
// Particle Billboard Pipeline (Additive Blend)
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the additive-blended particle billboard pipeline.
///
/// Identical to `GetParticleBillboard()` except for the blend state:
/// - One / One additive blending (fire, sparks, energy, magic effects)
/// - Order-independent: no back-to-front sorting required
///
/// Draw call: same DrawIndirect pattern as the alpha-blended variant.
RHIPipelineHandle PipelineLibrary::GetParticleBillboardAdditive() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetParticleBillboardAdditive called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_particleBillboardAdditive.IsValid()) {
        return m_particleBillboardAdditive;
    }

    if (!EnsureParticleBillboardShaders()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_particleBillboardVS;
    desc.pixelShader   = m_particleBillboardPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;

    // No vertex input layout — SV_VertexID only
    desc.inputElementCount = 0;

    desc.rasterizer.cullMode              = RHICullMode::None; // Two-sided for billboard particles

    // Read scene depth, do NOT write
    desc.depthStencil.depthEnable = true;
    desc.depthStencil.depthWrite  = false;
    desc.depthStencil.depthFunc   = RHICompareFunc::Greater; // Reversed-Z

    // Additive blend: One / One (order-independent)
    desc.renderTargetBlend[0].blendEnable = true;
    desc.renderTargetBlend[0].srcColor    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].dstColor    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].colorOp     = RHIBlendOp::Add;
    desc.renderTargetBlend[0].srcAlpha    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].dstAlpha    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].alphaOp     = RHIBlendOp::Add;

    desc.renderTargetFormats[0] = RHIFormat::RGBA16Float; // HDR scene color
    desc.renderTargetCount      = 1;
    desc.depthStencilFormat     = RHIFormat::D32Float;
    desc.debugName              = "ParticleBillboardAdditive";

    m_particleBillboardAdditive = CreateCachedPipeline(desc);
    if (!m_particleBillboardAdditive.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ParticleBillboardAdditive pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ParticleBillboardAdditive pipeline");
    }

    return m_particleBillboardAdditive;
}

// ─────────────────────────────────────────────────────────────────────────────
// Particle Spawn Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureParticleSpawnCSShader() {
    if (!m_particleSpawnCS.IsValid()) {
        m_particleSpawnCS = m_shaderMgr->CreateShader(
            EngineShader::ParticleSpawnCS, RHIShaderStage::Compute);
        if (!m_particleSpawnCS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ParticleSpawnCS");
            return false;
        }
    }
    return true;
}

/// Returns the particle spawn compute pipeline.
///
/// Initializes newly emitted particles with position, velocity, lifetime,
/// and other per-particle attributes. Dispatched with threadCount = emitCount.
RHIPipelineHandle PipelineLibrary::GetParticleSpawnCS() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetParticleSpawnCS called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_particleSpawnCS_PSO.IsValid()) {
        return m_particleSpawnCS_PSO;
    }

    if (!EnsureParticleSpawnCSShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_particleSpawnCS;
    desc.debugName     = "ParticleSpawnCS";

    m_particleSpawnCS_PSO = m_device->CreateComputePipeline(desc);
    if (!m_particleSpawnCS_PSO.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ParticleSpawnCS pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ParticleSpawnCS pipeline");
    }

    return m_particleSpawnCS_PSO;
}

// ─────────────────────────────────────────────────────────────────────────────
// Particle Update Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureParticleUpdateCSShader() {
    if (!m_particleUpdateCS.IsValid()) {
        m_particleUpdateCS = m_shaderMgr->CreateShader(
            EngineShader::ParticleUpdateCS, RHIShaderStage::Compute);
        if (!m_particleUpdateCS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ParticleUpdateCS");
            return false;
        }
    }
    return true;
}

/// Returns the particle update compute pipeline.
///
/// Steps the simulation forward: integrates velocity, applies forces/gravity,
/// ages particles, and writes dead indices to the free list.
RHIPipelineHandle PipelineLibrary::GetParticleUpdateCS() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetParticleUpdateCS called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_particleUpdateCS_PSO.IsValid()) {
        return m_particleUpdateCS_PSO;
    }

    if (!EnsureParticleUpdateCSShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_particleUpdateCS;
    desc.debugName     = "ParticleUpdateCS";

    m_particleUpdateCS_PSO = m_device->CreateComputePipeline(desc);
    if (!m_particleUpdateCS_PSO.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ParticleUpdateCS pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ParticleUpdateCS pipeline");
    }

    return m_particleUpdateCS_PSO;
}

// ─────────────────────────────────────────────────────────────────────────────
// Particle Indirect Args Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureParticleIndirectArgsCSShader() {
    if (!m_particleIndirectArgsCS.IsValid()) {
        m_particleIndirectArgsCS = m_shaderMgr->CreateShader(
            EngineShader::ParticleIndirectArgsCS, RHIShaderStage::Compute);
        if (!m_particleIndirectArgsCS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ParticleIndirectArgsCS");
            return false;
        }
    }
    return true;
}

/// Returns the particle indirect args compute pipeline.
///
/// Reads the alive count and writes DrawIndirect / DispatchIndirect argument
/// structs. Single-thread dispatch (1,1,1).
RHIPipelineHandle PipelineLibrary::GetParticleIndirectArgsCS() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetParticleIndirectArgsCS called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_particleIndirectArgsCS_PSO.IsValid()) {
        return m_particleIndirectArgsCS_PSO;
    }

    if (!EnsureParticleIndirectArgsCSShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_particleIndirectArgsCS;
    desc.debugName     = "ParticleIndirectArgsCS";

    m_particleIndirectArgsCS_PSO = m_device->CreateComputePipeline(desc);
    if (!m_particleIndirectArgsCS_PSO.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ParticleIndirectArgsCS pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ParticleIndirectArgsCS pipeline");
    }

    return m_particleIndirectArgsCS_PSO;
}

// ─────────────────────────────────────────────────────────────────────────────
// Particle Mesh Instance Transform Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureParticleMeshInstanceCSShader() {
    if (!m_particleMeshInstanceCS.IsValid()) {
        m_particleMeshInstanceCS = m_shaderMgr->CreateShader(
            EngineShader::ParticleMeshInstanceCS, RHIShaderStage::Compute);
        if (!m_particleMeshInstanceCS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ParticleMeshInstanceCS");
            return false;
        }
    }
    return true;
}

/// Returns the particle mesh instance transform compute pipeline.
///
/// Converts per-particle position/rotation/scale into per-instance world
/// matrices for the instanced mesh draw call.
RHIPipelineHandle PipelineLibrary::GetParticleMeshInstanceCS() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetParticleMeshInstanceCS called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_particleMeshInstanceCS_PSO.IsValid()) {
        return m_particleMeshInstanceCS_PSO;
    }

    if (!EnsureParticleMeshInstanceCSShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_particleMeshInstanceCS;
    desc.debugName     = "ParticleMeshInstanceCS";

    m_particleMeshInstanceCS_PSO = m_device->CreateComputePipeline(desc);
    if (!m_particleMeshInstanceCS_PSO.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ParticleMeshInstanceCS pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ParticleMeshInstanceCS pipeline");
    }

    return m_particleMeshInstanceCS_PSO;
}

// ─────────────────────────────────────────────────────────────────────────────
// Particle Sort Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureParticleSortCSShader() {
    if (!m_particleSortCS.IsValid()) {
        m_particleSortCS = m_shaderMgr->CreateShader(
            EngineShader::ParticleSortCS, RHIShaderStage::Compute);
        if (!m_particleSortCS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ParticleSortCS");
            return false;
        }
    }
    return true;
}

/// Returns the particle bitonic sort compute pipeline.
///
/// Two-phase depth sort: phase 0 generates sort keys from camera-relative
/// distances and performs a local bitonic sort within each thread group using
/// shared memory; phase 1 dispatches global merge passes across thread group
/// boundaries. Output: sorted index buffer for back-to-front rendering.
///
/// @ref Batcher 1968 "Sorting Networks and Their Applications" — bitonic merge sort
RHIPipelineHandle PipelineLibrary::GetParticleSortCS() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetParticleSortCS called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_particleSortCS_PSO.IsValid()) {
        return m_particleSortCS_PSO;
    }

    if (!EnsureParticleSortCSShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_particleSortCS;
    desc.debugName     = "ParticleSortCS";

    m_particleSortCS_PSO = m_device->CreateComputePipeline(desc);
    if (!m_particleSortCS_PSO.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ParticleSortCS pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ParticleSortCS pipeline");
    }

    return m_particleSortCS_PSO;
}

// ─────────────────────────────────────────────────────────────────────────────
// Decal Deferred Pipeline
// ─────────────────────────────────────────────────────────────────────────────

/// Compiles the decal deferred vertex and pixel shaders if not already cached.
///
/// Both shaders are compiled with `UZ_BINDLESS` when the device supports SM6.6.
/// The VS uses a built-in unit cube mesh from a structured buffer (no vertex input).
bool PipelineLibrary::EnsureDecalDeferredShaders() {
    if (!m_decalDeferredVS.IsValid()) {
        m_decalDeferredVS = m_shaderMgr->CreateShader(
            EngineShader::DecalDeferredVS, RHIShaderStage::Vertex);
        if (!m_decalDeferredVS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile DecalDeferredVS");
            return false;
        }
    }
    if (!m_decalDeferredPS.IsValid()) {
        m_decalDeferredPS = m_shaderMgr->CreateShader(
            EngineShader::DecalDeferredPS, RHIShaderStage::Pixel);
        if (!m_decalDeferredPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile DecalDeferredPS");
            return false;
        }
    }
    return true;
}

/// Returns the deferred decal projection pipeline.
///
/// PSO configuration:
/// - No vertex input layout (VS reads unit cube from structured buffer)
/// - Front-face culling: renders the inside of the decal box so the PS runs
///   for all pixels inside the volume even when the camera is outside
/// - Depth read-only with reversed-Z (Greater): decals only affect surfaces
///   that are closer than the back face of the box
/// - Blend disabled: decals overwrite G-Buffer channels directly
/// - 4 render targets matching the GBuffer layout
RHIPipelineHandle PipelineLibrary::GetDecalDeferred() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetDecalDeferred called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_decalDeferred.IsValid()) {
        return m_decalDeferred;
    }

    if (!EnsureDecalDeferredShaders()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_decalDeferredVS;
    desc.pixelShader   = m_decalDeferredPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;

    // No vertex input — VS reads unit cube vertices from structured buffer
    desc.inputElementCount = 0;

    // Front-face cull: render the inside (back faces) of the decal box
    desc.rasterizer.cullMode              = RHICullMode::Front;

    // Depth read-only: decals project onto existing scene geometry
    desc.depthStencil.depthEnable = true;
    desc.depthStencil.depthWrite  = false;
    desc.depthStencil.depthFunc   = RHICompareFunc::Greater; // Reversed-Z

    // No blend — decals overwrite G-Buffer channels
    // (blendEnable defaults to false)

    // G-Buffer render targets — must match GBuffer::Create() texture formats
    desc.renderTargetFormats[0] = RHIFormat::RGBA8Unorm;   // albedo+AO
    desc.renderTargetFormats[1] = RHIFormat::RGBA8Unorm;   // octNormal+metallic+roughness
    desc.renderTargetFormats[2] = RHIFormat::RGBA32Float;  // world-space position (debug)
    desc.renderTargetFormats[3] = RHIFormat::RGBA16Float;  // HDR emissive
    desc.renderTargetCount      = 4;
    desc.depthStencilFormat     = RHIFormat::D32Float;
    desc.debugName              = "DecalDeferred";

    m_decalDeferred = CreateCachedPipeline(desc);
    if (!m_decalDeferred.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DecalDeferred pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created DecalDeferred pipeline");
    }

    return m_decalDeferred;
}

// ─────────────────────────────────────────────────────────────────────────────
// Trail Ribbon Pipeline
// ─────────────────────────────────────────────────────────────────────────────

/// Compiles the trail vertex and pixel shaders if not already cached.
///
/// The VS uses SV_VertexID to read trail vertices from a structured buffer
/// (no vertex input layout). Compiled with `UZ_BINDLESS` on SM6.6.
bool PipelineLibrary::EnsureTrailRibbonShaders() {
    if (!m_trailVS.IsValid()) {
        m_trailVS = m_shaderMgr->CreateShader(
            EngineShader::TrailVS, RHIShaderStage::Vertex);
        if (!m_trailVS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile TrailVS");
            return false;
        }
    }
    if (!m_trailPS.IsValid()) {
        m_trailPS = m_shaderMgr->CreateShader(
            EngineShader::TrailPS, RHIShaderStage::Pixel);
        if (!m_trailPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile TrailPS");
            return false;
        }
    }
    return true;
}

/// Returns the trail ribbon pipeline (alpha blended, two-sided).
///
/// PSO configuration:
/// - No vertex input layout (SV_VertexID reads from structured buffer)
/// - Two-sided rendering (ribbons are visible from both sides)
/// - Depth read-only (trails do not occlude scene geometry)
/// - SrcAlpha / InvSrcAlpha alpha blending over HDR scene color
/// - Single HDR render target (RGBA16Float)
RHIPipelineHandle PipelineLibrary::GetTrailRibbon() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetTrailRibbon called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_trailRibbon.IsValid()) {
        return m_trailRibbon;
    }

    if (!EnsureTrailRibbonShaders()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_trailVS;
    desc.pixelShader   = m_trailPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;

    // No vertex input — SV_VertexID reads from structured buffer
    desc.inputElementCount = 0;

    // Two-sided: ribbons are visible from both directions
    desc.rasterizer.cullMode              = RHICullMode::None;

    // Depth read-only: trails do not occlude scene geometry
    desc.depthStencil.depthEnable = true;
    desc.depthStencil.depthWrite  = false;
    desc.depthStencil.depthFunc   = RHICompareFunc::Greater; // Reversed-Z

    // Standard alpha blend: SrcAlpha / InvSrcAlpha
    desc.renderTargetBlend[0].blendEnable = true;
    desc.renderTargetBlend[0].srcColor    = RHIBlendFactor::SrcAlpha;
    desc.renderTargetBlend[0].dstColor    = RHIBlendFactor::InvSrcAlpha;
    desc.renderTargetBlend[0].colorOp     = RHIBlendOp::Add;
    desc.renderTargetBlend[0].srcAlpha    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].dstAlpha    = RHIBlendFactor::InvSrcAlpha;
    desc.renderTargetBlend[0].alphaOp     = RHIBlendOp::Add;

    desc.renderTargetFormats[0] = RHIFormat::RGBA16Float; // HDR scene color
    desc.renderTargetCount      = 1;
    desc.depthStencilFormat     = RHIFormat::D32Float;
    desc.debugName              = "TrailRibbon";

    m_trailRibbon = CreateCachedPipeline(desc);
    if (!m_trailRibbon.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create TrailRibbon pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created TrailRibbon pipeline");
    }

    return m_trailRibbon;
}

// ─────────────────────────────────────────────────────────────────────────────
// Particle Mesh Pipeline (Opaque + Transparent)
// ─────────────────────────────────────────────────────────────────────────────

/// Compiles the particle mesh vertex and pixel shaders if not already cached.
///
/// Unlike billboard/trail shaders, the mesh VS reads actual vertex positions
/// from a vertex buffer (POSITION float3, input slot 0). Per-instance world
/// transforms come from a structured buffer written by ParticleMeshInstanceCS.
bool PipelineLibrary::EnsureParticleMeshShaders() {
    if (!m_particleMeshVS.IsValid()) {
        m_particleMeshVS = m_shaderMgr->CreateShader(
            EngineShader::ParticleMeshVS, RHIShaderStage::Vertex);
        if (!m_particleMeshVS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ParticleMeshVS");
            return false;
        }
    }
    if (!m_particleMeshPS.IsValid()) {
        m_particleMeshPS = m_shaderMgr->CreateShader(
            EngineShader::ParticleMeshPS, RHIShaderStage::Pixel);
        if (!m_particleMeshPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ParticleMeshPS");
            return false;
        }
    }
    return true;
}

/// Returns the opaque instanced mesh particle pipeline.
///
/// PSO configuration:
/// - Vertex input: POSITION float3 at slot 0 (actual mesh geometry)
/// - Per-instance world matrices read from structured buffer (no per-instance vertex input)
/// - Back-face culling (opaque meshes have consistent winding)
/// - Depth enabled + depth write enabled (opaque particles occlude)
/// - Reversed-Z: Greater comparison
/// - No blending (fully opaque)
/// - Single HDR render target (RGBA16Float)
///
/// Draw call: `DrawIndexedInstanced(indexCount, aliveCount, 0, 0, 0)` via DrawIndirect.
RHIPipelineHandle PipelineLibrary::GetParticleMeshOpaque() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetParticleMeshOpaque called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_particleMeshOpaque.IsValid()) {
        return m_particleMeshOpaque;
    }

    if (!EnsureParticleMeshShaders()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_particleMeshVS;
    desc.pixelShader   = m_particleMeshPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;

    // Vertex input: mesh position data from vertex buffer slot 0
    RHIInputElementDesc inputElements[1] = {};
    inputElements[0].semantic      = RHIInputSemantic::Position;
    inputElements[0].semanticIndex = 0;
    inputElements[0].format        = RHIFormat::RGB32Float;
    inputElements[0].inputSlot     = 0;
    inputElements[0].byteOffset    = 0;
    desc.inputElements     = inputElements;
    desc.inputElementCount = 1;

    // Opaque particle meshes — cull back faces
    desc.rasterizer.cullMode              = RHICullMode::Back;

    // Depth enabled + write: opaque particles participate fully in depth buffer
    desc.depthStencil.depthEnable = true;
    desc.depthStencil.depthWrite  = true;
    desc.depthStencil.depthFunc   = RHICompareFunc::Greater; // Reversed-Z

    // No blend — fully opaque (blendEnable defaults to false)

    desc.renderTargetFormats[0] = RHIFormat::RGBA16Float; // HDR scene color
    desc.renderTargetCount      = 1;
    desc.depthStencilFormat     = RHIFormat::D32Float;
    desc.debugName              = "ParticleMeshOpaque";

    m_particleMeshOpaque = CreateCachedPipeline(desc);
    if (!m_particleMeshOpaque.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ParticleMeshOpaque pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ParticleMeshOpaque pipeline");
    }

    return m_particleMeshOpaque;
}

/// Returns the transparent instanced mesh particle pipeline.
///
/// PSO configuration identical to GetParticleMeshOpaque() except:
/// - No backface culling (transparent meshes visible from both sides)
/// - Depth read-only (transparent particles do not occlude each other)
/// - SrcAlpha / InvSrcAlpha alpha blending over HDR scene color
///
/// Draw call: same DrawIndirect pattern as the opaque variant.
RHIPipelineHandle PipelineLibrary::GetParticleMeshTransparent() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetParticleMeshTransparent called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_particleMeshTransparent.IsValid()) {
        return m_particleMeshTransparent;
    }

    if (!EnsureParticleMeshShaders()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_particleMeshVS;
    desc.pixelShader   = m_particleMeshPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;

    // Vertex input: mesh position data from vertex buffer slot 0
    RHIInputElementDesc inputElements[1] = {};
    inputElements[0].semantic      = RHIInputSemantic::Position;
    inputElements[0].semanticIndex = 0;
    inputElements[0].format        = RHIFormat::RGB32Float;
    inputElements[0].inputSlot     = 0;
    inputElements[0].byteOffset    = 0;
    desc.inputElements     = inputElements;
    desc.inputElementCount = 1;

    // Two-sided: transparent meshes visible from both sides
    desc.rasterizer.cullMode              = RHICullMode::None;

    // Depth read-only: transparent particles do not occlude each other
    desc.depthStencil.depthEnable = true;
    desc.depthStencil.depthWrite  = false;
    desc.depthStencil.depthFunc   = RHICompareFunc::Greater; // Reversed-Z

    // Standard alpha blend: SrcAlpha / InvSrcAlpha
    desc.renderTargetBlend[0].blendEnable = true;
    desc.renderTargetBlend[0].srcColor    = RHIBlendFactor::SrcAlpha;
    desc.renderTargetBlend[0].dstColor    = RHIBlendFactor::InvSrcAlpha;
    desc.renderTargetBlend[0].colorOp     = RHIBlendOp::Add;
    desc.renderTargetBlend[0].srcAlpha    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].dstAlpha    = RHIBlendFactor::InvSrcAlpha;
    desc.renderTargetBlend[0].alphaOp     = RHIBlendOp::Add;

    desc.renderTargetFormats[0] = RHIFormat::RGBA16Float; // HDR scene color
    desc.renderTargetCount      = 1;
    desc.depthStencilFormat     = RHIFormat::D32Float;
    desc.debugName              = "ParticleMeshTransparent";

    m_particleMeshTransparent = CreateCachedPipeline(desc);
    if (!m_particleMeshTransparent.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ParticleMeshTransparent pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ParticleMeshTransparent pipeline");
    }

    return m_particleMeshTransparent;
}

} // namespace uz
