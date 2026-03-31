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
/// - [F10] VFX parameter panel (keyboard-navigable emitter config editor)
/// - [Q]   VFX module stack panel (simulation module visibility and toggle UI)
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
#include "engine/vfx/curves/vfx_color_ramp.h"
#include "outline_pass.h"
#include "gizmo_renderer.h"
#include "gizmo_interaction.h"
#include "editor_input_handler.h"
#include "vfx_parameter_panel.h"
#include "vfx_module_stack.h"
#include "vfx_editor_ui.h"

#include "vfx_preset_serializer.h"

#include <array>
#include <cstdint>
#include <memory>
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
// Multi-Emitter Instance
// ─────────────────────────────────────────────────────────────────────────────

/// Encapsulates a single emitter within a compound VFX effect.
///
/// Each instance owns an independent VFXParticleSystem (GPU particle pool)
/// and its own VFXEmitterConfig. Multiple instances combine to form compound
/// effects like Fire (flame + smoke + sparks) or Explosion (flash + debris +
/// shockwave). Analogous to Niagara's Emitter within a System.
///
/// Uses unique_ptr for the particle system because VFXParticleSystem has
/// deleted copy/move constructors (it manages GPU handles). This allows
/// VFXEmitterInstance to be movable for std::vector reallocation.
struct VFXEmitterInstance {
    std::string                          name;                        ///< Display name for the UI.
    std::unique_ptr<VFXParticleSystem>   system;                     ///< Independent GPU particle pool.
    VFXEmitterConfig                     config;                      ///< Per-emitter simulation/render config.
    float                                spawnRate      = 300.0f;     ///< Particles per second (continuous mode).
    float                                spawnAccum     = 0.0f;       ///< Sub-frame spawn accumulator.
    bool                                 enabled        = true;       ///< Per-emitter enable/disable toggle.
    bool                                 burstMode      = true;       ///< true = single burst, false = continuous.
    bool                                 burstPending   = true;       ///< true = fire burst on next frame.
    uint32_t                             burstCount     = 300;        ///< Number of particles per burst.
    Vec3                                 positionOffset = {0, 0, 0};  ///< Offset from the shared emitter origin.
    uint32_t                             poolSize       = 10000;      ///< Max particles in this emitter's pool.

    VFXEmitterInstance() : system(std::make_unique<VFXParticleSystem>()) {}
    ~VFXEmitterInstance() = default;
    VFXEmitterInstance(VFXEmitterInstance&&) noexcept = default;
    VFXEmitterInstance& operator=(VFXEmitterInstance&&) noexcept = default;
};

