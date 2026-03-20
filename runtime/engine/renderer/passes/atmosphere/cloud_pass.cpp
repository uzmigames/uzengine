/// UzEngine Cloud Pass — Implementation.
///
/// Creates noise textures, weather map, and cloud output textures, compiles
/// 5 compute pipelines, and dispatches the full volumetric cloud pipeline
/// each frame. The cloud output (half-res RGBA16F) is composited by a
/// subsequent pass before tonemapping.
///
/// @ref CloudGlobalShaderParams
/// @ref MainCommon tracing loop
/// @ref Temporal reconstruction

#include "cloud_pass.h"

#include "engine/rhi/rhi.h"
#include "engine/renderer/core/cb_ring_allocator.h"
#include "shader_manager.h"
#include "engine_shaders.h"
#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Root constant / CBV structures — must match HLSL cbuffer layouts exactly
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for cloud_noise_shape_cs.hlsl and cloud_noise_detail_cs.hlsl.
/// Matches `cbuffer RootConstants : register(b0)` — 4 DWORDs.
struct CloudNoiseRootConstants {
    uint32_t texDimX;
    uint32_t texDimY;
    uint32_t texDimZ;
    uint32_t _pad0;
};

static_assert(sizeof(CloudNoiseRootConstants) == 16,
    "CloudNoiseRootConstants must be 4 DWORDs (16 bytes)");

/// Root constants for cloud_weather_cs.hlsl.
/// Matches `cbuffer WeatherParams : register(b0)` — 4 DWORDs.
struct CloudWeatherRootConstants {
    float    windOffsetX;
    float    windOffsetY;
    float    globalCoverage;
    float    _pad0;
};

static_assert(sizeof(CloudWeatherRootConstants) == 16,
    "CloudWeatherRootConstants must be 4 DWORDs (16 bytes)");

/// CBV data for cloud_raymarch_cs.hlsl at register(b2).
/// Matches `cbuffer CloudRaymarchCB : register(b2)` — 256-byte aligned for D3D12.
///
/// HLSL packing: float3+float packs into one 16-byte row. float4x4 = 64 bytes.
/// Total layout:
///   invViewProj       : 64 bytes (rows 0-3)
///   cameraPos + time  : 16 bytes (row 4)
///   sunDir + layerBot : 16 bytes (row 5)
///   sunColor + layerH : 16 bytes (row 6)
///   windVel + coverage: 16 bytes (row 7)
///   density, ext, sca, phaseG : 16 bytes (row 8)
///   phaseG2, phaseBlend, detailWS, ambient : 16 bytes (row 9)
///   priSteps, lightSteps, shapeSrv, detailSrv : 16 bytes (row 10)
///   weatherSrv, depthSrv, outW, outH : 16 bytes (row 11)
///   ambientColor + pad : 16 bytes (row 12)
///   = 208 bytes data, padded to 256 for CBV alignment
struct CloudRaymarchCBV {
    // Row 0-3: invViewProj (float4x4, 64 bytes)
    float invViewProj[16];

    // Row 4: cameraPos (float3) + time (float)
    float cameraPos[3];
    float time;

    // Row 5: sunDir (float3) + layerBottomKm (float)
    float sunDir[3];
    float layerBottomKm;

    // Row 6: sunColor (float3) + layerHeightKm (float)
    float sunColor[3];
    float layerHeightKm;

    // Row 7: windVelocity (float3) + coverage (float)
    float windVelocity[3];
    float coverage;

    // Row 8: density, extinction, scatteringAlbedo, phaseG
    float density;
    float extinction;
    float scatteringAlbedo;
    float phaseG;

    // Row 9: phaseG2, phaseBlend, detailWindScale, ambientMultiplier
    float phaseG2;
    float phaseBlend;
    float detailWindScale;
    float ambientMultiplier;

    // Row 10: primarySteps, lightSteps, shapeNoiseSrv, detailNoiseSrv
    uint32_t primarySteps;
    uint32_t lightSteps;
    uint32_t shapeNoiseSrv;
    uint32_t detailNoiseSrv;

    // Row 11: weatherMapSrv, depthSrv, outputWidth, outputHeight
    uint32_t weatherMapSrv;
    uint32_t depthSrv;
    float    outputWidth;
    float    outputHeight;

    // Row 12: ambientColor (float3) + pad
    float ambientColor[3];
    float _pad0;

    // Padding to 256 bytes: 208 bytes data, need 48 more bytes
    float _pad1[12];
};

static_assert(sizeof(CloudRaymarchCBV) == 256,
    "CloudRaymarchCBV must be 256 bytes (D3D12 CB alignment)");

/// CBV data for cloud_temporal_cs.hlsl at register(b2).
/// Matches `cbuffer CloudTemporalCB : register(b2)` — 256-byte aligned.
///
/// Layout:
///   prevViewProj      : 64 bytes (rows 0-3)
///   invViewProj       : 64 bytes (rows 4-7)
///   currentSrv, historySrv, depthSrv, blendFactor : 16 bytes (row 8)
///   outputWidth, outputHeight, pad2               : 16 bytes (row 9)
///   = 160 bytes data, padded to 256
struct CloudTemporalCBV {
    // Rows 0-3: prevViewProj
    float prevViewProj[16];

