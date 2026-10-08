/// UzEngine Environment Map Importance Sampler — implementation.
/// @ref Vose 1991 "A Linear Algorithm for Generating Random Numbers with a Given Distribution"
/// @ref Pharr & Humphreys 2004 "Physically Based Rendering" Chapter 14.6

#include "env_map_sampler.h"

#include "log.h"
#include "rhi.h"
#include "shader_manager.h"
#include "engine_shaders.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <queue>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

EnvMapSampler::~EnvMapSampler() {
    Shutdown();
}

bool EnvMapSampler::Init(IRHIDevice* device, ShaderManager* shaderMgr) {
    if (!device || !shaderMgr) return false;
    m_device = device;
    m_shaderMgr = shaderMgr;
    return true;
}

void EnvMapSampler::Shutdown() {
    DestroyPyramidTextures();

    if (m_aliasBuffer.IsValid()) {
        m_device->DestroyBuffer(m_aliasBuffer);
        m_aliasBuffer = {};
    }
    m_aliasSrvIndex = UINT32_MAX;
    m_lumMip0SrvIndex = UINT32_MAX;
    m_totalLuminance = 0.0f;
    m_ready = false;
}

// ─── Pipeline creation ──────────────────────────────────────────────────────

bool EnvMapSampler::EnsurePipelines() {
    // Mip 0 pipeline (PYRAMID_MIP0 define)
    if (!m_mip0Pipeline.IsValid()) {
        auto source = EngineShaders::GetSource(EngineShader::EnvmapLuminancePyramidCS);
        auto bytecode = m_shaderMgr->GetPipeline().LoadBytecodeFromSource(
            "envmap_luminance_pyramid_mip0_cs", source, "PYRAMID_MIP0=1");

        if (bytecode.empty()) {
            UZ_LOG(Renderer, Error, "EnvMapSampler: failed to compile mip0 shader");
            return false;
        }

        RHIShaderDesc sd;
        sd.bytecode     = bytecode.data();
        sd.bytecodeSize = bytecode.size();
        sd.stage        = RHIShaderStage::Compute;
        sd.entryPoint   = "main";
        sd.debugName    = "envmap_lum_mip0_cs";
        m_mip0CS = m_device->CreateShader(sd);
        if (!m_mip0CS.IsValid()) return false;

        RHIComputePipelineDesc pd;
        pd.computeShader = m_mip0CS;
        pd.debugName     = "EnvMap_LumPyramid_Mip0";
        m_mip0Pipeline = m_device->CreateComputePipeline(pd);
        if (!m_mip0Pipeline.IsValid()) return false;

        UZ_LOG(Renderer, Info, "EnvMapSampler: mip0 pipeline created");
    }

    // Reduce pipeline (PYRAMID_REDUCE define)
    if (!m_reducePipeline.IsValid()) {
        auto source = EngineShaders::GetSource(EngineShader::EnvmapLuminancePyramidCS);
        auto bytecode = m_shaderMgr->GetPipeline().LoadBytecodeFromSource(
            "envmap_luminance_pyramid_reduce_cs", source, "PYRAMID_REDUCE=1");

        if (bytecode.empty()) {
            UZ_LOG(Renderer, Error, "EnvMapSampler: failed to compile reduce shader");
            return false;
        }

        RHIShaderDesc sd;
        sd.bytecode     = bytecode.data();
        sd.bytecodeSize = bytecode.size();
        sd.stage        = RHIShaderStage::Compute;
        sd.entryPoint   = "main";
        sd.debugName    = "envmap_lum_reduce_cs";
        m_reduceCS = m_device->CreateShader(sd);
        if (!m_reduceCS.IsValid()) return false;

        RHIComputePipelineDesc pd;
        pd.computeShader = m_reduceCS;
        pd.debugName     = "EnvMap_LumPyramid_Reduce";
        m_reducePipeline = m_device->CreateComputePipeline(pd);
        if (!m_reducePipeline.IsValid()) return false;

        UZ_LOG(Renderer, Info, "EnvMapSampler: reduce pipeline created");
    }

    return true;
}

// ─── Pyramid texture management ─────────────────────────────────────────────

