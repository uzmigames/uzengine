/// UzEngine Scene Renderer — Debug, accessors, screenshot, shadow readback.

#include "scene_renderer.h"

#include "camera.h"
#include "fly_camera_controller.h"
#include "shadow_atlas.h"
#include "shadow_pass.h"
#include "rhi.h"
#include "log.h"
#include "input.h"
#include "stats_overlay.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// RequestScreenshot
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::RequestScreenshot(const std::string& path) {
    m_screenshotPath    = path;
    m_screenshotPending = true;
}

void SceneRenderer::RequestScreenshotBase64() {
    m_screenshotBase64  = true;
    m_screenshotPending = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────

Camera& SceneRenderer::GetCamera() { return *m_camera; }
const Camera& SceneRenderer::GetCamera() const { return *m_camera; }
FlyCameraController& SceneRenderer::GetCameraController() { return *m_cameraCtrl; }
InputSystem* SceneRenderer::GetInput() const { return m_input.get(); }
StatsOverlay* SceneRenderer::GetStatsOverlay() const { return m_statsOverlay.get(); }

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Atlas Diagnostic Readback
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::PerformShadowAtlasReadback(IRHICommandList* cmdList) {
    if (!m_shadowAtlas.GetTexture().IsValid()) {
        m_shadowReadbackPhase = 2;
        return;
    }

    if (m_shadowReadbackPhase == 0) {
        // Phase 0: Create readback buffer, issue GPU copy.
        // Buffer stays alive — destroyed only in phase 1 after GPU is done.
        const uint32_t atlasWidth = kShadowResolution * GetAtlasColumns(4);
        const uint32_t rowBytes   = atlasWidth * 4;
        const uint32_t rowPitch   = (rowBytes + 255) & ~255u;
        m_shadowReadbackRowPitch  = rowPitch;
        m_shadowReadbackWidth     = atlasWidth;

        RHIBufferDesc rbDesc{};
        rbDesc.sizeBytes = rowPitch;
        rbDesc.memory    = RHIMemoryType::Readback;
        rbDesc.debugName = "ShadowAtlasReadback";
        m_shadowReadbackBuffer = m_device->CreateBuffer(rbDesc);
        if (!m_shadowReadbackBuffer.IsValid()) {
            UZ_LOG(Shadow, Error, "ShadowAtlasReadback: failed to create buffer");
            m_shadowReadbackPhase = 2;
            return;
        }

        RHIBarrierDesc toCopy{};
        toCopy.texture     = m_shadowAtlas.GetTexture();
        toCopy.stateBefore = RHIResourceState::ShaderResource;
        toCopy.stateAfter  = RHIResourceState::CopySrc;
        cmdList->ResourceBarrier(&toCopy, 1);

        cmdList->CopyTextureToBuffer(m_shadowAtlas.GetTexture(), 0,
                                      m_shadowReadbackBuffer, 0, rowPitch);

        RHIBarrierDesc toSrv{};
        toSrv.texture     = m_shadowAtlas.GetTexture();
        toSrv.stateBefore = RHIResourceState::CopySrc;
        toSrv.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&toSrv, 1);

        UZ_LOG(Shadow, Info, "ShadowAtlasReadback: GPU copy issued (phase 0)");
        m_shadowReadbackPhase = 1;

    } else if (m_shadowReadbackPhase >= 1 && m_shadowReadbackPhase <= 3) {
        // Phases 1-3: Wait for GPU to finish the copy command.
        // Triple-buffered = need 3 frames before the copy is guaranteed complete.
        UZ_LOG(Shadow, Info, "ShadowAtlasReadback: waiting (phase {})", m_shadowReadbackPhase);
        m_shadowReadbackPhase++;

    } else if (m_shadowReadbackPhase == 4) {
        // Phase 4: At least 3 full frames passed since the copy — safe to read.
        // Buffer is NOT destroyed here — kept alive until Shutdown to avoid
        // any possibility of destroying while GPU references it.
        void* mapped = m_device->MapBuffer(m_shadowReadbackBuffer);
        if (mapped) {
            const float* row0 = reinterpret_cast<const float*>(mapped);
            const uint32_t rowPixels = m_shadowReadbackWidth;
            const uint32_t halfRes   = kShadowResolution / 2;

            UZ_LOG(Shadow, Info, "=== Shadow Atlas Diagnostic Readback ===");
            UZ_LOG(Shadow, Info, "Atlas: {}x{} D32Float", rowPixels, rowPixels);
            UZ_LOG(Shadow, Info, "Row 0: px[0]={:.6f} px[{}]={:.6f} px[{}]={:.6f} px[{}]={:.6f}",
                   row0[0],
                   halfRes/2, row0[std::min(halfRes/2, rowPixels-1)],
                   halfRes,   row0[std::min(halfRes, rowPixels-1)],
                   kShadowResolution-1, row0[std::min(kShadowResolution-1, rowPixels-1)]);

            uint32_t nCleared = 0, nGeom = 0, nZero = 0;
            float minV = 1.0f, maxV = 0.0f;
            for (uint32_t x = 0; x < std::min(rowPixels, kShadowResolution); ++x) {
                float v = row0[x];
                if (v >= 0.9999f) ++nCleared;
                else if (v <= 0.0001f) ++nZero;
                else ++nGeom;
                minV = std::min(minV, v);
                maxV = std::max(maxV, v);
            }
            UZ_LOG(Shadow, Info, "Stats: cleared(1.0)={} geometry={} zero(0.0)={} min={:.8f} max={:.8f}",
                   nCleared, nGeom, nZero, minV, maxV);

            if (nZero > 0 && nGeom == 0 && nCleared == 0)
                UZ_LOG(Shadow, Error, "ALL ZERO — shadow atlas not written!");
            else if (nGeom > 0)
                UZ_LOG(Shadow, Info, "Shadow atlas contains valid depth data");
            else
                UZ_LOG(Shadow, Warning, "No geometry in row 0 — check cascade coverage");

            m_device->UnmapBuffer(m_shadowReadbackBuffer);
        } else {
            UZ_LOG(Shadow, Error, "ShadowAtlasReadback: failed to map buffer");
        }

        // Keep buffer alive — destroyed in Shutdown() to avoid GPU reference issues.
        m_shadowReadbackPhase = 5; // done
    }
}

} // namespace uz
