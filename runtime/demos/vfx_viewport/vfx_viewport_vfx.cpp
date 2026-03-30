/// VFX Viewport Demo — GPU Particle System Implementation
///
/// Creates SoA particle buffers, compiles compute/graphics pipelines via
/// ShaderManager, and dispatches spawn/update/sort/billboard passes.

#include "vfx_viewport_vfx.h"
#include "rhi_device.h"
#include "rhi_command_list.h"
#include "shader_manager.h"
#include "cb_ring_allocator.h"
#include "engine_shaders.h"
#include "log.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VFX Presets (matches the original uz_viewport.cpp presets)
// ─────────────────────────────────────────────────────────────────────────────

static const VFXPreset s_presets[] = {
    { "Simple Burst", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.2f,
        /*velocity*/ {0.0f, 0.0f, 6.0f}, /*velVariation*/ 3.0f,
        /*spread*/ 1.0f, /*minLife*/ 1.0f, /*maxLife*/ 2.5f, /*size*/ 1.0f,
        /*color*/ {1.0f, 0.8f, 0.4f, 1.0f},
        /*gravity*/ {0.0f, 0.0f, -3.0f}, /*damping*/ 0.15f,
        /*softFade*/ 0.5f, /*emissive*/ 1.0f,
        /*sizeOverLife*/ 0.5f, 1.2f, 0.0f, 0.3f,
        /*colorLifeStart*/ 1.0f, 0.9f, 0.5f, 1.0f,
        /*colorLifeEnd*/ 1.0f, 0.3f, 0.05f, 0.0f,
        /*alignment*/ 0, /*stretch*/ 0.0f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 1 // Additive — fire glow on dark backgrounds
    }, 300.0f },
    { "Smoke Plume", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.2f,
        /*velocity*/ {0.0f, 0.0f, 3.0f}, /*velVariation*/ 0.5f,
        /*spread*/ 0.6f, /*minLife*/ 3.0f, /*maxLife*/ 6.0f, /*size*/ 1.2f,
        /*color*/ {0.5f, 0.5f, 0.5f, 0.7f},
        /*gravity*/ {0.0f, 0.0f, -1.0f}, /*damping*/ 0.2f,
        /*softFade*/ 0.5f, /*emissive*/ 0.0f,
        /*sizeOverLife*/ 0.3f, 1.5f, 2.5f, 0.3f,
        /*colorLifeStart*/ 0.7f, 0.6f, 0.5f, 0.8f,
        /*colorLifeEnd*/ 0.2f, 0.2f, 0.2f, 0.0f,
        /*alignment*/ 0, /*stretch*/ 0.0f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 0 // AlphaBlend — smoke is translucent, not additive
    }, 200.0f },
    { "Sparks", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.15f,
        /*velocity*/ {0.0f, 0.0f, 10.0f}, /*velVariation*/ 4.0f,
        /*spread*/ 1.0f, /*minLife*/ 0.5f, /*maxLife*/ 1.5f, /*size*/ 0.15f,
        /*color*/ {1.0f, 0.9f, 0.5f, 1.0f},
        /*gravity*/ {0.0f, 0.0f, -12.0f}, /*damping*/ 0.03f,
        /*softFade*/ 0.2f, /*emissive*/ 2.0f,
        /*sizeOverLife*/ 1.0f, 1.0f, 0.3f, 0.0f,
        /*colorLifeStart*/ 1.0f, 0.95f, 0.7f, 1.0f,
        /*colorLifeEnd*/ 1.0f, 0.3f, 0.0f, 0.2f,
        /*alignment*/ 1, /*stretch*/ 2.0f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 1 // Additive — bright spark streaks
    }, 800.0f },
    { "Magic Orb", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.8f,
        /*velocity*/ {0.0f, 0.0f, 1.0f}, /*velVariation*/ 0.6f,
        /*spread*/ 3.14159f, /*minLife*/ 1.0f, /*maxLife*/ 2.0f, /*size*/ 0.5f,
        /*color*/ {0.3f, 0.6f, 1.0f, 0.9f},
        /*gravity*/ {0.0f, 0.0f, 0.0f}, /*damping*/ 0.3f,
        /*softFade*/ 0.3f, /*emissive*/ 0.5f,
        /*sizeOverLife*/ 0.5f, 1.0f, 0.2f, 0.4f,
        /*colorLifeStart*/ 0.4f, 0.8f, 1.0f, 1.0f,
        /*colorLifeEnd*/ 0.1f, 0.2f, 0.8f, 0.0f,
        /*alignment*/ 0, /*stretch*/ 0.0f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 1 // Additive — glowing magical particles
    }, 300.0f },
    { "Explosion Debris", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.3f,
        /*velocity*/ {0.0f, 0.0f, 8.0f}, /*velVariation*/ 4.0f,
        /*spread*/ 1.5f, /*minLife*/ 0.5f, /*maxLife*/ 2.0f, /*size*/ 0.5f,
        /*color*/ {1.0f, 0.5f, 0.15f, 1.0f},
        /*gravity*/ {0.0f, 0.0f, -20.0f}, /*damping*/ 0.08f,
        /*softFade*/ 0.3f, /*emissive*/ 1.0f,
        /*sizeOverLife*/ 0.8f, 1.5f, 0.1f, 0.1f,
        /*colorLifeStart*/ 1.0f, 0.8f, 0.4f, 1.0f,
        /*colorLifeEnd*/ 0.4f, 0.08f, 0.0f, 0.0f,
        /*alignment*/ 1, /*stretch*/ 2.0f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 1 // Additive — hot glowing debris
    }, 2000.0f },
    { "Fireball", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.4f,
        /*velocity*/ {0.0f, 0.0f, 0.5f}, /*velVariation*/ 1.5f,
        /*spread*/ 2.5f, /*minLife*/ 0.3f, /*maxLife*/ 0.8f, /*size*/ 0.6f,
        /*color*/ {1.0f, 0.5f, 0.1f, 0.9f},
        /*gravity*/ {0.0f, 0.0f, 1.0f}, /*damping*/ 0.5f,
        /*softFade*/ 0.4f, /*emissive*/ 2.5f,
        /*sizeOverLife*/ 0.5f, 1.3f, 0.4f, 0.2f,
        /*colorLifeStart*/ 1.0f, 0.7f, 0.2f, 1.0f,
        /*colorLifeEnd*/ 0.7f, 0.15f, 0.02f, 0.3f,
        /*alignment*/ 0, /*stretch*/ 0.5f,
        /*spawnShape*/ 0, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 1 // Additive — intense fireball glow
    }, 800.0f },
    // ── Spawn Shape Presets ──────────────────────────────────────────────
    { "Ring of Fire", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 3.0f,
        /*velocity*/ {0.0f, 0.0f, 4.0f}, /*velVariation*/ 1.0f,
        /*spread*/ 0.5f, /*minLife*/ 0.5f, /*maxLife*/ 1.5f, /*size*/ 0.4f,
        /*color*/ {1.0f, 0.5f, 0.1f, 0.9f},
        /*gravity*/ {0.0f, 0.0f, -2.0f}, /*damping*/ 0.2f,
        /*softFade*/ 0.3f, /*emissive*/ 1.5f,
        /*sizeOverLife*/ 0.5f, 1.0f, 0.0f, 0.3f,
        /*colorLifeStart*/ 1.0f, 0.8f, 0.3f, 1.0f,
        /*colorLifeEnd*/ 1.0f, 0.2f, 0.0f, 0.0f,
        /*alignment*/ 0, /*stretch*/ 0.0f,
        /*spawnShape*/ 3, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 0.3f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 1 // Additive — ring of fire glow
    }, 400.0f },
    { "Cone Jet", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 0.1f,
        /*velocity*/ {0.0f, 0.0f, 10.0f}, /*velVariation*/ 2.0f,
        /*spread*/ 0.3f, /*minLife*/ 0.5f, /*maxLife*/ 1.5f, /*size*/ 0.2f,
        /*color*/ {0.3f, 0.6f, 1.0f, 0.9f},
        /*gravity*/ {0.0f, 0.0f, -5.0f}, /*damping*/ 0.1f,
        /*softFade*/ 0.3f, /*emissive*/ 2.0f,
        /*sizeOverLife*/ 0.3f, 1.0f, 0.0f, 0.3f,
        /*colorLifeStart*/ 0.5f, 0.8f, 1.0f, 1.0f,
        /*colorLifeEnd*/ 0.1f, 0.3f, 0.8f, 0.0f,
        /*alignment*/ 1, /*stretch*/ 1.5f,
        /*spawnShape*/ 4, /*spawnOnSurface*/ 1,
        /*shapeParam1*/ 0.5f, /*shapeParam2*/ 2.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 1 // Additive — bright energy jet
    }, 600.0f },
    { "Cylinder Volume", {
        /*position*/ {0.0f, 0.0f, 0.0f}, /*spawnRadius*/ 2.0f,
        /*velocity*/ {0.0f, 0.0f, 2.0f}, /*velVariation*/ 0.5f,
        /*spread*/ 0.8f, /*minLife*/ 1.5f, /*maxLife*/ 3.0f, /*size*/ 0.3f,
        /*color*/ {0.2f, 0.9f, 0.3f, 0.8f},
        /*gravity*/ {0.0f, 0.0f, -1.0f}, /*damping*/ 0.15f,
        /*softFade*/ 0.4f, /*emissive*/ 0.5f,
        /*sizeOverLife*/ 0.5f, 1.2f, 0.3f, 0.4f,
        /*colorLifeStart*/ 0.3f, 1.0f, 0.4f, 1.0f,
        /*colorLifeEnd*/ 0.1f, 0.5f, 0.1f, 0.0f,
        /*alignment*/ 0, /*stretch*/ 0.0f,
        /*spawnShape*/ 5, /*spawnOnSurface*/ 0,
        /*shapeParam1*/ 4.0f, /*shapeParam2*/ 0.0f, /*shapeParam3*/ 0.0f,
        /*blendMode*/ 0 // AlphaBlend — translucent volume particles
    }, 300.0f },
};

