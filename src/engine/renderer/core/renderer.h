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
#include "engine/renderer/resources/material_instance.h"
#include "engine/renderer/resources/mesh_manager.h"
#include "engine/renderer/resources/ibl_baker.h"
#include "engine/renderer/resources/graph_material_loader.h"
#include "engine/renderer/scene/scene_proxy.h"
#include "engine/renderer/components/editor_component.h"
#include "engine/renderer/components/camera_component.h"
#include "engine/renderer/components/light_component.h"
#include "engine/renderer/components/static_mesh_renderer.h"

#include "engine/core/math.h"
#include "common/types.h"
#include "common/timer.h"
#include "engine/renderer/core/render_path.h"
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

    // Render path configuration (Forward or Deferred).
    [[nodiscard]] RenderPath getRenderPath() const noexcept { return renderPath_; }
    void setRenderPath(RenderPath path);

    // Post-process tuning (set from scene properties or IPC).
    void setAoParams(float radius, float intensity) { aoRadius_ = radius; aoIntensity_ = intensity; }
    void setContactShadows(bool enabled, float length) { contactShadowsEnabled_ = enabled; contactShadowLength_ = length; }
    void setTaaEnabled(bool e) { taaEnabled_ = e; }
    void setAutoExposure(bool enabled, float manual) { autoExposureEnabled_ = enabled; manualExposure_ = manual; }
    void setSsr(bool enabled, float maxRoughness) { ssrEnabled_ = enabled; ssrMaxRoughness_ = maxRoughness; }
    void setColorGrading(float temp, float tint, float sat, float contrast) {
        colorTemperature_ = temp; colorTint_ = tint; colorSaturation_ = sat; colorContrast_ = contrast;
    }
    void setCinematicPost(float ca, float vig, float vigFall, float grain) {
        chromaticAberration_ = ca; vignetteIntensity_ = vig; vignetteFalloff_ = vigFall; filmGrainStrength_ = grain;
    }
    void setDof(float focus, float aperture, float focalLength) {
        dofFocusDistance_ = focus; dofAperture_ = aperture; dofFocalLength_ = focalLength;
    }
    void setFog(float density, const float* color, float heightFalloff, float baseHeight, float maxOpacity) {
        fogDensity_ = density;
        fogColor_[0] = color[0]; fogColor_[1] = color[1]; fogColor_[2] = color[2];
        fogHeightFalloff_ = heightFalloff; fogBaseHeight_ = baseHeight; fogMaxOpacity_ = maxOpacity;
    }
    void setClouds(bool enabled, float coverage, float density,
                   float base, float top, float windSpeed,
                   float windDirX, float windDirZ, float absorption, float ambient) {
        cloudsEnabled_ = enabled; cloudCoverage_ = coverage; cloudDensity_ = density;
        cloudBase_ = base; cloudTop_ = top; cloudWindSpeed_ = windSpeed;
        cloudWindDir_[0] = windDirX; cloudWindDir_[1] = windDirZ;
        cloudAbsorption_ = absorption; cloudAmbient_ = ambient;
    }
    void setCloudQuality(uint32_t q) {
        auto newQ = static_cast<CloudQuality>(q < 4 ? q : 2);
        if (newQ != cloudQuality_) { cloudQuality_ = newQ; cloudHistoryValid_ = false; }
    }
    void setCloudTemporal(bool enabled) { cloudTemporalEnabled_ = enabled; }
    void setCloudDebug(uint32_t mode) { cloudDebugMode_ = mode < 5 ? mode : 0; }

    // Material Instance management (editor dynamic materials).
    MaterialInstanceManager& materialInstances() { return materialInstanceManager_; }
    MaterialManager&         materials()         { return materialManager_; }

    // Force re-cache material params for all proxies (after instance override changes).
    void invalidateMaterialCache();