bool EnvMapSampler::CreatePyramidTextures() {
    DestroyPyramidTextures();

    // Calculate mip count: reduce until 1x1
    uint32_t w = kPyramidWidth;
    uint32_t h = kPyramidHeight;
    uint32_t mipCount = 1;
    {
        uint32_t tw = w, th = h;
        while (tw > 1 || th > 1) {
            tw = std::max(tw / 2, 1u);
            th = std::max(th / 2, 1u);
            ++mipCount;
        }
    }

    m_mipLevels.resize(mipCount);

    for (uint32_t mip = 0; mip < mipCount; ++mip) {
        uint32_t mw = std::max(w >> mip, 1u);
        uint32_t mh = std::max(h >> mip, 1u);

        RHITextureDesc texDesc;
        texDesc.width     = mw;
        texDesc.height    = mh;
        texDesc.format    = RHIFormat::R32Float;
        texDesc.usage     = RHITextureUsage::UnorderedAccess
                          | RHITextureUsage::ShaderResource
                          | RHITextureUsage::CopySrc;
        texDesc.debugName = mip == 0 ? "EnvMap_LumPyramid_Mip0"
                                     : "EnvMap_LumPyramid_MipN";

        auto tex = m_device->CreateTexture(texDesc);
        if (!tex.IsValid()) {
            UZ_LOG(Renderer, Error, "EnvMapSampler: failed to create mip {} texture ({}x{})",
                   mip, mw, mh);
            return false;
        }

        m_mipLevels[mip].texture  = tex;
        m_mipLevels[mip].srvIndex = m_device->GetTextureSRVIndex(tex);
        m_mipLevels[mip].uavIndex = m_device->GetTextureUAVIndex(tex);
        m_mipLevels[mip].width    = mw;
        m_mipLevels[mip].height   = mh;
    }

    m_lumMip0SrvIndex = m_mipLevels[0].srvIndex;

    UZ_LOG(Renderer, Info, "EnvMapSampler: created {} mip levels ({}x{} → 1x1)",
           mipCount, kPyramidWidth, kPyramidHeight);
    return true;
}

void EnvMapSampler::DestroyPyramidTextures() {
    for (auto& mip : m_mipLevels) {
        if (mip.texture.IsValid()) {
            m_device->DestroyTexture(mip.texture);
        }
    }
    m_mipLevels.clear();
    m_lumMip0SrvIndex = UINT32_MAX;
}

// ─── GPU dispatch ───────────────────────────────────────────────────────────

void EnvMapSampler::DispatchMip0(IRHICommandList* cmdList, uint32_t envCubemapSrvIndex,
                                 uint32_t colorUavIndex, uint32_t sampleMip) {
    auto& mip0 = m_mipLevels[0];

    // Transition mip 0 to UAV
    RHIBarrierDesc barrier;
    barrier.texture   = mip0.texture;
    barrier.stateBefore = RHIResourceState::Common;
    barrier.stateAfter  = RHIResourceState::UnorderedAccess;
    cmdList->ResourceBarrier(&barrier, 1);

    cmdList->SetPipeline(m_mip0Pipeline);

    // Root constants: envSrvIndex, outputUavIdx, inputSrvIdx (unused), mipWidth, mipHeight, colorUavIdx, sampleMip
    uint32_t rootConstants[7] = {
        envCubemapSrvIndex,
        mip0.uavIndex,
        0, // unused for mip 0
        mip0.width,
        mip0.height,
        colorUavIndex,
        sampleMip
    };
    cmdList->SetRootConstants(0, rootConstants, 7);
    cmdList->SetDescriptorTable(4, mip0.uavIndex);

    cmdList->Dispatch(
        (mip0.width  + kThreadGroupSize - 1) / kThreadGroupSize,
        (mip0.height + kThreadGroupSize - 1) / kThreadGroupSize,
        1);

    // UAV barrier (ensure writes are visible)
    RHIBarrierDesc uavBarrier;
    uavBarrier.texture     = mip0.texture;
    uavBarrier.stateBefore = RHIResourceState::UnorderedAccess;
    uavBarrier.stateAfter  = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&uavBarrier, 1);
}