const VFXPreset* GetVFXPresets(int& outCount) {
    outCount = static_cast<int>(sizeof(s_presets) / sizeof(s_presets[0]));
    return s_presets;
}

// ─────────────────────────────────────────────────────────────────────────────
// Channel Stride
// ─────────────────────────────────────────────────────────────────────────────

uint32_t VFXParticleSystem::ChannelStride(uint32_t channel) {
    // Channels: 0=positions(float4=16), 1=velocities(float4=16), 2=ages(float=4),
    //           3=lifespans(float=4), 4=sizes(float=4), 5=colors(float4=16), 6=rotations(float=4)
    switch (channel) {
        case 0: return 16; // float4 positions (xyz + padding)
        case 1: return 16; // float4 velocities (xyz + padding)
        case 2: return 4;  // float ages
        case 3: return 4;  // float lifespans
        case 4: return 4;  // float sizes
        case 5: return 16; // float4 colors (rgba)
        case 6: return 4;  // float rotations
        default: return 4;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Creation Helper
// ─────────────────────────────────────────────────────────────────────────────

RHIBufferHandle VFXParticleSystem::CreateParticleBuffer(IRHIDevice* device,
                                                          uint32_t stride,
                                                          const char* name) {
    RHIBufferDesc desc{};
    desc.sizeBytes = static_cast<uint64_t>(m_maxParticles) * stride;
    desc.stride = stride;
    desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
    desc.memory = RHIMemoryType::Default;
    desc.debugName = name;
    return device->CreateBuffer(desc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

bool VFXParticleSystem::Init(IRHIDevice* device, ShaderManager* shaderMgr,
                              uint32_t maxParticles) {
    m_device = device;
    m_maxParticles = maxParticles;
    m_pingPong = 0;

    // Channel names for debug labels
    static const char* kChannelNames[] = {
        "VFX_Positions", "VFX_Velocities", "VFX_Ages",
        "VFX_Lifespans", "VFX_Sizes", "VFX_Colors", "VFX_Rotations"
    };

    // Create SoA ping-pong buffers
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        const uint32_t stride = ChannelStride(ch);
        for (int pp = 0; pp < 2; ++pp) {
            m_particleBuffers[ch][pp] = CreateParticleBuffer(device, stride, kChannelNames[ch]);
            if (!m_particleBuffers[ch][pp].IsValid()) {
                UZ_LOG(App, Error, "VFX: Failed to create particle buffer ch={} pp={}", ch, pp);
                return false;
            }
        }
    }

    // Alive count buffer (single uint32)
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = "VFX_AliveCount";
        m_aliveCount = device->CreateBuffer(desc);
    }

    // Free list buffer (one uint32 per particle slot)
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(maxParticles) * sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = "VFX_FreeList";
        m_freeList = device->CreateBuffer(desc);
    }

    // Free list head (single uint32)
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = "VFX_FreeListHead";
        m_freeListHead = device->CreateBuffer(desc);
    }

    // Indirect args buffer: DrawIndexedIndirect args (6 x uint32 = 24 bytes)
    // Format: [vertexCountPerInstance, instanceCount, startIndex, baseVertex, startInstance, padding]
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = 6 * sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::IndirectArgs | RHIBufferUsage::UnorderedAccess
                   | RHIBufferUsage::StructuredBuffer;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = "VFX_IndirectArgs";
        m_indirectArgs = device->CreateBuffer(desc);
    }

    // Sort buffers
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(maxParticles) * sizeof(float);
        desc.stride = sizeof(float);
        desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = "VFX_SortKeys";
        m_sortKeys = device->CreateBuffer(desc);
    }
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(maxParticles) * sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = "VFX_SortIndices";
        m_sortIndices = device->CreateBuffer(desc);
    }

    // Compile compute pipelines via RHIComputePipelineDesc + CreateComputePipeline
    {
        RHIComputePipelineDesc desc{};
        desc.computeShader = shaderMgr->CreateShader(EngineShader::ParticleSpawnCS, RHIShaderStage::Compute);
        desc.debugName = "VFX_SpawnCS";
        m_spawnPSO = device->CreateComputePipeline(desc);
        if (!m_spawnPSO.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create spawn compute PSO");
            return false;
        }
    }
    {
        RHIComputePipelineDesc desc{};
        desc.computeShader = shaderMgr->CreateShader(EngineShader::ParticleUpdateCS, RHIShaderStage::Compute);
        desc.debugName = "VFX_UpdateCS";
        m_updatePSO = device->CreateComputePipeline(desc);
        if (!m_updatePSO.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create update compute PSO");
            return false;
        }
    }
    {
        RHIComputePipelineDesc desc{};
        desc.computeShader = shaderMgr->CreateShader(EngineShader::ParticleIndirectArgsCS, RHIShaderStage::Compute);
        desc.debugName = "VFX_IndirectArgsCS";
        m_indirectArgsPSO = device->CreateComputePipeline(desc);
        if (!m_indirectArgsPSO.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create indirect args compute PSO");
            return false;
        }
    }
    {
        RHIComputePipelineDesc desc{};
        desc.computeShader = shaderMgr->CreateShader(EngineShader::ParticleSortCS, RHIShaderStage::Compute);
        desc.debugName = "VFX_SortCS";
        m_sortPSO = device->CreateComputePipeline(desc);
        if (!m_sortPSO.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create sort compute PSO");
            return false;
        }
    }

    // Billboard graphics pipeline: alpha blend, no depth write, back-face cull off
    {
        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader = shaderMgr->CreateShader(EngineShader::ParticleBillboardVS, RHIShaderStage::Vertex);
        desc.pixelShader = shaderMgr->CreateShader(EngineShader::ParticleBillboardPS, RHIShaderStage::Pixel);
        desc.renderTargetCount = 1;
        desc.renderTargetFormats[0] = RHIFormat::RGBA16Float; // HDR target
        desc.depthStencilFormat = RHIFormat::D32Float;
        desc.depthStencil.depthEnable = true;
        desc.depthStencil.depthWrite = false; // Particles don't write depth
        // depthFunc defaults to Greater (reversed-Z)
        desc.rasterizer.cullMode = RHICullMode::None;
        // AlphaComposite / PremulAlpha (blend mode 2): One / InvSrcAlpha
        // The pixel shader outputs premultiplied RGB (rgb *= opacity before return).
        // SrcAlpha=Zero, DstAlpha=InvSrcAlpha preserves correct destination alpha
        // compositing (Niagara exact match for alpha channel handling).
        desc.renderTargetBlend[0].blendEnable = true;
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].colorOp = RHIBlendOp::Add;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::Zero;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].alphaOp = RHIBlendOp::Add;
        // No input layout — billboard VS reads from structured buffers via SV_VertexID
        desc.inputElementCount = 0;
        desc.debugName = "VFX_BillboardPSO";
        m_billboardPSO = device->CreateGraphicsPipeline(desc);
        if (!m_billboardPSO.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create billboard graphics PSO (PremulAlpha)");
            return false;
        }

        // Additive PSO (blend mode 1): One/One — pure additive for fire, sparks, glow.
        // RGB adds directly to the framebuffer. Alpha: Zero/InvSrcAlpha preserves
        // destination alpha correctly (Niagara exact match).
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::One;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::Zero;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::InvSrcAlpha;
        desc.debugName = "VFX_BillboardPSO_Additive";
        m_billboardPSO_Additive = device->CreateGraphicsPipeline(desc);
        if (!m_billboardPSO_Additive.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create billboard graphics PSO (Additive)");
            return false;
        }

        // Translucent / AlphaBlend PSO (blend mode 0): SrcAlpha/InvSrcAlpha — standard
        // transparency for smoke, dust, soft translucent particles. The blend state
        // multiplies RGB by alpha automatically. Alpha: Zero/InvSrcAlpha preserves
        // destination alpha correctly (Niagara exact match).
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::Zero;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::InvSrcAlpha;
        desc.debugName = "VFX_BillboardPSO_AlphaBlend";
        m_billboardPSO_AlphaBlend = device->CreateGraphicsPipeline(desc);
        if (!m_billboardPSO_AlphaBlend.IsValid()) {
            UZ_LOG(App, Error, "VFX: Failed to create billboard graphics PSO (AlphaBlend)");
            return false;
        }
    }

    UZ_LOG(App, Info, "VFX: Initialized particle system — maxParticles={}", maxParticles);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Upload Initial Free List
