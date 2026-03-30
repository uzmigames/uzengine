/// VFX Viewport Demo — DemoApplication Subclass
///
/// Renders procedural PBR meshes, a procedural sky atmosphere, shadow mapping,
/// HDR tonemapping, and GPU-driven particle effects. Uses the DemoApplication
/// base class for window, device, swapchain, input, camera, shaders, and
/// pipeline management.
///
/// Replaces the legacy monolithic `tools/uz_viewport.cpp` (5095 lines, raw DX12,
/// SM5.0 FXC) with standard engine infrastructure:
/// - ShaderManager for SM6.6/SM6.0 shader compilation (no embedded HLSL strings)
/// - PipelineLibrary for cached PSO creation
/// - CBRingAllocator for per-frame constant buffer uploads
/// - FPSCameraController for fly/orbit camera
/// - RHI abstraction for all GPU resource management
///
/// # Features
/// - PBR sphere/cube/plane/cylinder/torus with material presets (Left/Right arrows)
/// - Mesh cycling (Up/Down arrows)
/// - Procedural sky (Rayleigh + Mie atmosphere via sky_atmosphere_ps.hlsl)
/// - 2048x2048 shadow depth map with PCF sampling (shadow bias: [/] keys)
/// - HDR render target (RGBA16F) with ACES tonemap (exposure: +/- keys)
/// - GPU compute bloom (threshold + downsample + upsample, B key toggles)
/// - GPU particle system with 5 emitter presets (Tab to cycle, Shift+Tab toggle)
/// - Debug view modes (F1-F11)
/// - Editor grid overlay
/// - Stats overlay (FPS, draw calls, frame time)
/// - Screenshot capture (F5 / --screenshot)

#pragma once

#include "demo_application.h"
#include "vfx_viewport_mesh.h"
#include "vfx_viewport_vfx.h"
#include "bloom_pass.h"
#include "rhi_types.h"
#include "mat4.h"
#include "quaternion.h"
#include "vector3.h"
#include "engine/vfx/trails/trail_system.h"
#include "engine/vfx/trails/trail_component.h"
#include "engine/vfx/decals/decal_component.h"
#include "engine/vfx/material/vfx_material.h"
#include "outline_pass.h"
#include "gizmo_renderer.h"
#include "gizmo_interaction.h"
#include "editor_input_handler.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Material Presets
// ─────────────────────────────────────────────────────────────────────────────

/// PBR material preset for the scene mesh.
struct MaterialPreset {
    const char* name;
    float baseColor[3];
    float metallic;
    float roughness;
    float ao;
    float emissiveColor[3];
    float emissiveIntensity;
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame Constant Buffer (local to this demo)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame constant buffer for the forward PBR + sky + shadow + tonemap
/// shaders in this demo. This uses the engine's standard PerFrameCB layout
/// (from constant_buffers.h) for compatibility with engine shaders.
///
/// Note: The VFX compute and billboard shaders use their own CB layouts
/// (VFXSpawnCB, VFXUpdateCB, VFXBillboardIndicesCB, VFXEmitterCB) defined
/// in vfx_viewport_vfx.h, matching the engine's VFX shader layouts.

// ─────────────────────────────────────────────────────────────────────────────
// Demo Class
// ─────────────────────────────────────────────────────────────────────────────

class VFXViewportDemo final : public DemoApplication {
public:
    /// Constructs the VFX viewport demo with the given configuration.
    explicit VFXViewportDemo(const DemoConfig& config);
    ~VFXViewportDemo() override = default;

    /// Sets file paths for PBR textures to load at init time.
    /// Empty paths are ignored (material preset color tints are used instead).
    void SetTexturePaths(const std::string& albedo,
                         const std::string& normal,
                         const std::string& roughness);