    // Rows 4-7: invViewProj
    float invViewProj[16];

    // Row 8: uint3 + float
    uint32_t currentSrvIndex;
    uint32_t historySrvIndex;
    uint32_t depthSrvIndex;
    float    blendFactor;

    // Row 9: float2 + float2 pad
    float    outputWidth;
    float    outputHeight;
    float    _pad[2];

    // Padding to 256 bytes: 160 bytes data, need 96 more
    float    _pad2[24];
};

static_assert(sizeof(CloudTemporalCBV) == 256,
    "CloudTemporalCBV must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

CloudPass::~CloudPass() {
    if (m_initialized) {
        Shutdown();
    }
}

bool CloudPass::Init(IRHIDevice* device, ShaderManager* shaderMgr,
                     uint32_t displayWidth, uint32_t displayHeight)
{
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "CloudPass::Init called while already initialized");
        return true;
    }

    if (!device) {
        UZ_LOG(Renderer, Error, "CloudPass::Init failed: device is null");
        return false;
    }
    if (!shaderMgr) {
        UZ_LOG(Renderer, Error, "CloudPass::Init failed: shaderMgr is null");
        return false;
    }

    m_device    = device;
    m_shaderMgr = shaderMgr;

    // Create noise textures (resolution-independent)
    if (!CreateNoiseTextures()) {
        Shutdown();
        return false;
    }

    // Create weather map (resolution-independent, 512x512)
    if (!CreateWeatherMapTexture()) {
        Shutdown();
        return false;
    }

    // Create half-res cloud output ping-pong textures
    const uint32_t halfW = std::max(displayWidth  / 2, 1u);
    const uint32_t halfH = std::max(displayHeight / 2, 1u);
    if (!CreateCloudOutputTextures(halfW, halfH)) {
        Shutdown();
        return false;
    }

    m_initialized = true;
    UZ_LOG(Renderer, Info,
           "CloudPass initialized (display={}x{}, half={}x{}, shapeNoiseSRV={}, "
           "detailNoiseSRV={}, weatherSRV={})",
           displayWidth, displayHeight, halfW, halfH,
           m_shapeNoiseSrv, m_detailNoiseSrv, m_weatherMapSrv);
    return true;
}

void CloudPass::Shutdown() {
    DestroyCloudOutputTextures();
    DestroyWeatherMapTexture();
    DestroyNoiseTextures();

    if (m_device) {
        // Destroy pipelines
        if (m_shapeNoisePipeline.IsValid())  m_device->DestroyPipeline(m_shapeNoisePipeline);
        if (m_detailNoisePipeline.IsValid()) m_device->DestroyPipeline(m_detailNoisePipeline);
        if (m_weatherMapPipeline.IsValid())  m_device->DestroyPipeline(m_weatherMapPipeline);
        if (m_raymarchPipeline.IsValid())    m_device->DestroyPipeline(m_raymarchPipeline);
        if (m_temporalPipeline.IsValid())    m_device->DestroyPipeline(m_temporalPipeline);

        // Destroy shaders
        if (m_shapeNoiseCS.IsValid())  m_device->DestroyShader(m_shapeNoiseCS);
        if (m_detailNoiseCS.IsValid()) m_device->DestroyShader(m_detailNoiseCS);
        if (m_weatherMapCS.IsValid())  m_device->DestroyShader(m_weatherMapCS);
        if (m_raymarchCS.IsValid())    m_device->DestroyShader(m_raymarchCS);
        if (m_temporalCS.IsValid())    m_device->DestroyShader(m_temporalCS);
    }

    m_shapeNoisePipeline  = RHIPipelineHandle::Invalid();
    m_detailNoisePipeline = RHIPipelineHandle::Invalid();
    m_weatherMapPipeline  = RHIPipelineHandle::Invalid();
    m_raymarchPipeline    = RHIPipelineHandle::Invalid();
    m_temporalPipeline    = RHIPipelineHandle::Invalid();

    m_shapeNoiseCS  = RHIShaderHandle::Invalid();
    m_detailNoiseCS = RHIShaderHandle::Invalid();
    m_weatherMapCS  = RHIShaderHandle::Invalid();
    m_raymarchCS    = RHIShaderHandle::Invalid();
    m_temporalCS    = RHIShaderHandle::Invalid();

    m_device    = nullptr;
    m_shaderMgr = nullptr;
    m_initialized = false;
    m_noisesBaked = false;
    m_initialCloudBarriersDone  = false;
    m_initialWeatherBarrierDone = false;
    m_frameCount = 0;
    m_cloudHistoryIndex = 0;

    UZ_LOG(Renderer, Info, "CloudPass shut down");
}