void EnvMapSampler::DispatchReductions(IRHICommandList* cmdList) {
    cmdList->SetPipeline(m_reducePipeline);

    for (size_t mip = 1; mip < m_mipLevels.size(); ++mip) {
        auto& prev = m_mipLevels[mip - 1];
        auto& curr = m_mipLevels[mip];

        // Transition current to UAV
        RHIBarrierDesc barrier;
        barrier.texture     = curr.texture;
        barrier.stateBefore = RHIResourceState::Common;
        barrier.stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);

        // Root constants: envSrvIndex (unused), outputUavIdx, inputSrvIdx, mipWidth, mipHeight, no colour, mip 0
        uint32_t rootConstants[7] = {
            0, // unused
            curr.uavIndex,
            prev.srvIndex,
            curr.width,
            curr.height,
            UINT32_MAX,
            0u
        };
        cmdList->SetRootConstants(0, rootConstants, 7);
        cmdList->SetDescriptorTable(4, curr.uavIndex);

        cmdList->Dispatch(
            (curr.width  + kThreadGroupSize - 1) / kThreadGroupSize,
            (curr.height + kThreadGroupSize - 1) / kThreadGroupSize,
            1);

        // UAV → SRV for next mip to read
        RHIBarrierDesc toSrv;
        toSrv.texture     = curr.texture;
        toSrv.stateBefore = RHIResourceState::UnorderedAccess;
        toSrv.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&toSrv, 1);
    }
}

// ─── Alias table construction ───────────────────────────────────────────────

/// @ref Vose 1991 — O(N) alias table construction.
/// Given an array of non-negative weights, builds an alias table for O(1)
/// discrete sampling. Each entry stores a probability threshold and an alias
/// index, allowing rejection sampling with a single random number pair.
void EnvMapSampler::BuildAliasTable(const float* weights, uint32_t count,
                                     std::vector<AliasEntry>& outTable)
{
    outTable.resize(count);

    double totalWeight = 0.0;
    for (uint32_t i = 0; i < count; ++i) {
        totalWeight += weights[i];
    }

    // Normalize to [0, count) — each bin's "fair share" is 1.0
    std::vector<double> prob(count);
    double scale = (totalWeight > 0.0) ? double(count) / totalWeight : 0.0;
    for (uint32_t i = 0; i < count; ++i) {
        prob[i] = weights[i] * scale;
    }

    // Separate into "small" (< 1) and "large" (>= 1) queues
    std::queue<uint32_t> small, large;
    for (uint32_t i = 0; i < count; ++i) {
        if (prob[i] < 1.0) small.push(i);
        else                large.push(i);
    }

    // Pair small entries with large entries
    while (!small.empty() && !large.empty()) {
        uint32_t s = small.front(); small.pop();
        uint32_t l = large.front(); large.pop();

        outTable[s].prob  = static_cast<float>(prob[s]);
        outTable[s].alias = l;

        // Redistribute excess from large to cover small's deficit
        prob[l] = (prob[l] + prob[s]) - 1.0;

        if (prob[l] < 1.0) small.push(l);
        else                large.push(l);
    }

    // Remaining entries are essentially 1.0 (numerical precision)
    while (!large.empty()) {
        uint32_t l = large.front(); large.pop();
        outTable[l].prob  = 1.0f;
        outTable[l].alias = l;
    }
    while (!small.empty()) {
        uint32_t s = small.front(); small.pop();
        outTable[s].prob  = 1.0f;
        outTable[s].alias = s;
    }
}