private:
    // Initialization sub-steps (called by init).
    bool createPipelineStates();
    bool createDepthBuffer();
    bool createReadbackBuffers();
    bool createPostProcessTargets();
    void destroyPostProcessTargets();
    bool createPickBufferResources();
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
    RhiPipeline velocityPSO_        = RHI_NULL;  // Depth-based velocity buffer
    RhiPipeline taaPSO_             = RHI_NULL;  // TAA resolve
    RhiPipeline gtaoPSO_            = RHI_NULL;  // GTAO (replaces SSAO)

    // Deferred shading PSOs
    RhiPipeline gbufferPSO_         = RHI_NULL;  // G-Buffer write (MRT)
    RhiPipeline deferredLightPSO_   = RHI_NULL;  // Deferred lighting (fullscreen)
    RhiPipelineDesc gbufferPipeDesc_{};           // Cached for graph material PSO creation

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

    // CSM shadow atlas — 4 cascades in 4096x4096 atlas (1024x1024 per cascade)
    RhiTexture  shadowMap_           = RHI_NULL;
    static constexpr uint32_t kShadowAtlasSize = 4096;
    static constexpr uint32_t kShadowMapSize   = 2048;  // backward compat constant
    static constexpr uint32_t kCascadeCount    = 4;
    static constexpr uint32_t kCascadeSize     = 1024;   // kShadowAtlasSize / 2

    // Per-cascade: light-space VP matrix + split distance
    Mat4  cascadeVP_[kCascadeCount]      = {};
    float cascadeSplitDist_[kCascadeCount] = {};
    Mat4  lightSpaceMat_ = Mat4::identity();  // cascade 0 for backward compat
    RhiBuffer cascadeCBV_ = RHI_NULL;         // CBV holding cascade VP matrices + splits

    RhiPipeline rhiPickPSO_         = RHI_NULL;  // Pick buffer (node ID as RGB)

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
    float      lastDeltaTime_ = 0.016f;  // previous frame's delta (for auto exposure)

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
    MaterialInstanceManager materialInstanceManager_;
    GraphMaterialLoader     graphLoader_;  // Shader graph → Material bridge
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
    RhiBuffer               ssaoB1CBV_     = RHI_NULL;  // Per-frame CBV for SSAO params (b1)
    RhiBuffer               outlineCBV_    = RHI_NULL;  // Per-frame CBV for outline params (b1)
    RhiTexture              irradianceCube_ = RHI_NULL;
    RhiTexture              prefilteredCube_ = RHI_NULL;
    RhiTexture              envCubemap_ = RHI_NULL;     // Source env cubemap for GPU IBL rebake
    IblBaker                iblBaker_;                    // GPU compute IBL pipeline

    // Sky IBL dirty tracking — regenerate cubemaps when SkyAtmosphere params change.
    float lastSkyZenith_[3]  = {-1.f, -1.f, -1.f};
    float lastSkyHorizon_[3] = {-1.f, -1.f, -1.f};
    float lastSkyGround_[3]  = {-1.f, -1.f, -1.f};
    float lastSunDir_[3]     = {-1.f, -1.f, -1.f};
    double iblLastBakeTime_  = -999.0;  // cooldown to prevent per-frame rebake

    // Render path selection (Forward or Deferred)
    RenderPath renderPath_ = RenderPath::Deferred;

    // GTAO + Contact Shadows tuning (per-scene, set by scene loader / IPC)
    float aoRadius_            = 0.5f;
    float aoIntensity_         = 1.5f;
    bool  contactShadowsEnabled_ = true;
    float contactShadowLength_ = 0.1f;

    // Auto Exposure state
    RhiPipeline autoExposurePSO_  = RHI_NULL;
    static constexpr uint32_t kExposureHistoryCount = 2;  // ping-pong
    RhiTexture exposureRT_[kExposureHistoryCount] = {};   // 1x1 R16_FLOAT
    uint32_t   exposureWriteIdx_ = 0;
    bool       autoExposureEnabled_ = true;
    float      manualExposure_      = 1.0f;  // used when auto exposure is off
    RhiBuffer  exposureCBV_         = RHI_NULL;

    // SSR state
    RhiPipeline ssrPSO_         = RHI_NULL;
    RhiPipeline hizPSO_         = RHI_NULL;
    RhiTexture  ssrRT_          = RHI_NULL;  // Full-res R11G11B10_FLOAT SSR result
    static constexpr uint32_t kHiZMips = 8;
    RhiTexture  hizMips_[kHiZMips] = {};     // Hi-Z depth mip chain (R32_FLOAT)
    RhiBuffer   ssrCBV_         = RHI_NULL;
    bool        ssrEnabled_     = true;
    float       ssrMaxRoughness_ = 0.5f;

    // Color Grading state
    RhiPipeline colorGradingPSO_ = RHI_NULL;
    RhiTexture  identityLUT_     = RHI_NULL;  // 32x32x32 identity 3D LUT
    float       colorTemperature_ = 0.0f;
    float       colorTint_        = 0.0f;
    float       colorSaturation_  = 1.0f;
    float       colorContrast_    = 1.0f;

    // Cinematic post state
    float       chromaticAberration_ = 0.0f;
    float       vignetteIntensity_ = 0.0f;
    float       vignetteFalloff_   = 2.0f;
    float       filmGrainStrength_ = 0.0f;

    // DoF state
    float       dofFocusDistance_ = 0.0f;   // 0 = disabled
    float       dofAperture_     = 5.6f;
    float       dofFocalLength_  = 50.0f;

    // Volumetric fog state
    float       fogDensity_       = 0.0f;   // 0 = disabled
    float       fogColor_[3]      = {0.5f, 0.6f, 0.7f};
    float       fogHeightFalloff_ = 0.5f;
    float       fogBaseHeight_    = 0.0f;
    float       fogMaxOpacity_    = 0.95f;

    // Volumetric clouds state
    RhiPipeline cloudsPSO_           = RHI_NULL;  // Blended fullscreen cloud pass
    RhiPipeline cloudsTemporalPSO_   = RHI_NULL;  // Temporal accumulation resolve
    bool        cloudsEnabled_       = false;
    float       cloudCoverage_       = 0.5f;       // [0..1] sky coverage
    float       cloudDensity_        = 0.15f;      // extinction coefficient
    float       cloudBase_           = 800.0f;     // cloud layer bottom (m)
    float       cloudTop_            = 2500.0f;    // cloud layer top (m)
    float       cloudWindSpeed_      = 8.0f;       // drift speed (m/s)
    float       cloudWindDir_[2]     = {1.0f, 0.0f};  // XZ wind direction (normalized)
    float       cloudAbsorption_     = 0.85f;      // single-scattering albedo
    float       cloudAmbient_        = 0.5f;       // ambient sky contribution

    // Cloud quality scalability
    enum class CloudQuality : uint32_t { LOW = 0, MEDIUM = 1, HIGH = 2, CINEMATIC = 3 };
    CloudQuality cloudQuality_       = CloudQuality::HIGH;
    bool         cloudTemporalEnabled_ = true;
    uint32_t     cloudDebugMode_     = 0;  // 0=off, 1=density, 2=steps, 3=transmittance, 4=lighting

    // Temporal accumulation targets (double-buffered)
    static constexpr uint32_t kCloudHistoryCount = 2;
    RhiTexture cloudHistoryRT_[kCloudHistoryCount] = {};  // RGBA16F, full-res
    uint32_t   cloudHistoryWriteIdx_ = 0;
    bool       cloudHistoryValid_    = false;

    // G-Buffer render targets (deferred path only)
    static constexpr uint32_t kGBufferCount = 4;
    RhiTexture gbufferRT_[kGBufferCount] = {};  // 0=AlbedoAO, 1=Normal, 2=Material, 3=Emission
    RhiBuffer  deferredLightCBV_ = RHI_NULL;    // Per-frame CBV for deferred lighting params
    bool       gbufferReady_     = false;        // First-frame barrier state tracking

    // Post-process render targets
    RhiTexture hdrRT_         = RHI_NULL;  // Full-res R11G11B10_FLOAT scene color
    RhiTexture ssaoRT_        = RHI_NULL;  // Half-res R8_UNORM SSAO
    RhiTexture ssaoBlurRT_    = RHI_NULL;  // Half-res R8_UNORM SSAO (blurred)
    static constexpr uint32_t kBloomMips = 5;
    RhiTexture bloomMips_[kBloomMips] = {};  // Progressively halved bloom chain

    // Projection matrix (for SSAO depth reconstruction), updated in update()
    Mat4 projMatrix_ = Mat4::identity();
    // View matrix — needed for contact shadows (lightDir world→view transform)
    Mat4 viewMatrix_ = Mat4::identity();

    // Unjittered viewProj — used for velocity buffer (jitter must NOT appear in motion vectors)
    Mat4 viewProjUnjittered_ = Mat4::identity();
    // Previous-frame unjittered viewProj for velocity/reprojection
    Mat4 prevViewProj_ = Mat4::identity();

    // Velocity buffer (R16G16_FLOAT) for TAA/GTAO/SSR motion vectors (prerequisite 0.3)
    RhiTexture velocityRT_ = RHI_NULL;

    // PostProcessCB — shared constant buffer for TAA/SSR/DoF/exposure params (prerequisite 0.2)
    RhiBuffer postProcessCBV_ = RHI_NULL;

    // TAA state
    static constexpr uint32_t kTAAHistoryCount = 2;  // ping-pong
    RhiTexture taaHistory_[kTAAHistoryCount] = {};    // R11G11B10_FLOAT, full-res
    uint32_t   taaWriteIdx_ = 0;                      // alternates 0/1 each frame
    bool       taaEnabled_  = true;                    // toggle
    float      taaJitterX_  = 0.0f;                   // current jitter in UV space
    float      taaJitterY_  = 0.0f;

    // Punctual light buffer (uploaded to LightFrameCB alongside directional light data)
    static constexpr uint32_t kMaxPunctualLights = 8;
    struct GpuPunctualLight {
        float posX, posY, posZ, radius;
        float colR, colG, colB, intensity;
        float dirX, dirY, dirZ, spotParams;  // spotParams=cosOuter for spot, 0 for point
    };
    uint32_t        punctualLightCount_ = 0;
    GpuPunctualLight punctualLights_[kMaxPunctualLights] = {};

    void gatherPunctualLights();  // Collects point/spot lights from activeScene_

#ifdef UZ_EDITOR
public:
    [[nodiscard]] uint32_t pickNodeAtScreen(int x, int y) const;
    [[nodiscard]] std::vector<uint32_t> boxPickNodes(int x, int y, int w, int h) const;

    /// Test-only: inject pick buffer data for unit testing without GPU.
    void setPickDataForTest(uint32_t w, uint32_t h, std::vector<uint8_t> data) {
        std::lock_guard<std::mutex> lk(pickMutex_);
        pickW_ = w;
        pickH_ = h;
        pickData_ = std::move(data);
    }

private:
    RhiTexture  pickRT_        = RHI_NULL;  // 1/4 res RGBA8 RT
    RhiTexture  pickDepth_     = RHI_NULL;  // 1/4 res depth buffer
    RhiBuffer   pickReadback_  = RHI_NULL;  // CPU-readable staging buffer
    uint32_t             pickW_ = 0;
    uint32_t             pickH_ = 0;
    mutable std::mutex   pickMutex_;
    std::vector<uint8_t> pickData_;
#endif
};

} // namespace uz