void CloudPass::OnResize(uint32_t displayWidth, uint32_t displayHeight) {
    const uint32_t halfW = std::max(displayWidth  / 2, 1u);
    const uint32_t halfH = std::max(displayHeight / 2, 1u);

    if (halfW == m_halfWidth && halfH == m_halfHeight) {
        return;
    }

    DestroyCloudOutputTextures();
    CreateCloudOutputTextures(halfW, halfH);

    // Reset temporal history on resize — history is invalid at new resolution
    m_cloudHistoryIndex = 0;
    m_initialCloudBarriersDone = false;
    m_frameCount = 0;

    UZ_LOG(Renderer, Info, "CloudPass resized to half={}x{}", halfW, halfH);
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Management
// ─────────────────────────────────────────────────────────────────────────────

bool CloudPass::CreateNoiseTextures() {
    // ── Shape noise: 128^3 RGBA8 3D texture ──
    {
        RHITextureDesc desc{};
        desc.width            = kShapeNoiseSize;
        desc.height           = kShapeNoiseSize;
        desc.depthOrArraySize = static_cast<uint16_t>(kShapeNoiseSize);
        desc.format           = RHIFormat::RGBA8Unorm;
        desc.usage            = RHITextureUsage::ShaderResource
                              | RHITextureUsage::UnorderedAccess;
        desc.is3D             = true;
        desc.debugName        = "Cloud_ShapeNoise_128";

        m_shapeNoise = m_device->CreateTexture(desc);
        if (!m_shapeNoise.IsValid()) {
            UZ_LOG(Renderer, Error, "CloudPass: failed to create shape noise 3D texture (128^3)");
            return false;
        }

        m_shapeNoiseSrv = m_device->GetTextureSRVIndex(m_shapeNoise);
        m_shapeNoiseUav = m_device->GetTextureUAVIndex(m_shapeNoise);

        if (m_shapeNoiseSrv == UINT32_MAX || m_shapeNoiseUav == UINT32_MAX) {
            UZ_LOG(Renderer, Error,
                   "CloudPass: shape noise descriptor creation failed (SRV={}, UAV={})",
                   m_shapeNoiseSrv, m_shapeNoiseUav);
            return false;
        }

        UZ_LOG(Renderer, Debug, "CloudPass: shape noise 3D — SRV={}, UAV={}",
               m_shapeNoiseSrv, m_shapeNoiseUav);
    }

    // ── Detail noise: 32^3 RGBA8 3D texture ──
    {
        RHITextureDesc desc{};
        desc.width            = kDetailNoiseSize;
        desc.height           = kDetailNoiseSize;
        desc.depthOrArraySize = static_cast<uint16_t>(kDetailNoiseSize);
        desc.format           = RHIFormat::RGBA8Unorm;
        desc.usage            = RHITextureUsage::ShaderResource
                              | RHITextureUsage::UnorderedAccess;
        desc.is3D             = true;
        desc.debugName        = "Cloud_DetailNoise_32";

        m_detailNoise = m_device->CreateTexture(desc);
        if (!m_detailNoise.IsValid()) {
            UZ_LOG(Renderer, Error, "CloudPass: failed to create detail noise 3D texture (32^3)");
            return false;
        }

        m_detailNoiseSrv = m_device->GetTextureSRVIndex(m_detailNoise);
        m_detailNoiseUav = m_device->GetTextureUAVIndex(m_detailNoise);

        if (m_detailNoiseSrv == UINT32_MAX || m_detailNoiseUav == UINT32_MAX) {
            UZ_LOG(Renderer, Error,
                   "CloudPass: detail noise descriptor creation failed (SRV={}, UAV={})",
                   m_detailNoiseSrv, m_detailNoiseUav);
            return false;
        }

        UZ_LOG(Renderer, Debug, "CloudPass: detail noise 3D — SRV={}, UAV={}",
               m_detailNoiseSrv, m_detailNoiseUav);
    }

    return true;
}

bool CloudPass::CreateWeatherMapTexture() {
    RHITextureDesc desc{};
    desc.width     = kWeatherMapSize;
    desc.height    = kWeatherMapSize;
    desc.format    = RHIFormat::RGBA8Unorm;
    desc.usage     = RHITextureUsage::ShaderResource
                   | RHITextureUsage::UnorderedAccess;
    desc.debugName = "Cloud_WeatherMap_512";

    m_weatherMap = m_device->CreateTexture(desc);
    if (!m_weatherMap.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create weather map texture (512x512)");
        return false;
    }

    m_weatherMapSrv = m_device->GetTextureSRVIndex(m_weatherMap);
    m_weatherMapUav = m_device->GetTextureUAVIndex(m_weatherMap);

    if (m_weatherMapSrv == UINT32_MAX || m_weatherMapUav == UINT32_MAX) {
        UZ_LOG(Renderer, Error,
               "CloudPass: weather map descriptor creation failed (SRV={}, UAV={})",
               m_weatherMapSrv, m_weatherMapUav);
        return false;
    }

    UZ_LOG(Renderer, Debug, "CloudPass: weather map — SRV={}, UAV={}",
           m_weatherMapSrv, m_weatherMapUav);
    return true;
}

bool CloudPass::CreateCloudOutputTextures(uint32_t halfW, uint32_t halfH) {
    m_halfWidth  = halfW;
    m_halfHeight = halfH;

    static const char* kNames[2] = { "Cloud_Output_0", "Cloud_Output_1" };

    for (uint32_t i = 0; i < 2; ++i) {
        RHITextureDesc desc{};
        desc.width     = halfW;
        desc.height    = halfH;
        desc.format    = RHIFormat::RGBA16Float;
        desc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess;
        desc.debugName = kNames[i];

        m_cloudOutput[i] = m_device->CreateTexture(desc);
        if (!m_cloudOutput[i].IsValid()) {
            UZ_LOG(Renderer, Error, "CloudPass: failed to create cloud output texture {} ({}x{})",
                   i, halfW, halfH);
            return false;
        }

        m_cloudOutputSrv[i] = m_device->GetTextureSRVIndex(m_cloudOutput[i]);
        m_cloudOutputUav[i] = m_device->GetTextureUAVIndex(m_cloudOutput[i]);

        if (m_cloudOutputSrv[i] == UINT32_MAX || m_cloudOutputUav[i] == UINT32_MAX) {
            UZ_LOG(Renderer, Error,
                   "CloudPass: cloud output {} descriptor creation failed (SRV={}, UAV={})",
                   i, m_cloudOutputSrv[i], m_cloudOutputUav[i]);
            return false;
        }

        UZ_LOG(Renderer, Debug, "CloudPass: cloud output {} ({}x{}) — SRV={}, UAV={}",
               i, halfW, halfH, m_cloudOutputSrv[i], m_cloudOutputUav[i]);
    }

    return true;
}

void CloudPass::DestroyNoiseTextures() {
    if (!m_device) return;

    if (m_shapeNoise.IsValid()) {
        m_device->DestroyTexture(m_shapeNoise);
        m_shapeNoise = RHITextureHandle{};
    }
    m_shapeNoiseSrv = UINT32_MAX;
    m_shapeNoiseUav = UINT32_MAX;

    if (m_detailNoise.IsValid()) {
        m_device->DestroyTexture(m_detailNoise);
        m_detailNoise = RHITextureHandle{};
    }
    m_detailNoiseSrv = UINT32_MAX;
    m_detailNoiseUav = UINT32_MAX;

    m_noisesBaked = false;
}

void CloudPass::DestroyWeatherMapTexture() {
    if (!m_device) return;

    if (m_weatherMap.IsValid()) {
        m_device->DestroyTexture(m_weatherMap);
        m_weatherMap = RHITextureHandle{};
    }
    m_weatherMapSrv = UINT32_MAX;
    m_weatherMapUav = UINT32_MAX;
    m_initialWeatherBarrierDone = false;
}

void CloudPass::DestroyCloudOutputTextures() {
    if (!m_device) return;

    for (uint32_t i = 0; i < 2; ++i) {
        if (m_cloudOutput[i].IsValid()) {
            m_device->DestroyTexture(m_cloudOutput[i]);
            m_cloudOutput[i] = RHITextureHandle{};
        }
        m_cloudOutputSrv[i] = UINT32_MAX;
        m_cloudOutputUav[i] = UINT32_MAX;
    }
    m_halfWidth  = 0;
    m_halfHeight = 0;
    m_initialCloudBarriersDone = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation (lazy)
// ─────────────────────────────────────────────────────────────────────────────

bool CloudPass::EnsureShapeNoisePipeline() {
    if (m_shapeNoisePipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::CloudNoiseShapeCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to compile cloud_noise_shape_cs.hlsl");
        return false;
    }

    RHIShaderDesc sd{};
    sd.bytecode     = bytecode.data();
    sd.bytecodeSize = bytecode.size();
    sd.stage        = RHIShaderStage::Compute;
    sd.entryPoint   = "main";
    sd.debugName    = "cloud_noise_shape_cs";

    m_shapeNoiseCS = m_device->CreateShader(sd);
    if (!m_shapeNoiseCS.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create shape noise shader");
        return false;
    }

    RHIComputePipelineDesc pd{};
    pd.computeShader = m_shapeNoiseCS;
    pd.debugName     = "CloudPass_ShapeNoise";

    m_shapeNoisePipeline = m_device->CreateComputePipeline(pd);
    if (!m_shapeNoisePipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create shape noise pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "CloudPass: shape noise pipeline created (shader={}, pipeline={})",
           m_shapeNoiseCS.index, m_shapeNoisePipeline.index);
    return true;
}

bool CloudPass::EnsureDetailNoisePipeline() {
    if (m_detailNoisePipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::CloudNoiseDetailCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to compile cloud_noise_detail_cs.hlsl");
        return false;
    }

    RHIShaderDesc sd{};
    sd.bytecode     = bytecode.data();
    sd.bytecodeSize = bytecode.size();
    sd.stage        = RHIShaderStage::Compute;
    sd.entryPoint   = "main";
    sd.debugName    = "cloud_noise_detail_cs";

    m_detailNoiseCS = m_device->CreateShader(sd);
    if (!m_detailNoiseCS.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create detail noise shader");
        return false;
    }

    RHIComputePipelineDesc pd{};
    pd.computeShader = m_detailNoiseCS;
    pd.debugName     = "CloudPass_DetailNoise";

    m_detailNoisePipeline = m_device->CreateComputePipeline(pd);
    if (!m_detailNoisePipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create detail noise pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "CloudPass: detail noise pipeline created (shader={}, pipeline={})",
           m_detailNoiseCS.index, m_detailNoisePipeline.index);
    return true;
}

bool CloudPass::EnsureWeatherMapPipeline() {
    if (m_weatherMapPipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::CloudWeatherCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to compile cloud_weather_cs.hlsl");
        return false;
    }

    RHIShaderDesc sd{};
    sd.bytecode     = bytecode.data();
    sd.bytecodeSize = bytecode.size();
    sd.stage        = RHIShaderStage::Compute;
    sd.entryPoint   = "main";
    sd.debugName    = "cloud_weather_cs";

    m_weatherMapCS = m_device->CreateShader(sd);
    if (!m_weatherMapCS.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create weather map shader");
        return false;
    }

    RHIComputePipelineDesc pd{};
    pd.computeShader = m_weatherMapCS;
    pd.debugName     = "CloudPass_WeatherMap";

    m_weatherMapPipeline = m_device->CreateComputePipeline(pd);
    if (!m_weatherMapPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create weather map pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "CloudPass: weather map pipeline created (shader={}, pipeline={})",
           m_weatherMapCS.index, m_weatherMapPipeline.index);
    return true;
}

bool CloudPass::EnsureRaymarchPipeline() {
    if (m_raymarchPipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::CloudRaymarchCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to compile cloud_raymarch_cs.hlsl");
        return false;
    }

    RHIShaderDesc sd{};
    sd.bytecode     = bytecode.data();
    sd.bytecodeSize = bytecode.size();
    sd.stage        = RHIShaderStage::Compute;
    sd.entryPoint   = "main";
    sd.debugName    = "cloud_raymarch_cs";

    m_raymarchCS = m_device->CreateShader(sd);
    if (!m_raymarchCS.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create raymarch shader");
        return false;
    }

    RHIComputePipelineDesc pd{};
    pd.computeShader = m_raymarchCS;
    pd.debugName     = "CloudPass_Raymarch";

    m_raymarchPipeline = m_device->CreateComputePipeline(pd);
    if (!m_raymarchPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create raymarch pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "CloudPass: raymarch pipeline created (shader={}, pipeline={})",
           m_raymarchCS.index, m_raymarchPipeline.index);
    return true;
}

bool CloudPass::EnsureTemporalPipeline() {
    if (m_temporalPipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::CloudTemporalCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to compile cloud_temporal_cs.hlsl");
        return false;
    }

    RHIShaderDesc sd{};
    sd.bytecode     = bytecode.data();
    sd.bytecodeSize = bytecode.size();
    sd.stage        = RHIShaderStage::Compute;
    sd.entryPoint   = "main";
    sd.debugName    = "cloud_temporal_cs";

    m_temporalCS = m_device->CreateShader(sd);
    if (!m_temporalCS.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create temporal shader");
        return false;
    }

    RHIComputePipelineDesc pd{};
    pd.computeShader = m_temporalCS;
    pd.debugName     = "CloudPass_Temporal";

    m_temporalPipeline = m_device->CreateComputePipeline(pd);
    if (!m_temporalPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create temporal pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "CloudPass: temporal pipeline created (shader={}, pipeline={})",
           m_temporalCS.index, m_temporalPipeline.index);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Noise Baking
// ─────────────────────────────────────────────────────────────────────────────

void CloudPass::BakeNoiseTextures(IRHICommandList* cmd) {
    UZ_PROFILE_SCOPE("CloudPass::BakeNoiseTextures");

    if (m_noisesBaked) {
        return;
    }

    if (!cmd) {
        UZ_LOG(Renderer, Error, "CloudPass::BakeNoiseTextures: null command list");
        return;
    }

    if (!EnsureShapeNoisePipeline() || !EnsureDetailNoisePipeline()) {
        UZ_LOG(Renderer, Error, "CloudPass::BakeNoiseTextures: pipeline compilation failed");
        return;
    }

    // ── Barrier: Common → UAV for both noise textures ──
    {
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture     = m_shapeNoise;
        barriers[0].stateBefore = RHIResourceState::Common;
        barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;
        barriers[1].texture     = m_detailNoise;
        barriers[1].stateBefore = RHIResourceState::Common;
        barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;
        cmd->ResourceBarrier(barriers, 2);
    }

    // ── Shape noise: 128^3 dispatched as [32, 32, 32] groups of [4,4,4] threads ──
    {
        UZ_PROFILE_SCOPE("CloudPass::BakeShapeNoise");

        cmd->SetPipeline(m_shapeNoisePipeline);

        CloudNoiseRootConstants rc{};
        rc.texDimX = kShapeNoiseSize;
        rc.texDimY = kShapeNoiseSize;
        rc.texDimZ = kShapeNoiseSize;

        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 4);

        // Bind UAV at slot 4 — matches compute root sig pattern:
        // slot 0 = root constants, slot 4 = UAV table
        cmd->SetDescriptorTable(4, m_shapeNoiseUav);

        const uint32_t groups = kShapeNoiseSize / k3DGroupSize; // 128/4 = 32
        cmd->Dispatch(groups, groups, groups);

        UZ_LOG(Renderer, Info, "CloudPass: dispatched shape noise bake (128^3, {}^3 groups)",
               groups);
    }

    // ── Detail noise: 32^3 dispatched as [8, 8, 8] groups of [4,4,4] threads ──
    {
        UZ_PROFILE_SCOPE("CloudPass::BakeDetailNoise");

        // Barrier between the two dispatches — shape noise done, detail starts
        RHIBarrierDesc uavBarrier{};
        uavBarrier.texture     = m_shapeNoise;
        uavBarrier.stateBefore = RHIResourceState::UnorderedAccess;
        uavBarrier.stateAfter  = RHIResourceState::UnorderedAccess;
        cmd->ResourceBarrier(&uavBarrier, 1);

        cmd->SetPipeline(m_detailNoisePipeline);

        CloudNoiseRootConstants rc{};
        rc.texDimX = kDetailNoiseSize;
        rc.texDimY = kDetailNoiseSize;
        rc.texDimZ = kDetailNoiseSize;

        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 4);
        cmd->SetDescriptorTable(4, m_detailNoiseUav);

        const uint32_t groups = kDetailNoiseSize / k3DGroupSize; // 32/4 = 8
        cmd->Dispatch(groups, groups, groups);

        UZ_LOG(Renderer, Info, "CloudPass: dispatched detail noise bake (32^3, {}^3 groups)",
               groups);
    }

    // ── Barrier: UAV → SRV for both noise textures (ready for ray march reads) ──
    {
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture     = m_shapeNoise;
        barriers[0].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[0].stateAfter  = RHIResourceState::ShaderResource;
        barriers[1].texture     = m_detailNoise;
        barriers[1].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[1].stateAfter  = RHIResourceState::ShaderResource;
        cmd->ResourceBarrier(barriers, 2);
    }

    m_noisesBaked = true;
    UZ_LOG(Renderer, Info, "CloudPass: noise textures baked successfully");
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute — per-frame cloud pipeline
// ─────────────────────────────────────────────────────────────────────────────

void CloudPass::Execute(IRHICommandList* cmd, CBRingAllocator& cbRing,
                        const CloudPassInput& input)
{
    UZ_PROFILE_SCOPE("CloudPass::Execute");

    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "CloudPass::Execute called before Init()");
        return;
    }

    if (!cmd) {
        UZ_LOG(Renderer, Error, "CloudPass::Execute: null command list");
        return;
    }

    // Cloud compute shaders require bindless (ResourceDescriptorHeap).
    // On SM5/DX11, clouds are not available.
    if (!m_device->IsBindless()) {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            UZ_LOG(Renderer, Warning,
                   "CloudPass: skipped — volumetric clouds require bindless (SM6.6+)");
            loggedOnce = true;
        }
        return;
    }

    if (m_halfWidth == 0 || m_halfHeight == 0) {
        UZ_LOG(Renderer, Error, "CloudPass::Execute: zero-size cloud output — aborting");
        return;
    }

    if (input.depthSrv == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "CloudPass::Execute: depthSrv is UINT32_MAX");
        return;
    }

    // ── Step 0: Bake noise textures if needed (one-time) ──
    if (!m_noisesBaked) {
        BakeNoiseTextures(cmd);
        if (!m_noisesBaked) {
            UZ_LOG(Renderer, Error, "CloudPass::Execute: noise bake failed, skipping clouds");
            return;
        }
    }

    // Ensure all per-frame pipelines are compiled
    if (!EnsureWeatherMapPipeline() || !EnsureRaymarchPipeline() || !EnsureTemporalPipeline()) {
        UZ_LOG(Renderer, Error, "CloudPass::Execute: pipeline compilation failed");
        return;
    }

    // First-frame diagnostic dump
    static bool loggedFirstFrame = false;
    if (!loggedFirstFrame) {
        UZ_LOG(Renderer, Info,
               "CloudPass: first execution — half={}x{}, depthSRV={}, shapeSRV={}, "
               "detailSRV={}, weatherSRV={}, steps={}/{}, coverage={:.2f}",
               m_halfWidth, m_halfHeight, input.depthSrv,
               m_shapeNoiseSrv, m_detailNoiseSrv, m_weatherMapSrv,
               input.primarySteps, input.lightSteps, input.coverage);
        loggedFirstFrame = true;
    }

    // Determine ping-pong indices:
    // currentIdx  = where this frame's raymarch writes
    // historyIdx  = where the previous frame's result lives
    const uint32_t currentIdx = m_cloudHistoryIndex;
    const uint32_t historyIdx = 1 - m_cloudHistoryIndex;

    // ══════════════════════════════════════════════════════════════════════
    // STEP 1: Update Weather Map
    // ══════════════════════════════════════════════════════════════════════
    {
        UZ_PROFILE_SCOPE("CloudPass::WeatherMap");

        // Initial barrier: Common → UAV on first frame
        if (!m_initialWeatherBarrierDone) {
            RHIBarrierDesc barrier{};
            barrier.texture     = m_weatherMap;
            barrier.stateBefore = RHIResourceState::Common;
            barrier.stateAfter  = RHIResourceState::UnorderedAccess;
            cmd->ResourceBarrier(&barrier, 1);
            m_initialWeatherBarrierDone = true;
        } else {
            // Subsequent frames: SRV → UAV (was left in SRV from last frame's raymarch read)
            RHIBarrierDesc barrier{};
            barrier.texture     = m_weatherMap;
            barrier.stateBefore = RHIResourceState::ShaderResource;
            barrier.stateAfter  = RHIResourceState::UnorderedAccess;
            cmd->ResourceBarrier(&barrier, 1);
        }

        cmd->SetPipeline(m_weatherMapPipeline);

        // Wind offset = time * windVelocity.xy (accumulated UV drift)
        CloudWeatherRootConstants rc{};
        rc.windOffsetX     = input.time * input.windVelocity[0];
        rc.windOffsetY     = input.time * input.windVelocity[1];
        rc.globalCoverage  = input.globalCoverage;

        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 4);
        cmd->SetDescriptorTable(4, m_weatherMapUav);

        // 512 / 8 = 64 groups per dimension
        const uint32_t groups = kWeatherMapSize / kGroupSize;
        cmd->Dispatch(groups, groups, 1);
    }

    // ── Barrier: weather map UAV → SRV ──
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_weatherMap;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // ══════════════════════════════════════════════════════════════════════
    // STEP 2: Ray March Clouds (half-res)
    // ══════════════════════════════════════════════════════════════════════
    {
        UZ_PROFILE_SCOPE("CloudPass::Raymarch");

        // Initial barrier: Common → UAV for cloud output on first frame
        if (!m_initialCloudBarriersDone) {
            RHIBarrierDesc barriers[2] = {};
            barriers[0].texture     = m_cloudOutput[0];
            barriers[0].stateBefore = RHIResourceState::Common;
            barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;
            barriers[1].texture     = m_cloudOutput[1];
            barriers[1].stateBefore = RHIResourceState::Common;
            barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;
            cmd->ResourceBarrier(barriers, 2);
            m_initialCloudBarriersDone = true;
        } else {
            // Transition current output from SRV → UAV (was SRV from previous frame's
            // temporal read, or the initial UAV if this is the first real dispatch)
            RHIBarrierDesc barrier{};
            barrier.texture     = m_cloudOutput[currentIdx];
            barrier.stateBefore = RHIResourceState::ShaderResource;
            barrier.stateAfter  = RHIResourceState::UnorderedAccess;
            cmd->ResourceBarrier(&barrier, 1);
        }

        cmd->SetPipeline(m_raymarchPipeline);

        // Fill the CBV at register(b2) via CBRingAllocator
        CloudRaymarchCBV cb{};
        std::memcpy(cb.invViewProj,   input.invViewProj,   sizeof(cb.invViewProj));
        std::memcpy(cb.cameraPos,     input.cameraPos,     sizeof(cb.cameraPos));
        cb.time = input.time;
        std::memcpy(cb.sunDir,        input.sunDir,        sizeof(cb.sunDir));
        cb.layerBottomKm = input.layerBottomKm;
        std::memcpy(cb.sunColor,      input.sunColor,      sizeof(cb.sunColor));
        cb.layerHeightKm = input.layerHeightKm;
        std::memcpy(cb.windVelocity,  input.windVelocity,  sizeof(cb.windVelocity));
        cb.coverage          = input.coverage;
        cb.density           = input.density;
        cb.extinction        = input.extinction;
        cb.scatteringAlbedo  = input.scatteringAlbedo;
        cb.phaseG            = input.phaseG;
        cb.phaseG2           = input.phaseG2;
        cb.phaseBlend        = input.phaseBlend;
        cb.detailWindScale   = input.detailWindScale;
        cb.ambientMultiplier = input.ambientMultiplier;
        cb.primarySteps      = input.primarySteps;
        cb.lightSteps        = input.lightSteps;
        cb.shapeNoiseSrv     = m_shapeNoiseSrv;
        cb.detailNoiseSrv    = m_detailNoiseSrv;
        cb.weatherMapSrv     = m_weatherMapSrv;
        cb.depthSrv          = input.depthSrv;
        cb.outputWidth       = static_cast<float>(m_halfWidth);
        cb.outputHeight      = static_cast<float>(m_halfHeight);
        std::memcpy(cb.ambientColor,  input.ambientColor,  sizeof(cb.ambientColor));

        auto alloc = cbRing.Allocate(sizeof(CloudRaymarchCBV));
        if (!alloc.cpuPtr) {
            UZ_LOG(Renderer, Error, "CloudPass: CB ring allocation failed for raymarch CBV");
            return;
        }
        std::memcpy(alloc.cpuPtr, &cb, sizeof(CloudRaymarchCBV));

        // Bind CBV at root param 2 (register b2) via cross-backend SetRootCBV
        cmd->SetRootCBV(2, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr,
                        sizeof(CloudRaymarchCBV));

        // Bind UAV at slot 4 — cloud output (current frame)
        cmd->SetDescriptorTable(4, m_cloudOutputUav[currentIdx]);

        const uint32_t groupsX = (m_halfWidth  + kGroupSize - 1) / kGroupSize;
        const uint32_t groupsY = (m_halfHeight + kGroupSize - 1) / kGroupSize;
        cmd->Dispatch(groupsX, groupsY, 1);
    }

    // ── Barrier: current cloud output UAV → SRV (for temporal read) ──
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_cloudOutput[currentIdx];
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // ══════════════════════════════════════════════════════════════════════
    // STEP 3: Temporal Filter
    // ══════════════════════════════════════════════════════════════════════
    //
    // Reads:  current frame cloud (SRV) + history cloud (SRV) + depth (SRV)
    // Writes: history slot (UAV) — overwrites history with blended result
    //
    // On the first frame (m_frameCount == 0), the history buffer is uninitialized.
    // The temporal shader handles this via blendFactor = 1.0 (use current only).
    {
        UZ_PROFILE_SCOPE("CloudPass::TemporalFilter");

        // Transition history output to UAV for temporal write
        // On first frame after init, it's already in UAV from the initial barrier.
        // On subsequent frames, it was left in SRV from last frame's temporal output read.
        if (m_frameCount > 0) {
            RHIBarrierDesc barrier{};
            barrier.texture     = m_cloudOutput[historyIdx];
            barrier.stateBefore = RHIResourceState::ShaderResource;
            barrier.stateAfter  = RHIResourceState::UnorderedAccess;
            cmd->ResourceBarrier(&barrier, 1);
        }

        cmd->SetPipeline(m_temporalPipeline);

        CloudTemporalCBV cb{};
        std::memcpy(cb.prevViewProj, input.prevViewProj, sizeof(cb.prevViewProj));
        std::memcpy(cb.invViewProj,  input.invViewProj,  sizeof(cb.invViewProj));
        cb.currentSrvIndex = m_cloudOutputSrv[currentIdx];
        cb.historySrvIndex = m_cloudOutputSrv[historyIdx];
        cb.depthSrvIndex   = input.depthSrv;
        // On first frame, use blend factor 1.0 (current only, no history to blend with)
        cb.blendFactor     = (m_frameCount == 0) ? 1.0f : input.temporalBlendFactor;
        cb.outputWidth     = static_cast<float>(m_halfWidth);
        cb.outputHeight    = static_cast<float>(m_halfHeight);

        auto alloc = cbRing.Allocate(sizeof(CloudTemporalCBV));
        if (!alloc.cpuPtr) {
            UZ_LOG(Renderer, Error, "CloudPass: CB ring allocation failed for temporal CBV");
            return;
        }
        std::memcpy(alloc.cpuPtr, &cb, sizeof(CloudTemporalCBV));

        cmd->SetRootCBV(2, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr,
                        sizeof(CloudTemporalCBV));

        // Bind UAV at slot 4 — temporal writes into history buffer
        cmd->SetDescriptorTable(4, m_cloudOutputUav[historyIdx]);

        const uint32_t groupsX = (m_halfWidth  + kGroupSize - 1) / kGroupSize;
        const uint32_t groupsY = (m_halfHeight + kGroupSize - 1) / kGroupSize;
        cmd->Dispatch(groupsX, groupsY, 1);
    }

    // ── Barrier: temporal output (history slot) UAV → SRV ──
    // The temporal output is the final cloud result for this frame. Transition
    // to SRV so the compositing pass (or tonemap) can read it.
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_cloudOutput[historyIdx];
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // ── Flip ping-pong ──
    // After this frame:
    //   historyIdx (now SRV) = temporally filtered result (the "output" for compositing)
    //   currentIdx (now SRV) = this frame's raw raymarch result
    // Next frame: m_cloudHistoryIndex swaps, so the new "history" is this frame's
    // temporal output, and the new "current" slot gets overwritten by the new raymarch.
    m_cloudHistoryIndex = 1 - m_cloudHistoryIndex;
    m_frameCount++;
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────

uint32_t CloudPass::GetCloudOutputSrv() const noexcept {
    if (!m_initialized || m_frameCount == 0) {
        return UINT32_MAX;
    }

    // The temporal filter wrote its output into the history slot. After the
    // ping-pong flip, the history slot is at (1 - m_cloudHistoryIndex).
    // But we flipped already, so the previous history index = m_cloudHistoryIndex.
    // Wait — let's think carefully:
    //   Before flip: currentIdx = old m_cloudHistoryIndex
    //                historyIdx = 1 - old m_cloudHistoryIndex
    //   Temporal wrote into historyIdx.
    //   After flip:  m_cloudHistoryIndex = 1 - old = historyIdx
    //
    // So the temporal output is at the NEW m_cloudHistoryIndex.
    // That's the SRV we want to expose.
    return m_cloudOutputSrv[m_cloudHistoryIndex];
}

} // namespace uz