bool EnvMapSampler::BuildAndUploadAliasTable() {
    auto& mip0  = m_mipLevels[0];
    auto& mipN  = m_mipLevels.back(); // 1x1 mip

    const uint32_t texelCount = mip0.width * mip0.height;
    const uint32_t mip0Bytes  = texelCount * sizeof(float);

    // ── Step 1: Readback mip 0 luminance to CPU ─────────────────────────

    // Transition mip 0 to CopySrc
    auto cmdList = m_device->CreateCommandList();
    cmdList->Begin();

    RHIBarrierDesc toCopySrc;
    toCopySrc.texture     = mip0.texture;
    toCopySrc.stateBefore = RHIResourceState::ShaderResource;
    toCopySrc.stateAfter  = RHIResourceState::CopySrc;
    cmdList->ResourceBarrier(&toCopySrc, 1);

    // Create readback buffer
    RHIBufferDesc readbackDesc;
    readbackDesc.sizeBytes = mip0Bytes;
    readbackDesc.usage     = RHIBufferUsage::CopyDst;
    readbackDesc.memory    = RHIMemoryType::Readback;
    readbackDesc.debugName = "EnvMapSampler_Readback_Mip0";
    auto readbackBuf = m_device->CreateBuffer(readbackDesc);

    uint32_t rowPitch = mip0.width * sizeof(float);
    // Align to 256 bytes (D3D12 texture row pitch alignment)
    uint32_t alignedRowPitch = (rowPitch + 255) & ~255u;

    cmdList->CopyTextureToBuffer(mip0.texture, 0, readbackBuf, 0, alignedRowPitch, 0);

    // Also readback 1x1 mip for total luminance
    RHIBarrierDesc toCopySrcN;
    toCopySrcN.texture     = mipN.texture;
    toCopySrcN.stateBefore = RHIResourceState::ShaderResource;
    toCopySrcN.stateAfter  = RHIResourceState::CopySrc;
    cmdList->ResourceBarrier(&toCopySrcN, 1);

    RHIBufferDesc readbackDescN;
    readbackDescN.sizeBytes = sizeof(float);
    readbackDescN.usage     = RHIBufferUsage::CopyDst;
    readbackDescN.memory    = RHIMemoryType::Readback;
    readbackDescN.debugName = "EnvMapSampler_Readback_MipN";
    auto readbackBufN = m_device->CreateBuffer(readbackDescN);

    cmdList->CopyTextureToBuffer(mipN.texture, 0, readbackBufN, 0, sizeof(float), 0);

    cmdList->End();
    cmdList->Execute();
    m_device->WaitIdle(); // CPU waits for GPU

    // Map and read
    const auto* lumData = static_cast<const float*>(m_device->MapBuffer(readbackBuf));
    std::vector<float> luminanceValues(texelCount);

    // Copy row-by-row to handle alignment padding
    for (uint32_t row = 0; row < mip0.height; ++row) {
        const auto* srcRow = reinterpret_cast<const float*>(
            reinterpret_cast<const uint8_t*>(lumData) + row * alignedRowPitch);
        std::memcpy(&luminanceValues[row * mip0.width], srcRow, mip0.width * sizeof(float));
    }
    m_device->UnmapBuffer(readbackBuf);

    // Read total luminance from 1x1 mip
    const auto* totalData = static_cast<const float*>(m_device->MapBuffer(readbackBufN));
    m_totalLuminance = *totalData;
    m_device->UnmapBuffer(readbackBufN);

    UZ_LOG(Renderer, Info, "EnvMapSampler: total luminance = {:.4f} (readback from 1x1 mip)",
           m_totalLuminance);

    // Clean up readback buffers
    m_device->DestroyBuffer(readbackBuf);
    m_device->DestroyBuffer(readbackBufN);

    // Transition mip 0 back to SRV
    auto cmdList2 = m_device->CreateCommandList();
    cmdList2->Begin();
    RHIBarrierDesc toSrv;
    toSrv.texture     = mip0.texture;
    toSrv.stateBefore = RHIResourceState::CopySrc;
    toSrv.stateAfter  = RHIResourceState::ShaderResource;
    cmdList2->ResourceBarrier(&toSrv, 1);

    RHIBarrierDesc toSrvN;
    toSrvN.texture     = mipN.texture;
    toSrvN.stateBefore = RHIResourceState::CopySrc;
    toSrvN.stateAfter  = RHIResourceState::ShaderResource;
    cmdList2->ResourceBarrier(&toSrvN, 1);

    cmdList2->End();
    cmdList2->Execute();
    m_device->WaitIdle();

    // ── Step 2: Build Vose alias table on CPU ───────────────────────────

    std::vector<AliasEntry> aliasTable;
    BuildAliasTable(luminanceValues.data(), texelCount, aliasTable);

    // ── Step 3: Upload alias table as StructuredBuffer ──────────────────

    uint32_t aliasBytes = texelCount * sizeof(AliasEntry);

    // Destroy previous if exists
    if (m_aliasBuffer.IsValid()) {
        m_device->DestroyBuffer(m_aliasBuffer);
    }

    RHIBufferDesc bufDesc;
    bufDesc.sizeBytes = aliasBytes;
    bufDesc.stride    = sizeof(AliasEntry);
    bufDesc.usage     = RHIBufferUsage::StructuredBuffer
                      | RHIBufferUsage::CopyDst;
    bufDesc.memory    = RHIMemoryType::Default;
    bufDesc.debugName = "EnvMapSampler_AliasTable";
    m_aliasBuffer = m_device->CreateBuffer(bufDesc);

    if (!m_aliasBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "EnvMapSampler: failed to create alias table buffer");
        return false;
    }

    m_aliasSrvIndex = m_device->GetBufferSRVIndex(m_aliasBuffer);

    // Upload via staging buffer
    RHIBufferDesc stagingDesc;
    stagingDesc.sizeBytes = aliasBytes;
    stagingDesc.usage     = RHIBufferUsage::CopySrc;
    stagingDesc.memory    = RHIMemoryType::Upload;
    stagingDesc.debugName = "EnvMapSampler_AliasStaging";
    auto stagingBuf = m_device->CreateBuffer(stagingDesc);

    void* mapped = m_device->MapBuffer(stagingBuf);
    std::memcpy(mapped, aliasTable.data(), aliasBytes);
    m_device->UnmapBuffer(stagingBuf);

    auto cmdList3 = m_device->CreateCommandList();
    cmdList3->Begin();
    cmdList3->CopyBuffer(stagingBuf, 0, m_aliasBuffer, 0, aliasBytes);
    cmdList3->End();
    cmdList3->Execute();
    m_device->WaitIdle();

    m_device->DestroyBuffer(stagingBuf);

    UZ_LOG(Renderer, Info,
           "EnvMapSampler: alias table uploaded ({} entries, {} bytes, SRV={})",
           texelCount, aliasBytes, m_aliasSrvIndex);

    return true;
}