    /// Sets a custom sprite sheet path (TGA/PNG) for flipbook particles.
    /// Overrides the procedural fire sprite for all fire presets.
    void SetSpritePath(const std::string& path) { m_customSpritePath = path; }

protected:
    void OnInit() override;
    void OnFrame(float dt) override;
    void OnResize(uint32_t width, uint32_t height) override;
    void OnShutdown() override;

private:
    // ── Procedural meshes ────────────────────────────────────────────────
    static constexpr int kMeshCount = 5;
    std::array<ProceduralMesh, kMeshCount> m_meshes;
    int m_currentMesh = 0;

    // ── Per-mesh transform state (needed for gizmo manipulation) ──────
    /// Stores position, rotation, and scale for each mesh slot.
    struct MeshInstance {
        Vec3 position{};
        Quat rotation = Quat::Identity();
        Vec3 scale{1.0f, 1.0f, 1.0f};
    };
    std::array<MeshInstance, kMeshCount> m_meshTransforms;
    uint32_t m_selectedMeshIndex = UINT32_MAX; ///< Index into m_meshes, UINT32_MAX = nothing selected.

    // ── Outline pass ──────────────────────────────────────────────────
    OutlinePass m_outlinePass;
    bool m_outlineEnabled = true; ///< O key toggles.

    // ── Gizmo renderer + interaction ──────────────────────────────────
    GizmoRenderer   m_gizmoRenderer;
    GizmoInteraction m_gizmoInteraction;
    GizmoMode  m_gizmoMode  = GizmoMode::Translate;
    GizmoSpace m_gizmoSpace = GizmoSpace::World;

    // ── Material presets ─────────────────────────────────────────────────
    int m_currentMaterial = 0;

    // ── Shadow map ──────────────────────────────────────────────────────
    static constexpr uint32_t kShadowMapSize = 2048;
    RHITextureHandle m_shadowMap;

    // ── HDR render target ───────────────────────────────────────────────
    RHITextureHandle m_hdrTarget;

    // ── Depth buffer ────────────────────────────────────────────────────
    RHITextureHandle m_depthBuffer;

    // ── Scene state ─────────────────────────────────────────────────────
    float m_sunElevation = 45.0f;      ///< Sun elevation in degrees.
    float m_shadowBias = 0.002f;       ///< Shadow mapping depth bias.
    float m_exposure = 1.0f;           ///< HDR exposure multiplier.
    float m_vignetteIntensity = 0.4f;  ///< Vignette effect strength.
    bool  m_postProcessEnabled = true; ///< ACES tonemap enabled (P key toggles).
    bool  m_showShadowDebug = false;   ///< Shadow map debug overlay (G key toggles).
    float m_totalTime = 0.0f;          ///< Accumulated elapsed time.

    // ── Bloom pass ─────────────────────────────────────────────────────
    BloomPass m_bloomPass;             ///< GPU compute bloom (threshold + downsample + upsample).
    bool      m_bloomEnabled = true;   ///< Bloom toggle (B key).

    // ── VFX particle system ─────────────────────────────────────────────
    VFXParticleSystem m_vfxSystem;
    bool  m_vfxEnabled = true;
    float m_vfxSpawnRate = 500.0f;
    float m_vfxSpawnAccum = 0.0f;
    int   m_vfxPresetIndex = 0;
    VFXEmitterConfig m_vfxEmitter;
    Mat4  m_prevViewProj;
    bool  m_vfxBurstMode = true;        ///< true = single burst, false = continuous emission.
    uint32_t m_vfxBurstCount = 500;     ///< Number of particles per burst.
    bool  m_vfxBurstPending = true;     ///< true = fire burst on next frame.
    VFXMaterial m_vfxMaterial;           ///< Material for custom sprite (--sprite CLI).

    // ── Predefined VFX material presets ─────────────────────────────────
    static constexpr int kVFXMaterialCount = 6;
    VFXMaterial m_vfxMaterials[kVFXMaterialCount]; ///< Preset VFX materials (M key cycles).
    int m_currentMaterialIndex = 0;                ///< Active material preset index.