// ─────────────────────────────────────────────────────────────────────────────

void VFXParticleSystem::UploadInitialFreeList(IRHICommandList* cmd) {
    // Free list: array of [0, 1, 2, ..., maxParticles-1] — every slot starts free
    {
        const uint64_t sizeBytes = static_cast<uint64_t>(m_maxParticles) * sizeof(uint32_t);
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeBytes;
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer;
        desc.memory = RHIMemoryType::Upload;
        desc.debugName = "VFX_FreeList_Staging";
        m_freeListStaging = m_device->CreateBuffer(desc);

        void* mapped = m_device->MapBuffer(m_freeListStaging);
        if (mapped) {
            auto* indices = static_cast<uint32_t*>(mapped);
            for (uint32_t i = 0; i < m_maxParticles; ++i) {
                indices[i] = i;
            }
            m_device->UnmapBuffer(m_freeListStaging);
        }
        cmd->CopyBuffer(m_freeListStaging, 0, m_freeList, 0, sizeBytes);
    }

    // Free list head: initial value = maxParticles (all slots are free)
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer;
        desc.memory = RHIMemoryType::Upload;
        desc.debugName = "VFX_FreeListHead_Staging";
        m_freeListHeadStaging = m_device->CreateBuffer(desc);

        void* mapped = m_device->MapBuffer(m_freeListHeadStaging);
        if (mapped) {
            auto* head = static_cast<uint32_t*>(mapped);
            *head = m_maxParticles;
            m_device->UnmapBuffer(m_freeListHeadStaging);
        }
        cmd->CopyBuffer(m_freeListHeadStaging, 0, m_freeListHead, 0, sizeof(uint32_t));
    }

    // Alive count: initial value = 0 (no particles alive yet)
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer;
        desc.memory = RHIMemoryType::Upload;
        desc.debugName = "VFX_AliveCount_Staging";
        m_aliveCountStaging = m_device->CreateBuffer(desc);

        void* mapped = m_device->MapBuffer(m_aliveCountStaging);
        if (mapped) {
            auto* count = static_cast<uint32_t*>(mapped);
            *count = 0;
            m_device->UnmapBuffer(m_aliveCountStaging);
        }
        cmd->CopyBuffer(m_aliveCountStaging, 0, m_aliveCount, 0, sizeof(uint32_t));
    }

    // Zero-initialize ALL particle attribute buffers (both ping-pong sets).
    // GPU Default memory has undefined contents — if the update shader reads
    // uninitialized data on frame 0, garbage lifespans could make particles
    // appear "alive" and corrupt the alive count. With all-zero data:
    // age=0, lifespan=0 → age >= lifespan → all particles are dead (correct).
    {
        // Find largest channel size for a reusable staging buffer
        uint64_t maxChannelSize = 0;
        for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
            const uint64_t chSize = static_cast<uint64_t>(m_maxParticles) * ChannelStride(ch);
            if (chSize > maxChannelSize) maxChannelSize = chSize;
        }

        RHIBufferDesc desc{};
        desc.sizeBytes = maxChannelSize;
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer;
        desc.memory = RHIMemoryType::Upload;
        desc.debugName = "VFX_ZeroInit_Staging";
        m_zeroInitStaging = m_device->CreateBuffer(desc);

        void* mapped = m_device->MapBuffer(m_zeroInitStaging);
        if (mapped) {
            std::memset(mapped, 0, maxChannelSize);
            m_device->UnmapBuffer(m_zeroInitStaging);
        }

        for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
            const uint64_t chSize = static_cast<uint64_t>(m_maxParticles) * ChannelStride(ch);
            for (int pp = 0; pp < 2; ++pp) {
                cmd->CopyBuffer(m_zeroInitStaging, 0,
                                m_particleBuffers[ch][pp], 0, chSize);
            }
        }
    }

    UZ_LOG(App, Info, "VFX: Uploaded initial free list — {} slots available", m_maxParticles);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void VFXParticleSystem::Shutdown(IRHIDevice* device) {
    if (!device) return;

    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        for (int pp = 0; pp < 2; ++pp) {
            if (m_particleBuffers[ch][pp].IsValid()) {
                device->DestroyBuffer(m_particleBuffers[ch][pp]);
                m_particleBuffers[ch][pp] = {};
            }
        }
    }

    auto destroyBuf = [device](RHIBufferHandle& h) {
        if (h.IsValid()) { device->DestroyBuffer(h); h = {}; }
    };
    destroyBuf(m_aliveCount);
    destroyBuf(m_freeList);
    destroyBuf(m_freeListHead);
    destroyBuf(m_indirectArgs);
    destroyBuf(m_sortKeys);
    destroyBuf(m_sortIndices);
    destroyBuf(m_freeListStaging);
    destroyBuf(m_freeListHeadStaging);
    destroyBuf(m_aliveCountStaging);
    destroyBuf(m_zeroInitStaging);

    auto destroyPso = [device](RHIPipelineHandle& h) {
        if (h.IsValid()) { device->DestroyPipeline(h); h = {}; }
    };
    destroyPso(m_spawnPSO);
    destroyPso(m_updatePSO);
    destroyPso(m_indirectArgsPSO);
    destroyPso(m_sortPSO);
    destroyPso(m_billboardPSO);
    destroyPso(m_billboardPSO_Additive);
    destroyPso(m_billboardPSO_AlphaBlend);
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch Compute
// ─────────────────────────────────────────────────────────────────────────────