// ─── Diagnostics dump (block B.5b) ─────────────────────────────────────────

void EnvMapSampler::WriteRadianceDump(RHITextureHandle colorTex, uint32_t envCubemapSrvIndex,
                                      const std::string& baseName, uint32_t sampleMip) {
    const uint32_t width = kPyramidWidth, height = kPyramidHeight;
    const uint32_t rowBytes = width * 4 * sizeof(float);
    const uint32_t alignedRowPitch = (rowBytes + 255) & ~255u;

    RHIBufferDesc readbackDesc;
    readbackDesc.sizeBytes = static_cast<uint64_t>(alignedRowPitch) * height;
    readbackDesc.usage     = RHIBufferUsage::CopyDst;
    readbackDesc.memory    = RHIMemoryType::Readback;
    readbackDesc.debugName = "EnvMapSampler_Readback_Radiance";
    auto readbackBuf = m_device->CreateBuffer(readbackDesc);

    auto cmdList = m_device->CreateCommandList();
    cmdList->Begin();
    RHIBarrierDesc toCopySrc;
    toCopySrc.texture     = colorTex;
    toCopySrc.stateBefore = RHIResourceState::UnorderedAccess;
    toCopySrc.stateAfter  = RHIResourceState::CopySrc;
    cmdList->ResourceBarrier(&toCopySrc, 1);
    cmdList->CopyTextureToBuffer(colorTex, 0, readbackBuf, 0, alignedRowPitch, 0);
    cmdList->End();
    cmdList->Execute();
    m_device->WaitIdle();

    std::error_code ec;
    std::filesystem::create_directories(m_dumpDir, ec);
    const std::string binPath  = m_dumpDir + "/" + baseName + ".f32";
    const std::string jsonPath = m_dumpDir + "/" + baseName + ".json";
    const auto* mapped = static_cast<const uint8_t*>(m_device->MapBuffer(readbackBuf));
    bool ok = mapped != nullptr;
    if (ok) {
        std::ofstream out(binPath, std::ios::binary);
        for (uint32_t row = 0; row < height && out; ++row) {
            out.write(reinterpret_cast<const char*>(mapped + static_cast<size_t>(row) * alignedRowPitch), rowBytes);
        }
        ok = static_cast<bool>(out);
        m_device->UnmapBuffer(readbackBuf);
    }
    m_device->DestroyBuffer(readbackBuf);
    if (ok) {
        std::ofstream js(jsonPath);
        js << "{\n  \"file\": \"" << baseName << ".f32\",\n  \"format\": \"rgba32f row-major equirect in cubemap space (u = phi / 2pi, v = theta / pi; the pole is the cube's +Y; world Z-up maps to it by swapping y and z)\",\n"
           << "  \"width\": " << width << ",\n  \"height\": " << height
           << ",\n  \"total_luminance\": " << m_totalLuminance
           << ",\n  \"source_cubemap_srv\": " << envCubemapSrvIndex
           << ",\n  \"source_mip\": " << sampleMip << "\n}\n";
        ok = static_cast<bool>(js);
    }
    if (ok) {
        UZ_LOG(Renderer, Info, "EnvMapSampler: radiance equirect dumped to {}", binPath);
    } else {
        UZ_LOG(Renderer, Error, "EnvMapSampler: radiance dump to {} failed", binPath);
    }
}

