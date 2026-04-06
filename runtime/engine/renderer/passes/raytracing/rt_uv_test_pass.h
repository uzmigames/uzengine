#pragma once

/// RT UV Test Pass — validates DXR infrastructure with a UV gradient via DispatchRays.
///
/// When enabled (r.RayTracing.TestPass=1), this pass dispatches rays that write
/// a UV gradient (R=u, G=v, B=0) to a full-resolution UAV texture, then
/// composites it over the HDR backbuffer as a fullscreen overlay.
///
/// @ref Microsoft DXR 1.0 Spec — DispatchRays, shader tables, RTPSO.

#include "rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class ShaderManager;

/// Self-contained RT test pass — owns RTPSO, shader tables, and output UAV.
class RTUVTestPass {
public:
    RTUVTestPass() = default;
    ~RTUVTestPass() = default;

    /// Initializes the RT test pass: compiles library shader, creates RTPSO,
    /// builds shader tables, allocates output UAV texture.
    ///
    /// # Arguments
    /// * `device` — RHI device with DXR support.
    /// * `shaderMgr` — Shader manager for compiling the library shader.
    /// * `width` / `height` — Output resolution (typically render resolution).
    ///
    /// # Returns
    /// true if all RT resources were created successfully.
    bool Init(IRHIDevice* device, ShaderManager* shaderMgr,
              uint32_t width, uint32_t height);

    /// Dispatches rays and writes UV gradient to the output UAV.
    ///
    /// Binds the RTPSO, shader tables, output UAV, and TLAS SRV,
    /// then calls DispatchRays at full resolution.
    void Execute(IRHICommandList* cmd, uint32_t tlasSrvIndex);

    /// Returns the SRV index of the output texture for compositing.
    [[nodiscard]] uint32_t GetOutputSRVIndex() const { return m_outputSrv; }

    /// Returns the output texture handle for pass inspector registration.
    [[nodiscard]] RHITextureHandle GetOutputTexture() const { return m_outputTexture; }

    /// Returns true if the pass is initialized and ready to execute.
    [[nodiscard]] bool IsReady() const { return m_initialized; }

    /// Destroys all RT resources.
    void Shutdown(IRHIDevice* device);

private:
    IRHIDevice*          m_device        = nullptr;
    RHIRTPipelineHandle  m_rtPipeline    = RHIRTPipelineHandle::Invalid();
    RHIShaderHandle      m_libShader     = RHIShaderHandle::Invalid();
    RHITextureHandle     m_outputTexture;
    uint32_t             m_outputUav     = UINT32_MAX;
    uint32_t             m_outputSrv     = UINT32_MAX;
    RHIBufferHandle      m_rayGenSBT;
    RHIBufferHandle      m_missSBT;
    RHIBufferHandle      m_hitGroupSBT;
    RHIShaderTableRange  m_rayGenRange   = {};
    RHIShaderTableRange  m_missRange     = {};
    RHIShaderTableRange  m_hitGroupRange = {};
    uint32_t             m_width         = 0;
    uint32_t             m_height        = 0;
    bool                 m_initialized   = false;
};

} // namespace uz