void VFXParticleSystem::DispatchCompute(IRHICommandList* cmdList, CBRingAllocator* cbRing,
                                         const VFXEmitterConfig& config, uint32_t spawnCount,
                                         float dt, uint32_t frameNumber) {
    const int read = m_pingPong;
    const int write = 1 - m_pingPong;

    // ── Reset alive count to 0 before this frame's simulation ─────────
    // Both spawn and update atomically increment aliveCount. Without a
    // per-frame reset, the counter grows without bound and the free list
    // eventually exhausts (particles stop spawning).
    if (m_aliveCountStaging.IsValid()) {
        cmdList->CopyBuffer(m_aliveCountStaging, 0, m_aliveCount, 0, sizeof(uint32_t));
    }

    // ── UAV barriers before compute ──────────────────────────────────────
    // Transition all write-set buffers to UAV state. Read-set already in SRV
    // from the previous frame's billboard draw.
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_particleBuffers[ch][write];
        barrier.stateBefore = RHIResourceState::Common;
        barrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // Transition aliveCount to UAV after the copy
    {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_aliveCount;
        barrier.stateBefore = RHIResourceState::CopyDst;
        barrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Spawn Pass ───────────────────────────────────────────────────────
    if (spawnCount > 0) {
        VFXSpawnCB cb{};
        std::memcpy(cb.emitterPos, config.position, sizeof(float) * 3);
        cb.spawnRadius = config.spawnRadius;
        std::memcpy(cb.spawnVelocity, config.spawnVelocity, sizeof(float) * 3);
        cb.velocityVariation = config.velocityVariation;
        cb.spreadAngle = config.spreadAngle;
        cb.minLifetime = config.minLifetime;
        cb.maxLifetime = config.maxLifetime;
        cb.initialSize = config.initialSize;
        std::memcpy(cb.initialColor, config.initialColor, sizeof(float) * 4);
        cb.dt = dt;
        cb.frameNumber = frameNumber;
        cb.spawnCount = spawnCount;
        cb.maxParticles = m_maxParticles;

        // Bindless indices for write-set buffers
        cb.freeListSrvIndex = m_device->GetBufferSRVIndex(m_freeList);
        cb.positionsUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[0][write]);
        cb.velocitiesUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[1][write]);
        cb.agesUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[2][write]);
        cb.lifespansUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[3][write]);
        cb.sizesUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[4][write]);
        cb.colorsUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[5][write]);
        cb.rotationsUavIndex = m_device->GetBufferUAVIndex(m_particleBuffers[6][write]);
        cb.freeListHeadUavIndex = m_device->GetBufferUAVIndex(m_freeListHead);
        cb.aliveCountUavIndex = m_device->GetBufferUAVIndex(m_aliveCount);

        // Spawn shape parameters
        cb.spawnShape = config.spawnShape;
        cb.spawnOnSurface = config.spawnOnSurface;
        cb.shapeParam1 = config.shapeParam1;
        cb.shapeParam2 = config.shapeParam2;
        cb.shapeParam3 = config.shapeParam3;

        auto alloc = cbRing->Allocate(sizeof(VFXSpawnCB));
        std::memcpy(alloc.cpuPtr, &cb, sizeof(VFXSpawnCB));

        cmdList->SetPipeline(m_spawnPSO);
        cmdList->SetRootCBV(1, alloc.gpuAddr); // b1 = root param 1 (CBV)
        cmdList->Dispatch((spawnCount + kSpawnGroupSize - 1) / kSpawnGroupSize, 1, 1);
    }

    // ── UAV barrier between spawn and update ────────────────────────────
    {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_aliveCount;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Update Pass ──────────────────────────────────────────────────────
    {
        VFXUpdateCB cb{};
        std::memcpy(cb.gravity, config.gravity, sizeof(float) * 3);
        cb.dt = dt;
        cb.damping = config.damping;
        cb.aliveCount = m_maxParticles; // Use maxParticles as upper bound
        cb.frameNumber = frameNumber;
        cb.maxParticles = m_maxParticles;

        // Read-set SRV indices
        cb.readPositionsSrv = m_device->GetBufferSRVIndex(m_particleBuffers[0][read]);
        cb.readVelocitiesSrv = m_device->GetBufferSRVIndex(m_particleBuffers[1][read]);
        cb.readAgesSrv = m_device->GetBufferSRVIndex(m_particleBuffers[2][read]);
        cb.readLifespansSrv = m_device->GetBufferSRVIndex(m_particleBuffers[3][read]);
        cb.readSizesSrv = m_device->GetBufferSRVIndex(m_particleBuffers[4][read]);
        cb.readColorsSrv = m_device->GetBufferSRVIndex(m_particleBuffers[5][read]);
        cb.readRotationsSrv = m_device->GetBufferSRVIndex(m_particleBuffers[6][read]);

        // Write-set UAV indices
        cb.writePositionsUav = m_device->GetBufferUAVIndex(m_particleBuffers[0][write]);
        cb.writeVelocitiesUav = m_device->GetBufferUAVIndex(m_particleBuffers[1][write]);
        cb.writeAgesUav = m_device->GetBufferUAVIndex(m_particleBuffers[2][write]);
        cb.writeLifespansUav = m_device->GetBufferUAVIndex(m_particleBuffers[3][write]);
        cb.writeSizesUav = m_device->GetBufferUAVIndex(m_particleBuffers[4][write]);
        cb.writeColorsUav = m_device->GetBufferUAVIndex(m_particleBuffers[5][write]);
        cb.writeRotationsUav = m_device->GetBufferUAVIndex(m_particleBuffers[6][write]);

        // Atomic buffer UAV indices
        cb.aliveCountUavIndex = m_device->GetBufferUAVIndex(m_aliveCount);
        cb.freeListUavIndex = m_device->GetBufferUAVIndex(m_freeList);
        cb.freeListHeadUavIndex = m_device->GetBufferUAVIndex(m_freeListHead);

        auto alloc = cbRing->Allocate(sizeof(VFXUpdateCB));
        std::memcpy(alloc.cpuPtr, &cb, sizeof(VFXUpdateCB));

        cmdList->SetPipeline(m_updatePSO);
        cmdList->SetRootCBV(1, alloc.gpuAddr); // b1 = root param 1 (CBV)
        cmdList->Dispatch((m_maxParticles + kUpdateGroupSize - 1) / kUpdateGroupSize, 1, 1);
    }

    // ── UAV barrier before indirect args ─────────────────────────────────
    {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_aliveCount;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Indirect Args Pass ──────────────────────────────────────────────
    {
        // The indirect args CS reads alive count and writes draw args
        cmdList->SetPipeline(m_indirectArgsPSO);
        // Root constant: alive count UAV index + indirect args UAV index
        uint32_t rootData[2] = {
            m_device->GetBufferUAVIndex(m_aliveCount),
            m_device->GetBufferUAVIndex(m_indirectArgs)
        };
        cmdList->SetRootConstants(0, rootData, 2);
        cmdList->Dispatch(1, 1, 1);
    }

    // ── Transition indirect args to IndirectArgument state ──────────────
    {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_indirectArgs;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter = RHIResourceState::Common; // IndirectArgument
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Transition write-set to SRV for billboard rendering ─────────────
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        RHIBarrierDesc barrier{};
        barrier.buffer = m_particleBuffers[ch][write];
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter = RHIResourceState::Common;
        cmdList->ResourceBarrier(&barrier, 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Blend Mode PSO Selection
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle VFXParticleSystem::GetBillboardPSOForBlendMode(uint32_t mode) const {
    switch (mode) {
        case 0:  return m_billboardPSO_AlphaBlend;   // SrcAlpha / InvSrcAlpha
        case 1:  return m_billboardPSO_Additive;     // One / One
        case 2:  // PremulAlpha (default) — fall through
        default: return m_billboardPSO;              // One / InvSrcAlpha
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw Billboards
// ─────────────────────────────────────────────────────────────────────────────

void VFXParticleSystem::DrawBillboards(IRHICommandList* cmdList, CBRingAllocator* cbRing,
                                        uint64_t perFrameCBGpuAddr,
                                        const Mat4& viewMat, const Vec3& emitterPos,
                                        const Mat4& prevViewProj,
                                        const VFXEmitterConfig& config,
                                        uint32_t blendMode,
                                        uint32_t depthBufferSrvIdx) {
    const int write = 1 - m_pingPong; // Freshly written data is in the write set

    // b0: Unified billboard root constants (VS buffer indices + PS texture indices)
    VFXBillboardIndicesCB indices{};
    // VS indices
    indices.positionBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[0][write]);
    indices.sizeBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[4][write]);
    indices.colorBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[5][write]);
    indices.rotationBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[6][write]);
    indices.sortedIndicesSrvIndex = m_device->GetBufferSRVIndex(m_sortIndices);
    indices.velocityBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[1][write]);
    indices.agesBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[2][write]);
    indices.lifespansBufferIdx = m_device->GetBufferSRVIndex(m_particleBuffers[3][write]);
    // PS indices — no sprite texture, always procedural circles
    indices.spriteTextureIdx = UINT32_MAX;
    indices.depthBufferIdx = depthBufferSrvIdx;

    cmdList->SetPipeline(GetBillboardPSOForBlendMode(blendMode));
    cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&indices),
                              sizeof(indices) / sizeof(uint32_t));

    // b1: PerFrameCB
    cmdList->SetRootCBV(1, perFrameCBGpuAddr);

    // b2: Unified VFXBillboardCB (VS emitter params + PS rendering params)
    VFXBillboardCB billboardCB{};
    // VS data
    billboardCB.invView = viewMat.Inverse();
    billboardCB.emitterPosition[0] = emitterPos.x;
    billboardCB.emitterPosition[1] = emitterPos.y;
    billboardCB.emitterPosition[2] = emitterPos.z;
    billboardCB.globalSizeMultiplier = 1.0f;
    billboardCB.globalColorTint[0] = 1.0f;
    billboardCB.globalColorTint[1] = 1.0f;
    billboardCB.globalColorTint[2] = 1.0f;
    billboardCB.globalColorTint[3] = 1.0f;
    billboardCB.prevViewProj = prevViewProj;
    // PS data — rendering parameters from emitter config
    billboardCB.softFadeDistance   = config.softFadeDistance;
    billboardCB.alphaErosion      = 0.0f;
    billboardCB.emissiveIntensity = config.emissiveIntensity;
    // Size over lifetime curve parameters
    billboardCB.sizeOverLifeStart    = config.sizeOverLifeStart;
    billboardCB.sizeOverLifePeak     = config.sizeOverLifePeak;
    billboardCB.sizeOverLifeEnd      = config.sizeOverLifeEnd;
    billboardCB.sizeOverLifePeakTime = config.sizeOverLifePeakTime;
    // Color over lifetime parameters
    billboardCB.colorLifeStart[0] = config.colorLifeStartR;
    billboardCB.colorLifeStart[1] = config.colorLifeStartG;
    billboardCB.colorLifeStart[2] = config.colorLifeStartB;
    billboardCB.colorLifeStart[3] = config.colorLifeStartA;
    billboardCB.colorLifeEnd[0]   = config.colorLifeEndR;
    billboardCB.colorLifeEnd[1]   = config.colorLifeEndG;
    billboardCB.colorLifeEnd[2]   = config.colorLifeEndB;
    billboardCB.colorLifeEnd[3]   = config.colorLifeEndA;
    // Billboard alignment and stretching
    billboardCB.alignmentMode = config.alignmentMode;
    billboardCB.stretchFactor = config.stretchFactor;
    // Blend mode hint tells the PS how to handle alpha output
    billboardCB.blendModeHint = blendMode;

    auto cbAlloc = cbRing->Allocate(sizeof(VFXBillboardCB));
    std::memcpy(cbAlloc.cpuPtr, &billboardCB, sizeof(VFXBillboardCB));
    cmdList->SetRootCBV(2, cbAlloc.gpuAddr);

    // Draw all particle slots — the billboard VS reads particle attributes
    // and outputs degenerate triangles for dead particles (age >= lifespan).
    // 6 vertices per particle (2 triangles per billboard quad).
    cmdList->Draw(m_maxParticles * 6, 1, 0, 0);
}

} // namespace uz