bool EnvMapSampler::DumpRadiance(uint32_t cubemapSrvIndex, uint32_t mip, const std::string& baseName) {
    if (m_dumpDir.empty() || cubemapSrvIndex == UINT32_MAX) return false;
    if (!EnsurePipelines()) return false;
    if (m_mipLevels.empty() && !CreatePyramidTextures()) return false;   // the pass writes the luminance mip too

    RHITextureDesc colorDesc;
    colorDesc.width     = kPyramidWidth;
    colorDesc.height    = kPyramidHeight;
    colorDesc.format    = RHIFormat::RGBA32Float;
    colorDesc.usage     = RHITextureUsage::UnorderedAccess | RHITextureUsage::ShaderResource | RHITextureUsage::CopySrc;
    colorDesc.debugName = "EnvMap_RadianceDump";
    RHITextureHandle colorTex = m_device->CreateTexture(colorDesc);
    if (!colorTex.IsValid()) return false;
    const uint32_t colorUav = m_device->GetTextureUAVIndex(colorTex);

    auto cmdList = m_device->CreateCommandList();
    cmdList->Begin();
    RHIBarrierDesc toUav;
    toUav.texture     = colorTex;
    toUav.stateBefore = RHIResourceState::Common;
    toUav.stateAfter  = RHIResourceState::UnorderedAccess;
    cmdList->ResourceBarrier(&toUav, 1);
    // The luminance mip 0 is a scratch here: back to UAV for the pass and to SRV afterwards (DispatchMip0's own
    // barriers assume Common -> UAV -> SRV, which holds right after CreatePyramidTextures; after a Rebuild the
    // texture sits in ShaderResource, so step it back first).
    if (m_ready) {
        RHIBarrierDesc lumToCommon;
        lumToCommon.texture     = m_mipLevels[0].texture;
        lumToCommon.stateBefore = RHIResourceState::ShaderResource;
        lumToCommon.stateAfter  = RHIResourceState::Common;
        cmdList->ResourceBarrier(&lumToCommon, 1);
    }
    DispatchMip0(cmdList, cubemapSrvIndex, colorUav, mip);
    cmdList->End();
    cmdList->Execute();
    m_device->WaitIdle();

    WriteRadianceDump(colorTex, cubemapSrvIndex, baseName, mip);
    m_device->DestroyTexture(colorTex);
    return true;
}

// ─── Main rebuild entry point ───────────────────────────────────────────────

bool EnvMapSampler::Rebuild(uint32_t envCubemapSrvIndex) {
    m_ready = false;

    if (!EnsurePipelines()) return false;
    if (!CreatePyramidTextures()) return false;

    // Diagnostics: the radiance the pyramid samples, dumped after the build (block B.5b)
    RHITextureHandle colorTex;
    uint32_t colorUav = UINT32_MAX;
    if (!m_dumpDir.empty()) {
        RHITextureDesc colorDesc;
        colorDesc.width     = kPyramidWidth;
        colorDesc.height    = kPyramidHeight;
        colorDesc.format    = RHIFormat::RGBA32Float;
        colorDesc.usage     = RHITextureUsage::UnorderedAccess
                            | RHITextureUsage::ShaderResource
                            | RHITextureUsage::CopySrc;
        colorDesc.debugName = "EnvMap_RadianceDump";
        colorTex = m_device->CreateTexture(colorDesc);
        if (colorTex.IsValid()) colorUav = m_device->GetTextureUAVIndex(colorTex);
    }

    // ── GPU: build luminance pyramid ────────────────────────────────────
    auto cmdList = m_device->CreateCommandList();
    cmdList->Begin();

    if (colorTex.IsValid()) {
        RHIBarrierDesc toUav;
        toUav.texture     = colorTex;
        toUav.stateBefore = RHIResourceState::Common;
        toUav.stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&toUav, 1);
    }
    DispatchMip0(cmdList, envCubemapSrvIndex, colorUav, 0u);
    DispatchReductions(cmdList);

    cmdList->End();
    cmdList->Execute();
    m_device->WaitIdle();

    // ── CPU: readback + alias table ─────────────────────────────────────
    if (!BuildAndUploadAliasTable()) {
        if (colorTex.IsValid()) m_device->DestroyTexture(colorTex);
        return false;
    }
    if (colorTex.IsValid()) {
        WriteRadianceDump(colorTex, envCubemapSrvIndex, "env_equirect", 0u);
        m_device->DestroyTexture(colorTex);
    }

    m_ready = true;
    UZ_LOG(Renderer, Info,
           "EnvMapSampler: rebuild complete (pyramid {}x{}, alias SRV={}, lumMip0 SRV={}, totalLum={:.4f})",
           kPyramidWidth, kPyramidHeight, m_aliasSrvIndex, m_lumMip0SrvIndex, m_totalLuminance);
    return true;
}

} // namespace uz
