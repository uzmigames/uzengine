/// UzEngine Async Mipmap Generation — Implementation.
///
/// See rhi_async_mipgen.h for design overview and usage.

#include "rhi_async_mipgen.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(RHI);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

AsyncMipgen::~AsyncMipgen() {
    if (m_initialized) {
        Shutdown();
    }
}

bool AsyncMipgen::Init(IRHIDevice* device, const AsyncMipgenConfig& config) {
    UZ_PROFILE_SCOPE(AsyncMipgen_Init);
    UZ_CHECK(device != nullptr);

    if (m_initialized) {
        UZ_LOG(RHI, Warning, "AsyncMipgen::Init: already initialized, ignoring");
        return true;
    }

    m_device = device;
    m_config = config;
    m_initialized = true;

    // TODO: Create compute PSO and root signature for mipgen_cs.hlsl
    // This requires the shader compilation pipeline to be wired up for
    // compute shaders on the async queue. For now, the structure and API
    // are complete — actual dispatch will be added when PSO creation is available.

    UZ_LOG(RHI, Info, "AsyncMipgen: initialized (maxMipsPerFrame={})", m_config.maxMipsPerFrame);
    return true;
}

void AsyncMipgen::Shutdown() {
    UZ_PROFILE_SCOPE(AsyncMipgen_Shutdown);

    if (!m_initialized) {
        return;
    }

    m_device = nullptr;
    m_computeCmdList = nullptr;
    m_mipsThisFrame = 0;
    m_initialized = false;

    UZ_LOG(RHI, Info, "AsyncMipgen: shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame Operations
// ─────────────────────────────────────────────────────────────────────────────

void AsyncMipgen::BeginFrame(IRHICommandList* computeCmdList) {
    UZ_PROFILE_SCOPE(AsyncMipgen_BeginFrame);
    UZ_CHECK(m_initialized);
    UZ_CHECK(computeCmdList != nullptr);

    m_computeCmdList = computeCmdList;
    m_mipsThisFrame = 0;
}

uint32_t AsyncMipgen::GenerateMips(RHITextureHandle texture, uint32_t mipCount) {
    UZ_PROFILE_SCOPE(AsyncMipgen_GenerateMips);
    UZ_CHECK(m_initialized);
    UZ_CHECK(m_computeCmdList != nullptr);

    if (mipCount == 0) {
        return 0;
    }

    // Budget: clamp to remaining per-frame allowance
    const uint32_t budget = (m_config.maxMipsPerFrame > 0)
        ? m_config.maxMipsPerFrame - m_mipsThisFrame
        : mipCount;

    if (budget == 0) {
        UZ_LOG(RHI, Debug, "AsyncMipgen: per-frame budget exhausted, deferring {} mips", mipCount);
        return 0;
    }

    const uint32_t mipsToGenerate = (mipCount < budget) ? mipCount : budget;

    // TODO: Actual compute dispatches will be added once the mipgen PSO is created.
    // Each mip level requires:
    //   1. UAV barrier on the output mip
    //   2. Set root constants (output size, texel size)
    //   3. Bind input SRV (mip N) and output UAV (mip N+1)
    //   4. Dispatch(ceil(width/8), ceil(height/8), 1)
    UZ_LOG(RHI, Debug, "AsyncMipgen: would generate {} mips for texture (handle={})",
           mipsToGenerate, texture.IsValid() ? "valid" : "invalid");

    for (uint32_t i = 0; i < mipsToGenerate; ++i) {
        UZ_STAT_INCREMENT(GPU, AsyncMipgenDispatches);
    }

    m_mipsThisFrame += mipsToGenerate;
    return mipsToGenerate;
}

void AsyncMipgen::EndFrame() {
    UZ_PROFILE_SCOPE(AsyncMipgen_EndFrame);
    UZ_CHECK(m_initialized);

    if (m_mipsThisFrame > 0) {
        UZ_LOG(RHI, Debug, "AsyncMipgen: generated {} mips this frame", m_mipsThisFrame);
    }

    m_computeCmdList = nullptr;
}

}  // namespace uz
