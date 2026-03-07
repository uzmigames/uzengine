#pragma once

// renderer.h -- Renderer using the RHI abstraction layer
//
// All GPU work goes through rhi()->* calls.  No direct DX12 / Vulkan / Metal
// includes leak into this header.
//
// RULE RHI-1: No backend types above rhi.h
// RULE RHI-2: No DirectXMath / GLM above rhi.h
// RULE CS-2:  Use engine math types (uz::Vec3, uz::Mat4)
// RULE CS-3:  Plain float arrays for GPU-uploaded data

#include <cstdint>
#include <mutex>
#include <vector>

#include "engine/renderer/core/fps_overlay.h"
#include "engine/renderer/core/render_graph.h"
#include "engine/renderer/resources/shader_library.h"
#include "engine/renderer/resources/material.h"
#include "engine/renderer/resources/mesh_manager.h"
#include "engine/renderer/resources/ibl_baker.h"
#include "engine/renderer/scene/scene_proxy.h"
#include "engine/renderer/components/editor_component.h"
#include "engine/renderer/components/camera_component.h"
#include "engine/renderer/components/light_component.h"
#include "engine/renderer/components/static_mesh_renderer.h"

#include "engine/core/math.h"
#include "common/types.h"
#include "common/timer.h"
#include "engine/renderer/scene/scene_params.h"

// RHI abstraction
extern "C" {
#include "engine/renderer/rhi/rhi.h"
}

namespace uz {

// ---------------------------------------------------------------------------
// Shaders are embedded at build-time from src/engine/renderer/shaders/*.hlsl
// and managed by ShaderLibrary (named registry + PSO caching).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Vertex layout for GPU upload (RULE CS-3: plain floats for GPU data).
// Float3 is defined in engine/core/math.h (12-byte POD, no padding).
// ---------------------------------------------------------------------------

struct Vertex {
    Float3 position;
    Float3 color;
};

// ---------------------------------------------------------------------------
// Renderer -- Backend-agnostic renderer using the RHI abstraction layer.
//
// Lifecycle:
//   1. Construct
//   2. init(hwnd, width, height)
//   3. Loop: update(totalTime) -> render()
//   4. shutdown()
//
// All GPU resources are managed through RHI handles.  shutdown() performs
// an explicit GPU drain so that destruction order is safe.
// ---------------------------------------------------------------------------

class Renderer {
public:
    Renderer()  = default;
    ~Renderer() = default;

    // Non-copyable, non-movable.
    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&)                 = delete;
    Renderer& operator=(Renderer&&)      = delete;

    // EditorNode is now defined in EditorComponent.
    using EditorNode = EditorComponent::EditorNode;

    // Initialize the renderer and all GPU resources for the given window.
    // window_handle is an opaque HWND (cast to void* for platform neutrality).
    bool init(void* window_handle, uint32_t width, uint32_t height);

    // Update the rotation angle using delta-time accumulation.
    void update(double totalTime, double deltaTime);

    /// Record and execute the command list, present the frame.
    /// @param readback  When true, copy the rendered frame to a CPU-readable
    ///                   buffer (for JPEG streaming).
    void render(bool readback = true);

    /// Resize the swap chain, render targets, depth buffer, and readback
    /// buffers to the given dimensions.
    bool resize(int width, int height);

    /// Current render target width in pixels.
    [[nodiscard]] int getWidth() const noexcept { return static_cast<int>(width_); }

    /// Current render target height in pixels.
    [[nodiscard]] int getHeight() const noexcept { return static_cast<int>(height_); }

    /// Stream (JPEG) resolution — currently matches render resolution.
    /// Used by ws_handlers if stream differs from render size.
    [[nodiscard]] uint32_t streamWidth()  const noexcept { return width_; }
    [[nodiscard]] uint32_t streamHeight() const noexcept { return height_; }

    // Drain the GPU and release all resources.  Safe to call multiple times.
    void shutdown();

    // Returns a copy of the most recently captured frame. Thread-safe via mutex.
    [[nodiscard]] FrameData getLatestFrame() const;

    // Alias for getLatestFrame().
    [[nodiscard]] FrameData getLastFrameCopy() const;