    // ── Fireball projectile ─────────────────────────────────────────────
    bool  m_fireballActive = false;     ///< true = fireball projectile is flying.
    Vec3  m_fireballPos = {};           ///< Current fireball world position.
    Vec3  m_fireballVel = {};           ///< Fireball velocity (units/sec).
    float m_fireballLife = 0.0f;        ///< Time alive (sec). Dies after max lifetime.
    static constexpr float kFireballSpeed = 15.0f;    ///< Projectile speed (units/sec).
    static constexpr float kFireballMaxLife = 4.0f;   ///< Max fireball duration (sec).

    // ── Trail / ribbon system ────────────────────────────────────────────
    TrailSystem m_trailSystem;                                ///< CPU trail simulation and mesh generation.
    TrailSystem::TrailHandle m_trailHandle = TrailSystem::kInvalidHandle; ///< Handle for the demo orbit trail.
    RHIPipelineHandle m_trailPSO;                             ///< Trail ribbon graphics PSO.
    RHIBufferHandle   m_trailVB;                              ///< StructuredBuffer for trail vertices (bindless SRV).
    RHIBufferHandle   m_trailIB;                              ///< StructuredBuffer<uint> for trail indices (bindless SRV).
    uint32_t          m_trailVBSrvIndex = UINT32_MAX;         ///< Bindless SRV index for trail vertex buffer.
    uint32_t          m_trailIBSrvIndex = UINT32_MAX;         ///< Bindless SRV index for trail index buffer.
    static constexpr uint32_t kMaxTrailVertices = 1024;       ///< 512 points * 2 verts per point.
    static constexpr uint32_t kMaxTrailIndices  = 3072;       ///< 511 segments * 6 indices per segment.
    bool m_trailEnabled = true;                               ///< Trail toggle (Y key).

    // ── Decal projection system ─────────────────────────────────────────────
    RHIPipelineHandle m_decalPSO;                            ///< Forward decal graphics PSO.
    bool m_decalEnabled = true;                              ///< Decal toggle (D key).
    static constexpr uint32_t kMaxDecals = 4;                ///< Max demo decals.
    struct DemoDecal {
        Vec3 position;         ///< World-space center of the decal OBB.
        Vec3 halfExtents;      ///< Half-extents in cm (XYZ).
        float opacity = 1.0f;  ///< Decal opacity [0, 1].
    };
    std::array<DemoDecal, kMaxDecals> m_decals;
    uint32_t m_activeDecalCount = 0;

    // ── Distortion pass ──────────────────────────────────────────────────
    RHITextureHandle  m_distortionRT;              ///< RGBA16Float distortion accumulation buffer.
    RHITextureHandle  m_preDistortionCopy;         ///< Copy of HDR before distortion resolve.
    RHIPipelineHandle m_distortionHazePSO;         ///< Fullscreen heat-haze accumulation PSO.
    RHIPipelineHandle m_distortionResolvePSO;      ///< Fullscreen distortion resolve PSO.
    bool m_distortionEnabled = true;               ///< Distortion toggle (X key).
    float m_distortionStrength = 0.5f;             ///< Heat-haze strength [0, 1].

    // ── Point lights ───────────────────────────────────────────────────
    bool m_pointLightsEnabled = false;             ///< Point light toggle (L key).

    // ── Environment rotation ───────────────────────────────────────────
    float m_envRotation = 0.0f;                    ///< Environment (sun/sky) Y-axis rotation in degrees (R key).

    // ── Object rotation (Alt + LMB drag) ──────────────────────────────
    float m_objectYaw   = 0.0f;                    ///< Object Y-axis rotation in degrees (Alt+LMB horizontal drag).
    float m_objectPitch = 0.0f;                    ///< Object X-axis rotation in degrees (Alt+LMB vertical drag).

    // ── Background presets (C key cycles) ──────────────────────────────
    int m_bgPreset = 2;                            ///< Background mode: 0=Sky, 1=Dark Gray, 2=Black, 3=White, 4=Neutral Gray.
    static constexpr int kBgPresetCount = 5;

