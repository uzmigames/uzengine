/// IBL Test Demo — Validates Image-Based Lighting via procedural environment cubemap.
///
/// Renders a grid of cubes with default materials under IBL ambient lighting
/// from a procedurally generated sky cubemap. The visual test confirms that
/// IBL reflections (irradiance + prefiltered specular) produce correct ambient
/// lighting instead of the flat constant ambient fallback.
///
/// Extends `DemoApplication` which owns all engine infrastructure. This class
/// only owns demo-specific resources: mesh, procedural cubemap, IBL bake results,
/// and the depth buffer.
///
/// Supports `--screenshot <path>` and `--frames <N>` for automated visual testing.

#pragma once

#include "demo_application.h"

#include "engine/renderer/ibl/ibl_baker.h"
#include "engine/renderer/mesh/gpu_mesh.h"
#include "engine/rhi/rhi_types.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace uz {

/// Configuration for the IBL test demo.
struct IBLTestConfig {
    uint32_t frameCount = 0;        ///< Number of frames to render (0 = infinite).
    std::string screenshotPath;     ///< If non-empty, save screenshot to this path.
    uint32_t screenshotFrame = 5;   ///< Frame number to capture (default: 5).
};

/// IBL Test Demo — renders cubes under IBL ambient lighting from a procedural sky.
///
/// # Lifecycle
/// - `OnInit()` — Loads mesh, generates procedural env cubemap, bakes IBL textures.
/// - `OnFrame(dt)` — Forward PBR pass with IBL SRV indices in the per-frame CB.
/// - `OnResize(w, h)` — Recreates the depth buffer.
/// - `OnShutdown()` — Releases demo-specific GPU resources.
class IBLTestDemo final : public DemoApplication {
public:
    /// Constructs the IBL test demo with the given configuration.
    ///
    /// # Arguments
    /// * `config` — Demo settings (frame count, screenshot path).
    explicit IBLTestDemo(const IBLTestConfig& config = {});
    ~IBLTestDemo() override;

protected:
    void OnInit() override;
    void OnFrame(float dt) override;
    void OnResize(uint32_t width, uint32_t height) override;
    void OnShutdown() override;

private:
    /// Loads the cube mesh from disk and uploads to GPU.
    void LoadMeshData();

    /// Creates the depth buffer at the current window dimensions.
    void CreateDepthBuffer();

    /// Generates a procedural sky gradient cubemap (64x64 per face, RGBA16F)
    /// and uploads it to the GPU as a Texture2DArray with 6 slices.
    void CreateProceduralEnvCubemap();

    // ── Configuration ───────────────────────────────────────────────────
    IBLTestConfig config_;

    // ── Timing ──────────────────────────────────────────────────────────
    std::chrono::high_resolution_clock::time_point startTime_;
    float totalTime_ = 0.f;

    // ── Mesh data ───────────────────────────────────────────────────────
    GPUMesh cubeMesh_;

    // ── IBL Baker ───────────────────────────────────────────────────────
    IBLBaker iblBaker_;

    // ── Environment cubemap (procedural sky gradient) ───────────────────
    RHITextureHandle envCubemap_ = RHITextureHandle::Invalid();
    uint32_t envCubemapSrv_ = UINT32_MAX;

    // ── Baked IBL SRV indices ───────────────────────────────────────────
    uint32_t brdfLutSrv_ = UINT32_MAX;
    uint32_t irradianceSrv_ = UINT32_MAX;
    uint32_t prefilteredSrv_ = UINT32_MAX;

    // ── Depth buffer ────────────────────────────────────────────────────
    RHITextureHandle depthBuffer_ = RHITextureHandle::Invalid();
};

} // namespace uz