    // Set the external scene parameters struct.
    void setSceneParams(SceneParams* params);

    // Set the scene pointer for scene-driven camera and lighting.
    void setScene(Scene* scene);

    // Replace the editor scene nodes and switch to editor rendering mode.
    size_t loadEditorScene(std::vector<EditorNode> nodes);

    // Returns a copy of the current editor nodes. Thread-safe.
    std::vector<EditorNode> getEditorNodes() const { return editor_.getNodes(); }

    // Replace editor nodes (thread-safe delegate). For gizmo drag updates.
    void loadEditorNodes(std::vector<EditorNode> nodes) { editor_.loadNodes(std::move(nodes)); }

private:
    // Initialization sub-steps (called by init).
    bool createPipelineStates();
    bool createDepthBuffer();
    bool createReadbackBuffers();
    bool createPostProcessTargets();
    void destroyPostProcessTargets();
    void setupTestScene();

    // Perform GPU -> CPU readback of the current frame's render target.
    void performReadback();

    // -----------------------------------------------------------------------
    // RHI resource handles
    // -----------------------------------------------------------------------
    RhiPipeline rhiPipeline_        = RHI_NULL;
    RhiPipeline rhiPbrPipeline_     = RHI_NULL;
    RhiPipeline rhiStencilMarkPSO_  = RHI_NULL;  // Stencil write, no color
    RhiPipeline rhiOutlinePSO_      = RHI_NULL;  // Outline draw, stencil test
    RhiPipeline rhiSkyPipeline_     = RHI_NULL;  // Fullscreen sky gradient
    RhiPipeline rhiShadowPipeline_  = RHI_NULL;  // Depth-only shadow pass

    // Post-process pipeline states
    RhiPipeline ssaoPSO_            = RHI_NULL;  // SSAO generation
    RhiPipeline ssaoBlurPSO_        = RHI_NULL;  // SSAO bilateral blur
    RhiPipeline bloomDownPSO_       = RHI_NULL;  // Bloom downsample
    RhiPipeline bloomUpPSO_         = RHI_NULL;  // Bloom upsample (additive)
    RhiPipeline compositePSO_       = RHI_NULL;  // Final composite (tonemap+gamma)

    // Sky CB data — updated in update(), consumed in render()
    struct SkyCB {
        float camForward[3]; float tanHalfFovY;
        float camRight[3];   float aspect;
        float camUp[3];      float _pad;
        float sunDir[3];     float sunIntensity;  // directional light for sun disk
    } skyCB_{};

    // Sky colors CB — sourced from SkyAtmosphere scene component, or hardcoded defaults.
    // Bound at b0 in the sky pass (b0 is unused for world matrix in that draw).
    // Defaults match the original UE5-inspired hardcoded palette.
    struct SkyColorsCB {
        float skyZenith[3];  float _sc0;       // deep azure zenith
        float skyHorizon[3]; float _sc1;       // pale bright sky-blue at horizon
        float skyHaze[3];    float _sc2;       // warm atmospheric haze below horizon
        float skyGround[3];  float fogDensity; // muted earth / height fog
    } skyColorsCB_{
        {0.03f, 0.07f, 0.45f}, 0.0f,  // zenith: deep saturated blue
        {0.22f, 0.42f, 0.82f}, 0.0f,  // horizon: vivid sky blue
        {0.55f, 0.45f, 0.32f}, 0.0f,  // haze: warm golden scatter
        {0.20f, 0.18f, 0.15f}, 0.0f,  // ground: dark neutral
    };

    // Shadow map resources
    RhiTexture  shadowMap_           = RHI_NULL;  // 2048×2048 D32 shadow depth
    static constexpr uint32_t kShadowMapSize = 2048;

    // Light-space matrix for shadow pass (orthographic, updated in update())
    Mat4 lightSpaceMat_ = Mat4::identity();

    // Shader library (named shader registry + PSO cache).
    ShaderLibrary shaderLib_;
    RhiTexture  rhiDepthBuf_  = RHI_NULL;
    RhiFence    rhiReadbackFence_ = RHI_NULL;
    RhiBuffer   rhiReadbackBufs_[kFrameCount] = {};