/// Identifies a compound VFX preset (multiple emitters forming one effect).
enum class VFXCompoundPreset : int {
    Single    = 0,  ///< Single emitter — backward-compatible with legacy behavior.
    Fire      = 1,  ///< Flame core + smoke + sparks (3 emitters).
    Explosion = 2,  ///< Flash + debris + shockwave smoke (3 emitters).
    Magic     = 3,  ///< Core glow + orbiting particles + dust (3 emitters).
    Count
};

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
    bool  m_meshVisible = false;       ///< Show/hide the emitter mesh (sphere). Off by default.
    float m_totalTime = 0.0f;          ///< Accumulated elapsed time.

    // ── Bloom pass ─────────────────────────────────────────────────────
    BloomPass m_bloomPass;             ///< GPU compute bloom (threshold + downsample + upsample).
    bool      m_bloomEnabled = true;   ///< Bloom toggle (B key).

    // ── Multi-emitter VFX system ────────────────────────────────────────
    static constexpr int kMaxEmitters = 8;         ///< Hard limit on simultaneous emitters.
    std::vector<VFXEmitterInstance> m_emitters;     ///< Active emitter instances.
    int  m_activeEmitterIndex = 0;                 ///< Which emitter the UI edits.
    bool m_vfxEnabled = true;                      ///< Global VFX enable/disable.
    int  m_vfxPresetIndex = 0;                     ///< File-based preset index.
    VFXCompoundPreset m_compoundPreset = VFXCompoundPreset::Single; ///< Active compound preset.
    Mat4 m_prevViewProj;

    // ── File-based preset list (loaded/refreshed via ScanDirectory) ─────────
    std::vector<std::string> m_presetFiles;  ///< Sorted absolute paths to .json preset files.
    std::vector<std::string> m_presetNames;  ///< Filename stems for display (no dir, no .json).
    float m_saveHintTimer        = 0.0f;     ///< Countdown (s) for "Saved preset..." hint display.
    bool  m_deleteConfirmPending = false;    ///< True while waiting for second Delete keypress.
    int   m_deleteConfirmIdx     = -1;       ///< Preset index targeted by the pending delete.
    float m_deleteConfirmTimer   = 0.0f;     ///< Countdown (s) for delete confirmation window.
    // Burst/spawn state moved into VFXEmitterInstance. These aliases point to
    // the active emitter for backward-compatible keyboard/UI controls.
    // ── Predefined VFX material presets ─────────────────────────────────
    static constexpr int kVFXMaterialCount = 6;
    VFXMaterial m_vfxMaterials[kVFXMaterialCount]; ///< Preset VFX materials (M key cycles).
    int m_currentMaterialIndex = 0;                ///< Active material preset index.

    // ── Color ramp LUT presets (N key cycles) ──────────────────────────
    static constexpr int kColorRampCount = 3;         ///< Fire, Ice, Rainbow.
    VFXColorRamp m_colorRamps[kColorRampCount];        ///< Predefined color ramp gradients.
    int m_currentRampIndex = -1;                       ///< -1 = legacy lerp, 0-2 = ramp index.

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
    bool m_trailEnabled = false;                              ///< Trail toggle (Y key). Off by default.

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
    bool m_distortionEnabled = false;              ///< Distortion toggle (X key). Off by default.
    float m_distortionStrength = 0.5f;             ///< Heat-haze strength [0, 1].

    // ── Point lights ───────────────────────────────────────────────────
    // ── Vortex force state (F key cycles modes) ─────────────────────
    /// @ref Stomakhin 2013 "A Material Point Method for Snow Simulation" App.A
    int m_vortexMode = 0;  ///< 0=Off, 1=Tornado, 2=Whirlpool. F key cycles.

    // ── Wind zone state (N key cycles modes) ──────────────────────
    /// @ref Stam 1999 "Stable Fluids" -- wind zone accumulation
    int       m_windMode       = 0;    ///< 0=Off, 1=Ambient, 2=Gust. N key cycles.
    WindZone  m_windZones[kMaxWindZones]      = {};  ///< Active wind zone descriptors.
    uint32_t  m_windZoneCount                 = 0u; ///< Number of active wind zones this frame.

    // ── Kill volume state (K key cycles modes) ─────────────────────
    /// @ref Ericson 2005 "Real-Time Collision Detection" Ch.4
    int        m_killVolumeMode = 0;   ///< 0=Off, 1=InvWall, 2=Ground, 3=Sphere. K key cycles.
    KillVolume m_killVolumes[kMaxKillVolumes]  = {};  ///< Active kill volume descriptors.
    uint32_t   m_killVolumeCount               = 0u; ///< Number of active kill volumes this frame.

    bool m_pointLightsEnabled = false;             ///< Point light toggle (L key).

    // ── Sub-emitter state (Task 3.87.0 — CPU-side simplified sub-emitter) ──
    /// When enabled, each primary burst fires a secondary "sparks" burst after
    /// kSubEmitterDelay seconds using a separate emitter configuration. This is
    /// a pure CPU-side implementation: no GPU readback, no death-event flags.
    /// The secondary burst is dispatched as an independent DispatchCompute call
    /// on the same VFXParticleSystem with a temporary emitter config override.
    bool  m_subEmitterEnabled = false;   ///< E key (no selection) toggles sub-emitter mode.
    float m_subEmitterTimer   = -1.0f;   ///< Countdown (s). < 0 = inactive. Fires when it crosses 0.
    static constexpr float kSubEmitterDelay  = 0.4f;  ///< Delay (s) from primary burst to secondary.
    uint32_t m_subEmitterCount = 100;    ///< Number of secondary "sparks" particles per burst.
    VFXEmitterConfig m_subEmitterConfig; ///< Secondary burst emitter config (orange sparks).

    // ── Frustum culling state (Task 3.89.0) ─────────────────────────────────
    bool     m_frustumCullingEnabled = false; ///< Ctrl+F toggles CPU frustum cull check.
    uint32_t m_culledFrameCount = 0;          ///< Frames where VFX was frustum-culled.

    // ── VFX parameter panel (F10 key toggles) ───────────────────────────────
    VFXParameterPanel m_paramPanel;           ///< Keyboard-navigable emitter parameter editor.

    // ── VFX module stack panel (Q key toggles) ───────────────────────────────
    VFXModuleStackPanel m_moduleStack;        ///< Console-based simulation module visibility and toggle UI.

    // ── VFX editor UI (mouse-driven visual overlay) ─────────────────────────
    VFXEditorUI m_editorUI;                   ///< Clickable menu bar, panels, sliders, buttons.

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

    /// Loads the preset at `idx` from `m_presetFiles` into the live emitter config.
    /// Clamps idx to valid range; logs a warning and returns if out of range.
    void LoadPreset(int idx);

    /// Exports all built-in presets to JSON files in `dir` (skips existing files)
    /// and then rescans the directory to populate `m_presetFiles`/`m_presetNames`.
    ///
    /// # Arguments
    /// * `dir` — Relative path to the presets directory (e.g. "content/vfx/presets/").
    void RefreshPresetList(const std::string& dir);

    /// Creates compound emitter configurations for the given preset type.
    /// Clears existing emitters and populates m_emitters with the new set.
    /// Does NOT call Init/UploadInitialFreeList — caller must do that.
    void BuildCompoundPreset(VFXCompoundPreset preset);

    /// Initializes all emitter GPU resources and uploads initial free lists.
    /// Requires device + command list in recording state.
    void InitAllEmitters(IRHIDevice* device, ShaderManager* shaders,
                         IRHICommandList* cmd);

    /// Shuts down all emitter GPU resources.
    void ShutdownAllEmitters(IRHIDevice* device);

    /// Resets all emitters: uploads initial free lists and arms bursts.
    void ResetAllEmitters(IRHICommandList* cmd);

    /// Returns a mutable reference to the currently selected emitter.
    /// Clamps m_activeEmitterIndex to a valid range.
    VFXEmitterInstance& ActiveEmitter();

    /// Returns a const reference to the currently selected emitter.
    const VFXEmitterInstance& ActiveEmitter() const;
};

} // namespace uz