    // ── Loaded PBR textures (from --albedo/--normal/--roughness CLI) ────
    std::string m_albedoPath;
    std::string m_normalPath;
    std::string m_roughnessPath;
    RHITextureHandle m_albedoTex;
    RHITextureHandle m_normalTex;
    RHITextureHandle m_roughnessTex;
    uint32_t m_albedoSrvIndex  = UINT32_MAX;  ///< Bindless SRV index for loaded albedo texture.
    uint32_t m_normalSrvIndex  = UINT32_MAX;  ///< Bindless SRV index for loaded normal texture.
    uint32_t m_roughnessSrvIndex = UINT32_MAX; ///< Bindless SRV index for loaded roughness texture.

    // ── Custom sprite sheet (loaded from file) ────────────────────────
    std::string m_customSpritePath;
    RHITextureHandle m_customSpriteTex;
    uint32_t m_customSpriteSrvIdx = UINT32_MAX;

    // ── Procedural sprite sheet textures for flipbook animation ────────
    RHITextureHandle m_fireSpriteTex;           ///< 256x256 RGBA8 4x4 fire sprite sheet.
    uint32_t m_fireSpriteSrvIdx = UINT32_MAX;   ///< Bindless SRV index for fire sprite sheet.
    RHITextureHandle m_smokeSpriteTex;          ///< 256x256 RGBA8 4x4 smoke sprite sheet.
    uint32_t m_smokeSpriteSrvIdx = UINT32_MAX;  ///< Bindless SRV index for smoke sprite sheet.

    // ── Sky dummy LUTs (1x1 textures for ray march fallback) ──────────
    RHITextureHandle m_dummyTransmittanceLut;   ///< 1x1 white RGBA16F (transmittance=1, no extinction).
    RHITextureHandle m_dummyMultiScatterLut;    ///< 1x1 black R11G11B10F (no multi-scatter contribution).

    // ── Pipeline handles ────────────────────────────────────────────────
    RHIPipelineHandle m_skyPSO;
    RHIPipelineHandle m_tonemapPSO;

    // ── Internal methods ────────────────────────────────────────────────
    void CreateSizeDependent(uint32_t width, uint32_t height);
    void DestroySizeDependent();
    void HandleInput(float dt);
    void RenderShadowPass(IRHICommandList* cmd);
    void RenderSkyPass(IRHICommandList* cmd, uint64_t perFrameGpu);
    void RenderScenePass(IRHICommandList* cmd, uint64_t perFrameGpu);
    void RenderVFXPass(IRHICommandList* cmd, uint64_t perFrameGpu, float dt);
    void RenderTrailPass(IRHICommandList* cmd, uint64_t perFrameGpu);
    void RenderDecalPass(IRHICommandList* cmd, uint64_t perFrameGpu);
    void RenderDistortionPass(IRHICommandList* cmd, uint64_t perFrameGpu);
    void RenderTonemapPass(IRHICommandList* cmd, uint64_t perFrameGpu);
    void RenderOutlinePass(IRHICommandList* cmd, const CBAllocation& perFrameAlloc);
    void RenderGizmoPass(IRHICommandList* cmd);

    /// Builds the world matrix for the mesh at the given transform index.
    Mat4 BuildMeshWorldMatrix(uint32_t meshIndex) const;

    /// Generates a 4x4 procedural fire sprite sheet (256x256 RGBA8).
    /// Each frame shows a soft radial glow transitioning from yellow-white
    /// (birth) through orange to dim red (death).
    static void GenerateFireSpriteSheet(std::vector<uint32_t>& pixels,
                                        uint32_t cellSize, uint32_t rows, uint32_t cols);

    /// Generates a 4x4 procedural smoke sprite sheet (256x256 RGBA8).
    /// Each frame shows a soft puff transitioning from small dense white
    /// (birth) to large diffuse transparent gray (death).
    static void GenerateSmokeSpriteSheet(std::vector<uint32_t>& pixels,
                                         uint32_t cellSize, uint32_t rows, uint32_t cols);

    /// Returns the sprite sheet SRV index for the current VFX preset.
    /// Fire-like presets get the fire sprite, smoke gets smoke, others get UINT32_MAX.
    uint32_t GetSpriteTextureForPreset(int presetIndex) const;
};

} // namespace uz