    uint64_t    readbackFenceValue_ = 0;

    // Latest captured frame (protected by mutex for cross-thread access).
    mutable std::mutex frameMutex_;
    FrameData          latestFrame_;
    uint32_t           frameNumber_       = 0;

    // Stream timer — started on init(), used for frame timestamps.
    Timer              streamTimer_;

    // Frame tracking.
    uint32_t frameIndex_ = 0;

    // Render dimensions.
    uint32_t width_  = 0;
    uint32_t height_ = 0;

    // Current view-projection matrix, updated each frame by update().
    // Uses engine math types (RULE CS-2).
    Mat4 viewProj_ = Mat4::identity();

    bool initialized_ = false;

    // External scene parameters (set via setSceneParams, nullable).
    SceneParams* sceneParams_ = nullptr;

    // External scene pointer (set via setScene, nullable).
    Scene* scene_ = nullptr;

    // Cached light state.
    LightState cachedLight_;

    // CPU-side FPS overlay.
    FPSOverlay fpsOverlay_;

    // Cached clear color -- updated from sceneParams_ each frame in update().
    float clearR_ = 0.16f;
    float clearG_ = 0.16f;
    float clearB_ = 0.16f;

    // -----------------------------------------------------------------------
    // Render components (modular subsystems)
    // -----------------------------------------------------------------------
    EditorComponent editor_;

    // PBR mesh renderer (spheres, loaded models, etc.).
    StaticMeshRenderer meshRenderer_;

    // Frame-local render graph (reset each frame).
    RenderGraph renderGraph_;

    // Material and Proxy management
    MaterialManager         materialManager_;
    MeshManager             meshManager_;
    std::vector<ISceneProxy*> proxies_;
    std::vector<uint32_t>   proxy_node_ids_;   // node ID per proxy
    bool                    scene_dirty_ = true;
    uint32_t                last_node_count_ = 0;
    FrameConstants          frameConstants_;
    GpuMesh                 sphereMesh_;
    RhiTexture              whiteTex_ = RHI_NULL;
    RhiTexture              blackTex_ = RHI_NULL;
    RhiTexture              normalTex_ = RHI_NULL;
    RhiTexture              brdfLut_  = RHI_NULL;
    RhiBuffer               lightFrameCBV_ = RHI_NULL;  // Per-frame CBV for LightFrameCB (b2)
    RhiTexture              irradianceCube_ = RHI_NULL;
    RhiTexture              prefilteredCube_ = RHI_NULL;
    RhiTexture              envCubemap_ = RHI_NULL;     // Source env cubemap for GPU IBL rebake
    IblBaker                iblBaker_;                    // GPU compute IBL pipeline

    // Sky IBL dirty tracking — regenerate cubemaps when SkyAtmosphere params change.
    float lastSkyZenith_[3]  = {-1.f, -1.f, -1.f};
    float lastSkyHorizon_[3] = {-1.f, -1.f, -1.f};
    float lastSkyGround_[3]  = {-1.f, -1.f, -1.f};
    float lastSunDir_[3]     = {-1.f, -1.f, -1.f};

    // Post-process render targets
    RhiTexture hdrRT_         = RHI_NULL;  // Full-res R11G11B10_FLOAT scene color
    RhiTexture ssaoRT_        = RHI_NULL;  // Half-res R8_UNORM SSAO
    RhiTexture ssaoBlurRT_    = RHI_NULL;  // Half-res R8_UNORM SSAO (blurred)
    static constexpr uint32_t kBloomMips = 5;
    RhiTexture bloomMips_[kBloomMips] = {};  // Progressively halved bloom chain

    // Projection matrix (for SSAO depth reconstruction), updated in update()
    Mat4 projMatrix_ = Mat4::identity();

#ifdef UZ_EDITOR
public:
    [[nodiscard]] uint32_t pickNodeAtScreen(int x, int y) const;
    [[nodiscard]] std::vector<uint32_t> boxPickNodes(int x, int y, int w, int h) const;

private:
    uint32_t             pickW_ = 0;
    uint32_t             pickH_ = 0;
    mutable std::mutex   pickMutex_;
    std::vector<uint8_t> pickData_;
#endif
};

} // namespace uz
