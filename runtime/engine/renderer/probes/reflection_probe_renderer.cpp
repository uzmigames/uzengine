/// UzEngine Reflection Probe Renderer — Implementation.
///
/// Gathers ReflectionProbeComponent entities from the ECS, captures dirty
/// probes by rendering the scene into 6 cubemap faces, bakes IBL via
/// IBLBaker, and uploads a StructuredBuffer<ReflectionProbeGPU> for the
/// deferred lighting shader.
///
/// @ref Lagarde 2012 "Local Image-based Lighting With Parallax-corrected Cubemaps"
/// @ref Akenine-Moller 2018 "Real-Time Rendering 4th Ed" Ch.10.5 — cubemap capture

#include "reflection_probe_renderer.h"

#include "constant_buffers.h"
#include "cb_ring_allocator.h"
#include "draw_list.h"
#include "gpu_mesh.h"
#include "ibl_baker.h"
#include "root_slots.h"
#include "world.h"
#include "reflection_probe_component.h"
#include "scene_component.h"
#include "rhi.h"
#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

using namespace DirectX;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Cubemap Face Camera Matrices
// ─────────────────────────────────────────────────────────────────────────────

/// Helper: converts a Z-up Vec3 to a Y-up XMVECTOR.
/// Matches the swap in Camera::UpdateView: Z-up(x,y,z) -> Y-up(y,z,x).
static XMVECTOR ToYUpXM(const Vec3& v) {
    return XMVectorSet(v.y, v.z, v.x, 0.0f);
}

CubemapFaceMatrices BuildCubemapFaceMatrices(
    const Vec3& worldPos, float nearPlane, float farPlane)
{
    CubemapFaceMatrices result;

    // Cubemap face directions in UzEngine Z-up coordinates.
    // DX convention: face order is +X, -X, +Y, -Y, +Z, -Z.
    //
    // The mapping from DX Y-up axes to UzEngine Z-up axes is:
    //   DX +X -> UZ +Y (right), DX -X -> UZ -Y (left)
    //   DX +Y -> UZ +Z (up),    DX -Y -> UZ -Z (down)
    //   DX +Z -> UZ +X (fwd),   DX -Z -> UZ -X (back)
    //
    // @ref Akenine-Moller 2018 "Real-Time Rendering 4th Ed" Ch.10.5
    struct FaceDir {
        Vec3 forward;
        Vec3 up;
    };

    static constexpr FaceDir kFaceDirs[kCubeFaceCount] = {
        { { 0.0f,  1.0f,  0.0f}, { 0.0f,  0.0f,  1.0f} },  // Face 0: +X DX -> +Y UZ, up=+Z
        { { 0.0f, -1.0f,  0.0f}, { 0.0f,  0.0f,  1.0f} },  // Face 1: -X DX -> -Y UZ, up=+Z
        { { 0.0f,  0.0f,  1.0f}, {-1.0f,  0.0f,  0.0f} },  // Face 2: +Y DX -> +Z UZ, up=-X
        { { 0.0f,  0.0f, -1.0f}, { 1.0f,  0.0f,  0.0f} },  // Face 3: -Y DX -> -Z UZ, up=+X
        { { 1.0f,  0.0f,  0.0f}, { 0.0f,  0.0f,  1.0f} },  // Face 4: +Z DX -> +X UZ, up=+Z
        { {-1.0f,  0.0f,  0.0f}, { 0.0f,  0.0f,  1.0f} },  // Face 5: -Z DX -> -X UZ, up=+Z
    };

    // 90-degree FOV, 1:1 aspect ratio, with the engine's reversed-Z projection.
    // XM_PIDIV2 = pi/2 radians = 90 degrees.
    const XMMATRIX proj = Mat4::Perspective(XM_PIDIV2, 1.0f, nearPlane, farPlane).toXM();

    // The same swap matrix used by Camera::UpdateView to convert Z-up to Y-up.
    // Row-vector convention: world(x,y,z) -> Y-up(y,z,x).
    const XMMATRIX swap = XMMatrixSet(
        0, 0, 1, 0,  // Row 0: world X -> Y-up Z (depth)
        1, 0, 0, 0,  // Row 1: world Y -> Y-up X (screen right)
        0, 1, 0, 0,  // Row 2: world Z -> Y-up Y (screen up)
        0, 0, 0, 1   // Row 3: W
    );

    const XMVECTOR eyeYUp = ToYUpXM(worldPos);

    for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
        const Vec3& fwd = kFaceDirs[face].forward;
        const Vec3& up  = kFaceDirs[face].up;

        // Convert face forward/up from Z-up to Y-up for DirectXMath
        const XMVECTOR fwdYUp = ToYUpXM(fwd);
        const XMVECTOR upYUp  = ToYUpXM(up);

        // LookToLH builds a view matrix from eye position, look direction, and up
        const XMMATRIX lookTo = XMMatrixLookToLH(eyeYUp, fwdYUp, upYUp);

        // Combined view: swap * lookTo (same formula as Camera::UpdateView)
        const XMMATRIX view = XMMatrixMultiply(swap, lookTo);

        result.view[face]     = Mat4(view);
        result.viewProj[face] = Mat4(XMMatrixMultiply(view, proj));
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Capture sun shadow cascades (phase3a 3.37.0, item 1.10)
// ─────────────────────────────────────────────────────────────────────────────

/// The per-cascade CB of shadow_depth_vs.hlsl (80 bytes; local to the shadow pass translation
/// units as well). The light position / radius are the local-light tiles'; zero for a sun cascade.
struct CaptureShadowCB {
    Mat4  viewProj;
    float lightPosX = 0.f, lightPosY = 0.f, lightPosZ = 0.f;
    float lightRadius = 0.f;
};

/// Up vector of the light view that is not parallel to the light direction (shadow_cascade.cpp's
/// ChooseLightUp: Z-up, X when the sun is within ~8 degrees of vertical).
static Vec3 ChooseCaptureLightUp(const Vec3& lightDir) {
    const Vec3 worldUp{0.0f, 0.0f, 1.0f};
    return (std::abs(lightDir.Dot(worldUp)) > 0.99f) ? Vec3{1.0f, 0.0f, 0.0f} : worldUp;
}

std::array<ProbeShadowCascade, kCaptureShadowCascades> BuildProbeShadowCascades(
    const Vec3& probePos, float influenceRadius, const Vec3& boxExtents, const Vec3& sunDir,
    uint32_t resolution)
{
    std::array<ProbeShadowCascade, kCaptureShadowCascades> out{};

    // Cascade 0 covers the probe's region: the influence sphere and the proxy box (its corner
    // distance), never under a metre; cascade 1 the surroundings.
    const bool hasBox = boxExtents.x > 0.0f && boxExtents.y > 0.0f && boxExtents.z > 0.0f;
    float r0 = std::max(influenceRadius, 100.0f);
    if (hasBox) {
        r0 = std::max(r0, boxExtents.Length());
    }
    const float r1 = std::min(std::max(r0 * kCaptureShadowFarScale, r0), kCaptureShadowFarMax);
    const float radii[kCaptureShadowCascades] = { r0, r1 };

    // The light shines along -sunDir (sunDir points toward the sun, PerFrameCB::lightDir).
    Vec3 lightShine = -sunDir;
    if (lightShine.Length() < 1e-6f) {
        lightShine = Vec3{0.0f, 0.0f, -1.0f};
    }
    lightShine = lightShine.Normalized();
    const Vec3 lightUp = ChooseCaptureLightUp(lightShine);

    // The frame CSM's sphere-bounded orthographic volume (shadow_cascade.cpp, step 3d-3g): the eye
    // pulled back kCasterPullbackScale radii toward the sun so the casters between the sun and the
    // sphere are rasterised; standard-Z over [0, pullback + radius]; the frame's bias rules with
    // CSMDepthBias 10, a 2 cm normal bias floored at 1.5 texels and a fully world-scaled depth bias.
    constexpr float kCasterPullbackScale = 8.0f;
    constexpr float kCsmDepthBias        = 10.0f;
    constexpr float kNormalBiasCm        = 2.0f;
    const float res = static_cast<float>(std::max(resolution, 1u));

    for (uint32_t c = 0; c < kCaptureShadowCascades; ++c) {
        const float radius   = radii[c];
        const float pullback = radius * kCasterPullbackScale;
        const Vec3  lightEye = probePos - lightShine * pullback;
        const Mat4  lightView = Mat4::LookAt(lightEye, probePos, lightUp);
        const float orthoFar = pullback + radius;
        const Mat4  lightProj{XMMatrixOrthographicOffCenterLH(-radius, radius, -radius, radius, 0.0f, orthoFar)};

        const float texel        = (2.0f * radius) / res;
        const float constantBias = kCsmDepthBias / orthoFar;

        ProbeShadowCascade& cascade = out[c];
        cascade.viewProj   = lightView * lightProj;
        cascade.radius     = radius;
        cascade.texelSize  = texel;
        cascade.depthRange = orthoFar;
        cascade.depthBias  = constantBias * texel;
        cascade.normalBias = std::max(kNormalBiasCm, texel * 1.5f);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

ReflectionProbeRenderer::~ReflectionProbeRenderer() {
    if (m_initialized) {
        Shutdown();
    }
}

ReflectionProbeRenderer::ReflectionProbeRenderer(ReflectionProbeRenderer&& other) noexcept
    : m_device(other.m_device)
    , m_iblBaker(other.m_iblBaker)
    , m_initialized(other.m_initialized)
    , m_captureDepth(other.m_captureDepth)
    , m_captureDepthSrv(other.m_captureDepthSrv)
    , m_captureDepthResolution(other.m_captureDepthResolution)
    , m_captureShadowAtlas(other.m_captureShadowAtlas)
    , m_captureShadowAtlasSrv(other.m_captureShadowAtlasSrv)
    , m_captureShadowIsSrv(other.m_captureShadowIsSrv)
    , m_probeBuffer(other.m_probeBuffer)
    , m_probeBufferSrvIndex(other.m_probeBufferSrvIndex)
{
    for (uint32_t i = 0; i < kMaxProbes; ++i) {
        m_probes[i] = other.m_probes[i];
    }

    other.m_device = nullptr;
    other.m_iblBaker = nullptr;
    other.m_initialized = false;
    other.m_captureDepth = RHITextureHandle{};
    other.m_captureDepthSrv = UINT32_MAX;
    other.m_captureDepthResolution = 0;
    other.m_captureShadowAtlas = RHITextureHandle{};
    other.m_captureShadowAtlasSrv = UINT32_MAX;
    other.m_captureShadowIsSrv = false;
    other.m_probeBuffer = RHIBufferHandle{};
    other.m_probeBufferSrvIndex = UINT32_MAX;
    for (uint32_t i = 0; i < kMaxProbes; ++i) {
        other.m_probes[i] = {};
    }
}

ReflectionProbeRenderer& ReflectionProbeRenderer::operator=(ReflectionProbeRenderer&& other) noexcept {
    if (this != &other) {
        if (m_initialized) {
            Shutdown();
        }

        m_device = other.m_device;
        m_iblBaker = other.m_iblBaker;
        m_initialized = other.m_initialized;
        m_captureDepth = other.m_captureDepth;
        m_captureDepthSrv = other.m_captureDepthSrv;
        m_captureDepthResolution = other.m_captureDepthResolution;
        m_captureShadowAtlas = other.m_captureShadowAtlas;
        m_captureShadowAtlasSrv = other.m_captureShadowAtlasSrv;
        m_captureShadowIsSrv = other.m_captureShadowIsSrv;
        m_probeBuffer = other.m_probeBuffer;
        m_probeBufferSrvIndex = other.m_probeBufferSrvIndex;
        for (uint32_t i = 0; i < kMaxProbes; ++i) {
            m_probes[i] = other.m_probes[i];
        }

        other.m_device = nullptr;
        other.m_iblBaker = nullptr;
        other.m_initialized = false;
        other.m_captureDepth = RHITextureHandle{};
        other.m_captureDepthSrv = UINT32_MAX;
        other.m_captureDepthResolution = 0;
        other.m_captureShadowAtlas = RHITextureHandle{};
        other.m_captureShadowAtlasSrv = UINT32_MAX;
        other.m_captureShadowIsSrv = false;
        other.m_probeBuffer = RHIBufferHandle{};
        other.m_probeBufferSrvIndex = UINT32_MAX;
        for (uint32_t i = 0; i < kMaxProbes; ++i) {
            other.m_probes[i] = {};
        }
    }
    return *this;
}

bool ReflectionProbeRenderer::Init(IRHIDevice* device, IBLBaker* iblBaker) {
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "ReflectionProbeRenderer::Init called while already initialized");
        return true;
    }

    if (!device) {
        UZ_LOG(Renderer, Error, "ReflectionProbeRenderer::Init failed: device is null");
        return false;
    }
    if (!iblBaker) {
        UZ_LOG(Renderer, Error, "ReflectionProbeRenderer::Init failed: iblBaker is null");
        return false;
    }

    m_device = device;
    m_iblBaker = iblBaker;

    // ── Create StructuredBuffer for probe GPU data ────────────────────────
    // kMaxProbes * sizeof(ReflectionProbeGPU) = 8 * 32 = 256 bytes.
    // Upload heap for CPU-writable, GPU-readable access each frame.
    RHIBufferDesc bufDesc{};
    bufDesc.sizeBytes = kMaxProbes * sizeof(ReflectionProbeGPU);
    bufDesc.stride    = sizeof(ReflectionProbeGPU);
    bufDesc.usage     = RHIBufferUsage::StructuredBuffer;
    bufDesc.memory    = RHIMemoryType::Upload;
    bufDesc.debugName = "ReflectionProbeBuffer";

    m_probeBuffer = device->CreateBuffer(bufDesc);
    if (!m_probeBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "ReflectionProbeRenderer: failed to create probe buffer ({} bytes)",
               bufDesc.sizeBytes);
        return false;
    }

    m_probeBufferSrvIndex = device->GetBufferSRVIndex(m_probeBuffer);
    if (m_probeBufferSrvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "ReflectionProbeRenderer: probe buffer has no SRV");
        device->DestroyBuffer(m_probeBuffer);
        m_probeBuffer = RHIBufferHandle{};
        return false;
    }

    m_initialized = true;

    UZ_LOG(Renderer, Info, "ReflectionProbeRenderer initialized (maxProbes={}, bufferSRV={})",
           kMaxProbes, m_probeBufferSrvIndex);
    return true;
}

void ReflectionProbeRenderer::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Destroy per-probe capture cubemaps
    for (uint32_t i = 0; i < kMaxProbes; ++i) {
        DestroyCaptureResources(i);
    }

    // Destroy shared capture depth buffer
    if (m_captureDepth.IsValid()) {
        m_device->DestroyTexture(m_captureDepth);
        m_captureDepth = RHITextureHandle{};
    }
    m_captureDepthSrv = UINT32_MAX;
    m_captureDepthResolution = 0;

    // Destroy the capture sun shadow atlas
    if (m_captureShadowAtlas.IsValid()) {
        m_device->DestroyTexture(m_captureShadowAtlas);
        m_captureShadowAtlas = RHITextureHandle{};
    }
    m_captureShadowAtlasSrv = UINT32_MAX;
    m_captureShadowIsSrv = false;

    // Destroy the structured buffer
    if (m_probeBuffer.IsValid()) {
        m_device->DestroyBuffer(m_probeBuffer);
        m_probeBuffer = RHIBufferHandle{};
    }

    // Destroy probe-owned IBL textures and clear slots
    for (uint32_t i = 0; i < kMaxProbes; ++i) {
        if (m_probes[i].irradianceTexture.IsValid()) {
            m_device->DestroyTexture(m_probes[i].irradianceTexture);
        }
        if (m_probes[i].prefilteredTexture.IsValid()) {
            m_device->DestroyTexture(m_probes[i].prefilteredTexture);
        }
        m_probes[i] = {};
    }

    m_probeBufferSrvIndex = UINT32_MAX;
    m_device = nullptr;
    m_iblBaker = nullptr;
    m_initialized = false;

    UZ_LOG(Renderer, Info, "ReflectionProbeRenderer shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Update — per-frame gather, capture, and upload
// ─────────────────────────────────────────────────────────────────────────────

ProbeFrameResult ReflectionProbeRenderer::Update(
    World& world,
    uint32_t skyCubemapSrvIndex,
    RHITextureHandle skyCubemapTexture)
{
    ProbeFrameResult result;

    if (!m_initialized) {
        return result;
    }

    // Skip update if the sky cubemap is not yet available (first frames): the
    // captures draw it behind the geometry, so nothing can have been captured.
    // The texture handle itself is not read here (the bake source is the
    // slot's scene capture); it stays in the signature with the SRV index.
    if (skyCubemapSrvIndex == UINT32_MAX) {
        return result;
    }
    static_cast<void>(skyCubemapTexture);

    if (!m_iblBaker || !m_iblBaker->IsInitialized()) {
        return result;
    }

    auto& registry = world.GetRegistry();

    // ── Gather enabled probes sorted by priority ─────────────────────────
    // We need both ReflectionProbeComponent (for probe params) and
    // SceneComponent (for world position). Entities missing SceneComponent
    // are silently skipped.
    struct ProbeEntry {
        entt::entity entity;
        int32_t      priority;
        bool         needsCapture;
    };

    std::vector<ProbeEntry> entries;
    entries.reserve(kMaxProbes);

    auto probeView = registry.view<ReflectionProbeComponent, SceneComponent>();
    for (auto [enttEntity, probeComp, sceneComp] : probeView.each()) {
        if (!probeComp.enabled) {
            continue;
        }
        entries.push_back({enttEntity, probeComp.priority, probeComp.needsCapture});
    }

    // Sort by priority (highest first) — higher priority probes are preferred
    // when the shader iterates the probe buffer.
    std::sort(entries.begin(), entries.end(),
              [](const ProbeEntry& a, const ProbeEntry& b) {
                  return a.priority > b.priority;
              });

    // Clamp to kMaxProbes
    const uint32_t activeCount = static_cast<uint32_t>(
        std::min(entries.size(), static_cast<size_t>(kMaxProbes)));

    // ── Mark unused slots as inactive ────────────────────────────────────
    for (uint32_t i = activeCount; i < kMaxProbes; ++i) {
        m_probes[i].active = false;
        m_probes[i].captured = false;
    }

    // ── Update active slots and capture one dirty probe per frame ────────
    bool capturedThisFrame = false;

    for (uint32_t i = 0; i < activeCount; ++i) {
        auto& slot = m_probes[i];
        auto& entry = entries[i];

        slot.active = true;

        // Check if this probe needs (re)capture
        bool needsBake = entry.needsCapture || !slot.captured;

        // A recorded scene capture sits on a frame's command list that may not have been
        // submitted yet; the bake (its own list, waited synchronously) would read the
        // cubemap before it was rendered. Advance the phase and bake on the next Update(),
        // by which time that list has been submitted ahead of the bake's.
        if (slot.capturePhase == CapturePhase::Recorded) {
            slot.capturePhase = CapturePhase::Executed;
            continue;
        }
        // The bake reads the slot's executed scene capture and nothing else. The faces are
        // rendered with the camera's Z-up -> Y-up rotation (BuildCubemapFaceMatrices), so a
        // probe cubemap is a rotation of world space and the lighting shader looks it up as
        // (R.y, R.z, R.x); the sky cubemap follows the sky convention (R.x, R.z, R.y), and
        // baking it into a probe would hand the shader a reflected sky. A probe without an
        // executed capture (no sky cubemap yet, capture resources not created, another probe
        // took this frame's capture) waits for CaptureIfDirty and contributes nothing meanwhile.
        const bool hasCapture = slot.captureCubemap.IsValid() && slot.captureCubemapSrv != UINT32_MAX;
        if (needsBake && (!hasCapture || slot.capturePhase != CapturePhase::Executed)) {
            continue;
        }

        if (needsBake && !capturedThisFrame) {
            const uint32_t         sourceSrv = slot.captureCubemapSrv;
            const RHITextureHandle sourceTex = slot.captureCubemap;

            // Destroy previous probe IBL textures before re-baking (caller-owned)
            if (slot.irradianceTexture.IsValid()) {
                m_device->DestroyTexture(slot.irradianceTexture);
            }
            if (slot.prefilteredTexture.IsValid()) {
                m_device->DestroyTexture(slot.prefilteredTexture);
            }

            auto bakeResult = m_iblBaker->BakeEnvironment(sourceSrv, sourceTex);

            if (bakeResult.irradianceSrvIndex != UINT32_MAX &&
                bakeResult.prefilteredSrvIndex != UINT32_MAX) {

                slot.irradianceSrvIndex   = bakeResult.irradianceSrvIndex;
                slot.prefilteredSrvIndex  = bakeResult.prefilteredSrvIndex;
                slot.irradianceTexture    = bakeResult.irradianceTexture;
                slot.prefilteredTexture   = bakeResult.prefilteredTexture;
                slot.captured = true;
                slot.capturePhase = CapturePhase::Idle;

                // Clear the dirty flag on the ECS component
                auto& probeComp = registry.get<ReflectionProbeComponent>(entry.entity);
                probeComp.needsCapture = false;

                // Store the baked SRV indices back on the component for external queries
                probeComp.irradianceSrvIndex  = bakeResult.irradianceSrvIndex;
                probeComp.prefilteredSrvIndex = bakeResult.prefilteredSrvIndex;

                UZ_LOG(Renderer, Info,
                       "ReflectionProbeRenderer: baked probe {} from its scene capture (irrad={}, prefilt={})",
                       i, bakeResult.irradianceSrvIndex, bakeResult.prefilteredSrvIndex);
            } else {
                UZ_LOG(Renderer, Warning,
                       "ReflectionProbeRenderer: IBL bake failed for probe {}", i);
                slot.capturePhase = CapturePhase::Idle;
            }

            capturedThisFrame = true;
        }
    }

    // ── Upload probe data to the structured buffer ───────────────────────
    result.probeCount = UploadProbeData(world);
    result.probeBufferSrvIndex = (result.probeCount > 0) ? m_probeBufferSrvIndex : UINT32_MAX;

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// UploadProbeData — fills the GPU StructuredBuffer with active probe data
// ─────────────────────────────────────────────────────────────────────────────

uint32_t ReflectionProbeRenderer::UploadProbeData(World& world) {
    auto* mapped = static_cast<ReflectionProbeGPU*>(m_device->MapBuffer(m_probeBuffer));
    if (!mapped) {
        UZ_LOG(Renderer, Error, "ReflectionProbeRenderer: failed to map probe buffer");
        return 0;
    }

    auto& registry = world.GetRegistry();
    uint32_t written = 0;

    // Re-gather probes in priority order (same order as Update tracking).
    // This is lightweight — we just iterate the same view.
    struct SortEntry {
        entt::entity entity;
        int32_t      priority;
    };

    std::vector<SortEntry> entries;
    entries.reserve(kMaxProbes);

    auto probeView = registry.view<ReflectionProbeComponent, SceneComponent>();
    for (auto [enttEntity, probeComp, sceneComp] : probeView.each()) {
        if (!probeComp.enabled) {
            continue;
        }
        entries.push_back({enttEntity, probeComp.priority});
    }

    std::sort(entries.begin(), entries.end(),
              [](const SortEntry& a, const SortEntry& b) {
                  return a.priority > b.priority;
              });

    const uint32_t count = static_cast<uint32_t>(
        std::min(entries.size(), static_cast<size_t>(kMaxProbes)));

    for (uint32_t i = 0; i < count; ++i) {
        const auto& slot = m_probes[i];
        if (!slot.active || !slot.captured) {
            continue;
        }

        const auto& probeComp = registry.get<ReflectionProbeComponent>(entries[i].entity);
        const auto& sceneComp = registry.get<SceneComponent>(entries[i].entity);
        const Vec3 worldPos = sceneComp.GetWorldPosition();

        ReflectionProbeGPU& gpu = mapped[written];
        gpu.posX              = worldPos.x;
        gpu.posY              = worldPos.y;
        gpu.posZ              = worldPos.z;
        gpu.influenceRadius   = probeComp.influenceRadius;
        gpu.blendDistance     = probeComp.blendDistance;
        // Parallax proxy box (0 = none). Negative authored values are treated as 0.
        gpu.boxHalfX          = std::max(probeComp.boxExtents.x, 0.0f);
        gpu.boxHalfY          = std::max(probeComp.boxExtents.y, 0.0f);
        gpu.boxHalfZ          = std::max(probeComp.boxExtents.z, 0.0f);
        gpu.irradianceSrvIndex  = slot.irradianceSrvIndex;
        gpu.prefilteredSrvIndex = slot.prefilteredSrvIndex;
        gpu.prefilteredMipLevels = IBLBaker::kPrefilteredMipLevels;
        gpu._pad0             = 0;

        ++written;
    }

    // Zero out remaining slots to prevent stale GPU reads
    for (uint32_t i = written; i < kMaxProbes; ++i) {
        std::memset(&mapped[i], 0, sizeof(ReflectionProbeGPU));
    }

    m_device->UnmapBuffer(m_probeBuffer);

    return written;
}

// ─────────────────────────────────────────────────────────────────────────────
// Capture Resources — per-probe cubemap and shared depth buffer
// ─────────────────────────────────────────────────────────────────────────────

bool ReflectionProbeRenderer::EnsureCaptureResources(uint32_t slotIndex, uint32_t resolution) {
    UZ_CHECK(slotIndex < kMaxProbes);
    UZ_CHECK(resolution > 0);

    auto& slot = m_probes[slotIndex];

    // Already allocated at the correct resolution — nothing to do
    if (slot.captureCubemap.IsValid() && slot.captureResolution == resolution) {
        return true;
    }

    // Resolution changed or first allocation — destroy old resources
    DestroyCaptureResources(slotIndex);

    // ── Create RGBA16F cubemap (Texture2DArray with 6 slices) ────────────
    // Usage: RenderTarget (written by capture pass) + ShaderResource (read by IBLBaker).
    // Created in the Common state; the first capture transitions it to RenderTarget.
    RHITextureDesc cubeDesc{};
    cubeDesc.width           = resolution;
    cubeDesc.height          = resolution;
    cubeDesc.depthOrArraySize = static_cast<uint16_t>(kCubeFaceCount);
    cubeDesc.mipLevels       = 1;
    cubeDesc.format          = RHIFormat::RGBA16Float;
    cubeDesc.usage           = RHITextureUsage::RenderTarget | RHITextureUsage::ShaderResource;
    cubeDesc.debugName       = "ProbeCaptureCubemap";

    slot.captureCubemap = m_device->CreateTexture(cubeDesc);
    if (!slot.captureCubemap.IsValid()) {
        UZ_LOG(Renderer, Error,
               "ReflectionProbeRenderer: failed to create capture cubemap for slot {} ({}x{}x6)",
               slotIndex, resolution, resolution);
        return false;
    }

    slot.captureCubemapSrv = m_device->GetTextureSRVIndex(slot.captureCubemap);
    if (slot.captureCubemapSrv == UINT32_MAX) {
        UZ_LOG(Renderer, Error,
               "ReflectionProbeRenderer: capture cubemap for slot {} has no SRV", slotIndex);
        m_device->DestroyTexture(slot.captureCubemap);
        slot.captureCubemap = RHITextureHandle{};
        return false;
    }

    slot.captureResolution = resolution;
    slot.captureIsSrv = false;
    slot.capturePhase = CapturePhase::Idle;

    // ── Ensure shared depth buffer matches this resolution ───────────────
    // If the depth buffer exists at a different resolution, recreate it.
    // Since we capture one probe per frame and faces are sequential, one
    // shared depth buffer is sufficient. ShaderResource usage: the sky pass
    // reads it to fill the pixels no geometry covered (depth == far).
    if (!m_captureDepth.IsValid() || m_captureDepthResolution != resolution) {
        if (m_captureDepth.IsValid()) {
            m_device->DestroyTexture(m_captureDepth);
            m_captureDepthSrv = UINT32_MAX;
        }

        RHITextureDesc depthDesc{};
        depthDesc.width  = resolution;
        depthDesc.height = resolution;
        depthDesc.format = RHIFormat::D32Float;
        depthDesc.usage  = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
        depthDesc.debugName = "ProbeCaptureDepth";

        m_captureDepth = m_device->CreateTexture(depthDesc);
        if (!m_captureDepth.IsValid()) {
            UZ_LOG(Renderer, Error,
                   "ReflectionProbeRenderer: failed to create capture depth ({}x{})",
                   resolution, resolution);
            // Clean up the cubemap we just created
            m_device->DestroyTexture(slot.captureCubemap);
            slot.captureCubemap = RHITextureHandle{};
            slot.captureCubemapSrv = UINT32_MAX;
            slot.captureResolution = 0;
            return false;
        }

        m_captureDepthSrv        = m_device->GetTextureSRVIndex(m_captureDepth);
        m_captureDepthResolution = resolution;
    }

    UZ_LOG(Renderer, Info,
           "ReflectionProbeRenderer: created capture cubemap for slot {} ({}x{}x6 RGBA16F, depthSRV={})",
           slotIndex, resolution, resolution, m_captureDepthSrv);
    return true;
}

void ReflectionProbeRenderer::DestroyCaptureResources(uint32_t slotIndex) {
    UZ_CHECK(slotIndex < kMaxProbes);

    auto& slot = m_probes[slotIndex];

    if (slot.captureCubemap.IsValid()) {
        m_device->DestroyTexture(slot.captureCubemap);
        slot.captureCubemap = RHITextureHandle{};
    }
    slot.captureCubemapSrv = UINT32_MAX;
    slot.captureResolution = 0;
    slot.captureIsSrv = false;
    slot.capturePhase = CapturePhase::Idle;
}

// ─────────────────────────────────────────────────────────────────────────────
// RenderProbeCubemap — renders scene into 6 cubemap faces
// ─────────────────────────────────────────────────────────────────────────────

bool ReflectionProbeRenderer::EnsureCaptureShadowResources() {
    if (m_captureShadowAtlas.IsValid()) {
        return true;
    }
    // Square atlas: shadow.hlsli's tile math is (1 / columns) in both directions, so the two
    // cascades sit in the top row of a 2 x 2 grid (the frame's 4-cascade layout).
    constexpr uint32_t kColumns = 2;
    RHITextureDesc atlasDesc{};
    atlasDesc.width     = kCaptureShadowResolution * kColumns;
    atlasDesc.height    = kCaptureShadowResolution * kColumns;
    atlasDesc.format    = RHIFormat::D32Float;
    atlasDesc.usage     = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
    atlasDesc.debugName = "ProbeCaptureShadowAtlas";
    m_captureShadowAtlas = m_device->CreateTexture(atlasDesc);
    if (!m_captureShadowAtlas.IsValid()) {
        UZ_LOG(Renderer, Error, "ReflectionProbeRenderer: failed to create the capture shadow atlas ({}x{})",
               atlasDesc.width, atlasDesc.height);
        return false;
    }
    m_captureShadowAtlasSrv = m_device->GetTextureSRVIndex(m_captureShadowAtlas);
    if (m_captureShadowAtlasSrv == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "ReflectionProbeRenderer: the capture shadow atlas has no SRV");
        m_device->DestroyTexture(m_captureShadowAtlas);
        m_captureShadowAtlas = RHITextureHandle{};
        return false;
    }
    m_captureShadowIsSrv = false;
    UZ_LOG(Renderer, Info, "ReflectionProbeRenderer: created the capture shadow atlas ({}x{} D32F, SRV {})",
           atlasDesc.width, atlasDesc.height, m_captureShadowAtlasSrv);
    return true;
}

CBAllocation ReflectionProbeRenderer::RenderCaptureShadowAtlas(
    const Vec3& probePos, float influenceRadius, const Vec3& boxExtents,
    const ProbeCaptureSceneDesc& sceneDesc)
{
    UZ_PROFILE_SCOPE("ReflectionProbe::RenderCaptureShadowAtlas");
    auto* cmd = sceneDesc.cmdList;

    // The CB the faces sample through (shadow.hlsli, CascadeShadow): no cascade until the atlas
    // below is rendered, so a capture without a sun, a shadow pipeline or an atlas is unshadowed.
    ShadowDataCB shadowData{};
    shadowData.shadowAtlasIndex = UINT32_MAX;
    shadowData.cascadeCount     = 0;
    shadowData.shadowMapSize    = static_cast<float>(kCaptureShadowResolution);
    shadowData.receiverBias     = 1.0f;
    shadowData.atlasColumns     = 2;

    const Vec3 sunDir = (sceneDesc.frameCB != nullptr)
        ? Vec3{sceneDesc.frameCB->lightDir.x, sceneDesc.frameCB->lightDir.y, sceneDesc.frameCB->lightDir.z}
        : Vec3{0.0f, 0.0f, 0.0f};
    const bool canShadow = sceneDesc.shadowPipeline.IsValid() && sunDir.Length() > 1e-6f
                        && sceneDesc.drawList != nullptr && EnsureCaptureShadowResources();

    if (canShadow) {
        const auto cascades = BuildProbeShadowCascades(probePos, influenceRadius, boxExtents, sunDir,
                                                       kCaptureShadowResolution);
        for (uint32_t c = 0; c < kCaptureShadowCascades; ++c) {
            shadowData.cascadeViewProj[c]   = cascades[c].viewProj;
            shadowData.cascadeSplits[c]     = cascades[c].radius;
            // Blend into the next cascade over the outer 10 % of this one (the frame's transition fraction).
            shadowData.cascadeFadeStarts[c] = cascades[c].radius * 0.9f;
            shadowData.cascadeDepthBias[c]  = cascades[c].depthBias;
            shadowData.cascadeNormalBias[c] = cascades[c].normalBias;
        }
        shadowData.shadowAtlasIndex = m_captureShadowAtlasSrv;
        shadowData.cascadeCount     = kCaptureShadowCascades;
        // No distance fade (shadowFadeEnd <= shadowFadeStart): a receiver no cascade covers is lit.

        // Render the cascades: atlas to depth-write, clear, one depth-only pass per cascade tile.
        {
            RHIBarrierDesc toDepth{};
            toDepth.texture     = m_captureShadowAtlas;
            toDepth.stateBefore = m_captureShadowIsSrv ? RHIResourceState::ShaderResource : RHIResourceState::Common;
            toDepth.stateAfter  = RHIResourceState::DepthStencilWrite;
            cmd->ResourceBarrier(&toDepth, 1);
        }
        cmd->ClearDepthStencil(m_captureShadowAtlas, kShadowDepthClear, 0);

        const auto& commands = sceneDesc.drawList->GetCommands();
        for (uint32_t c = 0; c < kCaptureShadowCascades; ++c) {
            const uint32_t tileX = (c % 2) * kCaptureShadowResolution;
            const uint32_t tileY = (c / 2) * kCaptureShadowResolution;
            RHIViewport vp{};
            vp.x = static_cast<float>(tileX);
            vp.y = static_cast<float>(tileY);
            vp.width    = static_cast<float>(kCaptureShadowResolution);
            vp.height   = static_cast<float>(kCaptureShadowResolution);
            vp.maxDepth = 1.0f;
            RHIScissorRect sc{};
            sc.left   = static_cast<int32_t>(tileX);
            sc.top    = static_cast<int32_t>(tileY);
            sc.right  = static_cast<int32_t>(tileX + kCaptureShadowResolution);
            sc.bottom = static_cast<int32_t>(tileY + kCaptureShadowResolution);

            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount = 0;
            passDesc.hasDepth = true;
            passDesc.depthAttachment.texture    = m_captureShadowAtlas;
            passDesc.depthAttachment.clearDepth = kShadowDepthClear;
            passDesc.depthAttachment.clear      = false;   // cleared above
            cmd->BeginRenderPass(passDesc);
            cmd->SetPipeline(sceneDesc.shadowPipeline);
            cmd->SetViewport(vp);
            cmd->SetScissor(sc);

            CaptureShadowCB cascadeCb{};
            cascadeCb.viewProj = cascades[c].viewProj;
            CBAllocation cascadeAlloc = sceneDesc.cbAllocator->Allocate(sizeof(CaptureShadowCB));
            if (cascadeAlloc.cpuPtr == nullptr) {
                UZ_LOG(Renderer, Error, "ReflectionProbeRenderer: CB ring exhausted (capture shadow cascade {})", c);
                cmd->EndRenderPass();
                break;
            }
            std::memcpy(cascadeAlloc.cpuPtr, &cascadeCb, sizeof(CaptureShadowCB));
            cmd->SetRootCBV(kSlotShadow, cascadeAlloc.bufferHandle, cascadeAlloc.offsetBytes,
                            cascadeAlloc.cpuPtr, sizeof(CaptureShadowCB));

            uint32_t lastBoundMeshId = UINT32_MAX;
            for (const auto& drawCmd : commands) {
                // Translucent / additive materials and mesh decals are not occluders (the frame's shadow pass).
                if ((drawCmd.materialFlags & 0x3u) >= 2u || (drawCmd.materialFlags & 0x10u) != 0u) continue;
                if (drawCmd.meshId >= sceneDesc.meshCount) continue;
                const GPUMesh& mesh = sceneDesc.meshes[drawCmd.meshId];
                if (!mesh.IsValid()) continue;

                if (drawCmd.meshId != lastBoundMeshId) {
                    if (sceneDesc.bindless) {
                        cmd->SetDescriptorIndex(kSlotRootConstants, mesh.vbSrvIndex);
                    } else {
                        cmd->SetVertexBuffer(0, mesh.vertexBuffer, 0);
                    }
                    cmd->SetIndexBuffer(mesh.indexBuffer, mesh.indexFormat, 0);
                    lastBoundMeshId = drawCmd.meshId;
                }

                PerObjectCB perObject{};
                perObject.model = drawCmd.worldMatrix;
                CBAllocation objAlloc = sceneDesc.cbAllocator->Allocate(sizeof(PerObjectCB));
                if (objAlloc.cpuPtr == nullptr) {
                    UZ_LOG(Renderer, Error, "ReflectionProbeRenderer: CB ring exhausted (capture shadow draw)");
                    break;
                }
                std::memcpy(objAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));
                cmd->SetRootCBV(kSlotPerObject, objAlloc.bufferHandle, objAlloc.offsetBytes,
                                objAlloc.cpuPtr, sizeof(PerObjectCB));
                if (drawCmd.lodIndexCount > 0) {
                    cmd->DrawIndexed(drawCmd.lodIndexCount, 1, drawCmd.lodIndexByteOffset / 4);
                } else {
                    cmd->DrawIndexed(mesh.indexCount);
                }
            }
            cmd->EndRenderPass();
        }

        {
            RHIBarrierDesc toSrv{};
            toSrv.texture     = m_captureShadowAtlas;
            toSrv.stateBefore = RHIResourceState::DepthStencilWrite;
            toSrv.stateAfter  = RHIResourceState::ShaderResource;
            cmd->ResourceBarrier(&toSrv, 1);
        }
        m_captureShadowIsSrv = true;
    }

    CBAllocation shadowAlloc = sceneDesc.cbAllocator->Allocate(sizeof(ShadowDataCB));
    if (shadowAlloc.cpuPtr == nullptr) {
        UZ_LOG(Renderer, Error, "ReflectionProbeRenderer: CB ring exhausted (capture ShadowDataCB)");
        return shadowAlloc;
    }
    std::memcpy(shadowAlloc.cpuPtr, &shadowData, sizeof(ShadowDataCB));
    return shadowAlloc;
}

void ReflectionProbeRenderer::RenderProbeCubemap(
    uint32_t slotIndex, const Vec3& worldPos, float influenceRadius, const Vec3& boxExtents,
    const ProbeCaptureSceneDesc& sceneDesc,
    uint32_t skyCubemapSrvIndex, RHITextureHandle skyCubemapTexture)
{
    UZ_PROFILE_SCOPE("ReflectionProbe::RenderCubemap");
    UZ_CHECK(slotIndex < kMaxProbes);

    auto& slot = m_probes[slotIndex];
    UZ_CHECK(slot.captureCubemap.IsValid());
    UZ_CHECK(sceneDesc.cmdList != nullptr);
    UZ_CHECK(sceneDesc.cbAllocator != nullptr);

    auto* cmd = sceneDesc.cmdList;
    const uint32_t resolution = slot.captureResolution;

    // The capture's own sun shadow cascades, and the ShadowDataCB the faces sample them through.
    const CBAllocation shadowAlloc = RenderCaptureShadowAtlas(worldPos, influenceRadius, boxExtents, sceneDesc);
    if (shadowAlloc.cpuPtr == nullptr) {
        return;   // CB ring exhausted (logged): the probe stays dirty and is captured on a later frame
    }

    // Build 6 view-projection matrices for this probe position
    const CubemapFaceMatrices faceMatrices = BuildCubemapFaceMatrices(worldPos);

    // The cubemap is created in Common and left in ShaderResource after a capture
    // (IBLBaker reads it there); render-target writes need an explicit transition.
    {
        RHIBarrierDesc toRt{};
        toRt.texture     = slot.captureCubemap;
        toRt.stateBefore = slot.captureIsSrv ? RHIResourceState::ShaderResource : RHIResourceState::Common;
        toRt.stateAfter  = RHIResourceState::RenderTarget;
        cmd->ResourceBarrier(&toRt, 1);
    }

    // The sky pass needs the skybox pipeline, the face depth as an SRV and the sky cubemap.
    const bool drawSky = sceneDesc.skyboxPipeline.IsValid()
                      && m_captureDepthSrv != UINT32_MAX
                      && skyCubemapSrvIndex != UINT32_MAX;

    // Viewport and scissor for the capture resolution
    RHIViewport vp{};
    vp.width    = static_cast<float>(resolution);
    vp.height   = static_cast<float>(resolution);
    vp.maxDepth = 1.0f;

    RHIScissorRect sc{};
    sc.right  = static_cast<int32_t>(resolution);
    sc.bottom = static_cast<int32_t>(resolution);

    // Render each face as a separate render pass targeting one array slice.
    // The forward PBR pipeline is used with a per-face PerFrameCB that
    // overrides the viewProj and cameraPos fields.
    for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
        // ── Begin render pass targeting this cubemap face ─────────────────
        RHIRenderPassDesc passDesc{};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments[0].texture    = slot.captureCubemap;
        passDesc.colorAttachments[0].arraySlice = face;
        passDesc.colorAttachments[0].mipLevel   = 0;
        passDesc.colorAttachments[0].clear      = true;
        passDesc.colorAttachments[0].clearColor[0] = 0.0f;
        passDesc.colorAttachments[0].clearColor[1] = 0.0f;
        passDesc.colorAttachments[0].clearColor[2] = 0.0f;
        passDesc.colorAttachments[0].clearColor[3] = 1.0f;
        passDesc.hasDepth = true;
        passDesc.depthAttachment.texture    = m_captureDepth;
        passDesc.depthAttachment.clearDepth = kDepthFar;
        passDesc.depthAttachment.clear      = true;

        cmd->BeginRenderPass(passDesc);

        // ── Bind pipeline and viewport ───────────────────────────────────
        cmd->SetPipeline(sceneDesc.capturePipeline);
        cmd->SetViewport(vp);
        cmd->SetScissor(sc);
        // The capture's shadow cascades (slot 3) for probe_capture_ps.hlsl; SM5 binds the atlas at t7.
        cmd->SetRootCBV(kSlotShadow, shadowAlloc.bufferHandle, shadowAlloc.offsetBytes,
                        shadowAlloc.cpuPtr, sizeof(ShadowDataCB));
        if (!sceneDesc.bindless && m_captureShadowAtlas.IsValid()) {
            cmd->SetFragmentTexture(7, m_captureShadowAtlas);
        }

        // ── Allocate and upload per-frame CB with this face's viewProj ───
        // Start from the frame's own PerFrameCB so the forward shader sees the
        // sun direction / colour and the ambient term (a zero-initialised CB
        // lights nothing: the faces came out black), then override the camera
        // fields with this face's view-projection and the probe position.
        PerFrameCB probeFrameCB{};
        if (sceneDesc.frameCB != nullptr) {
            probeFrameCB = *sceneDesc.frameCB;
        }
        probeFrameCB.viewProj    = faceMatrices.viewProj[face];
        probeFrameCB.invViewProj = faceMatrices.viewProj[face].Inverse();
        probeFrameCB.cameraPos   = Vec4(worldPos, 1.0f);
        probeFrameCB.nearPlane   = 10.0f;
        probeFrameCB.farPlane    = 100000.0f;

        CBAllocation frameAlloc = sceneDesc.cbAllocator->Allocate(sizeof(PerFrameCB));
        if (frameAlloc.cpuPtr == nullptr) {
            UZ_LOG(Renderer, Error,
                   "ReflectionProbeRenderer: CB ring exhausted during probe capture (face {})", face);
            cmd->EndRenderPass();
            break;
        }
        std::memcpy(frameAlloc.cpuPtr, &probeFrameCB, sizeof(PerFrameCB));
        cmd->SetRootCBV(kSlotPerFrame, frameAlloc.bufferHandle, frameAlloc.offsetBytes,
                        frameAlloc.cpuPtr, sizeof(PerFrameCB));

        // ── Draw scene meshes ────────────────────────────────────────────
        if (sceneDesc.drawList != nullptr) {
            const auto& commands = sceneDesc.drawList->GetCommands();
            uint32_t lastBoundMeshId = UINT32_MAX;

            for (const auto& drawCmd : commands) {
                // The opaque geometry the G-buffer pass draws: translucent / additive materials
                // (blend mode bits[0:1] >= 2) and mesh decals (bit 4) are not drawn as opaque walls.
                if ((drawCmd.materialFlags & 0x3u) >= 2u || (drawCmd.materialFlags & 0x10u) != 0u) {
                    continue;
                }
                if (drawCmd.meshId >= sceneDesc.meshCount) {
                    continue;
                }
                const GPUMesh& mesh = sceneDesc.meshes[drawCmd.meshId];
                if (!mesh.IsValid()) {
                    continue;
                }

                // Rebind mesh buffers only when the mesh changes
                if (drawCmd.meshId != lastBoundMeshId) {
                    if (sceneDesc.bindless) {
                        const uint32_t pushData[4] = {
                            mesh.vbSrvIndex, (mesh.vertexStride == 32) ? 1u : 0u,
                            mesh.colorSrvIndex, 0u
                        };
                        cmd->SetRootConstants(kSlotRootConstants, pushData, 4);
                    } else {
                        cmd->SetVertexBuffer(0, mesh.vertexBuffer, 0);
                    }
                    cmd->SetIndexBuffer(mesh.indexBuffer, mesh.indexFormat, 0);
                    lastBoundMeshId = drawCmd.meshId;
                }

                // Allocate per-object CB with world/normal matrices and material data
                PerObjectCB perObject{};
                perObject.model        = drawCmd.worldMatrix;
                perObject.normalMatrix = drawCmd.normalMatrix;
                perObject.albedoIndex    = drawCmd.albedoSrvIndex;
                perObject.normalIndex    = drawCmd.normalSrvIndex;
                perObject.roughnessIndex = drawCmd.roughnessSrvIndex;
                perObject.metallicIndex  = drawCmd.metallicSrvIndex;
                perObject.aoIndex        = drawCmd.aoSrvIndex;
                perObject.baseColorTintR      = drawCmd.baseColorTintR;
                perObject.baseColorTintG      = drawCmd.baseColorTintG;
                perObject.baseColorTintB      = drawCmd.baseColorTintB;
                perObject.roughnessMultiplier = drawCmd.roughnessMultiplier;
                perObject.metallicMultiplier  = drawCmd.metallicMultiplier;
                perObject.normalIntensity     = drawCmd.normalIntensity;
                perObject.uvScaleX            = drawCmd.uvScaleX;
                perObject.uvScaleY            = drawCmd.uvScaleY;
                perObject.uvOffsetX           = drawCmd.uvOffsetX;
                perObject.uvOffsetY           = drawCmd.uvOffsetY;
                perObject.aoIntensity         = drawCmd.aoIntensity;
                perObject.opacity             = drawCmd.opacity;
                perObject.materialFlags       = drawCmd.materialFlags;
                perObject.alphaClipThreshold  = drawCmd.alphaClipThreshold;

                CBAllocation objAlloc = sceneDesc.cbAllocator->Allocate(sizeof(PerObjectCB));
                if (objAlloc.cpuPtr == nullptr) {
                    UZ_LOG(Renderer, Error,
                           "ReflectionProbeRenderer: CB ring exhausted during probe draw (face {})", face);
                    break;
                }
                std::memcpy(objAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));
                cmd->SetRootCBV(kSlotPerObject, objAlloc.bufferHandle, objAlloc.offsetBytes,
                                objAlloc.cpuPtr, sizeof(PerObjectCB));

                if (drawCmd.lodIndexCount > 0) {
                    cmd->DrawIndexed(drawCmd.lodIndexCount, 1, drawCmd.lodIndexByteOffset / 4);
                } else {
                    cmd->DrawIndexed(mesh.indexCount);
                }
            }
        }

        cmd->EndRenderPass();

        if (!drawSky) {
            continue;
        }

        // ── Sky pass: the sky cubemap behind the geometry ─────────────────
        // skybox_cubemap_ps fills the pixels whose depth is the far plane
        // (reversed-Z 0) from the sky cubemap along invViewProj, so it reads
        // this face's depth as an SRV: no depth attachment on this pass.
        {
            RHIBarrierDesc depthToSrv{};
            depthToSrv.texture     = m_captureDepth;
            depthToSrv.stateBefore = RHIResourceState::DepthStencilWrite;
            depthToSrv.stateAfter  = RHIResourceState::ShaderResource;
            cmd->ResourceBarrier(&depthToSrv, 1);
        }

        RHIRenderPassDesc skyPass{};
        skyPass.colorAttachmentCount = 1;
        skyPass.colorAttachments[0].texture    = slot.captureCubemap;
        skyPass.colorAttachments[0].arraySlice = face;
        skyPass.colorAttachments[0].mipLevel   = 0;
        skyPass.colorAttachments[0].clear      = false;
        skyPass.hasDepth = false;

        cmd->BeginRenderPass(skyPass);
        cmd->SetPipeline(sceneDesc.skyboxPipeline);
        cmd->SetViewport(vp);
        cmd->SetScissor(sc);
        cmd->SetRootCBV(kSlotPerFrame, frameAlloc.bufferHandle, frameAlloc.offsetBytes,
                        frameAlloc.cpuPtr, sizeof(PerFrameCB));
        if (sceneDesc.bindless) {
            const uint32_t skyIndices[2] = { m_captureDepthSrv, skyCubemapSrvIndex };
            cmd->SetRootConstants(kSlotRootConstants, skyIndices, 2);
        } else {
            cmd->SetFragmentTexture(0, m_captureDepth);
            cmd->SetFragmentTexture(1, skyCubemapTexture);
        }
        cmd->Draw(3);
        cmd->EndRenderPass();

        {
            RHIBarrierDesc depthToWrite{};
            depthToWrite.texture     = m_captureDepth;
            depthToWrite.stateBefore = RHIResourceState::ShaderResource;
            depthToWrite.stateAfter  = RHIResourceState::DepthStencilWrite;
            cmd->ResourceBarrier(&depthToWrite, 1);
        }
    }

    // Transition cubemap from RenderTarget to ShaderResource so IBLBaker can read it
    RHIBarrierDesc barrier{};
    barrier.texture     = slot.captureCubemap;
    barrier.stateBefore = RHIResourceState::RenderTarget;
    barrier.stateAfter  = RHIResourceState::ShaderResource;
    cmd->ResourceBarrier(&barrier, 1);
    slot.captureIsSrv = true;
    slot.capturePhase = CapturePhase::Recorded;
}

// ─────────────────────────────────────────────────────────────────────────────
// CaptureIfDirty — finds the highest-priority dirty probe and captures it
// ─────────────────────────────────────────────────────────────────────────────

void ReflectionProbeRenderer::CaptureIfDirty(
    World& world, const ProbeCaptureSceneDesc& sceneDesc,
    uint32_t skyCubemapSrvIndex, RHITextureHandle skyCubemapTexture)
{
    UZ_PROFILE_SCOPE("ReflectionProbe::CaptureIfDirty");

    if (!m_initialized) {
        return;
    }

    // Skip if no command list provided — caller doesn't want scene capture
    if (sceneDesc.cmdList == nullptr || sceneDesc.cbAllocator == nullptr) {
        return;
    }

    // The faces draw the sky cubemap behind the geometry, and Update() bakes
    // nothing until it exists: capture only once the sky is ready.
    if (skyCubemapSrvIndex == UINT32_MAX) {
        return;
    }

    auto& registry = world.GetRegistry();

    // Gather enabled probes sorted by priority (same gathering as Update)
    struct ProbeEntry {
        entt::entity entity;
        int32_t      priority;
        bool         needsCapture;
        Vec3         worldPos;
        uint32_t     resolution;
        float        influenceRadius;   ///< With boxExtents: the region the capture's shadow cascade 0 covers.
        Vec3         boxExtents;
    };

    std::vector<ProbeEntry> entries;
    entries.reserve(kMaxProbes);

    auto probeView = registry.view<ReflectionProbeComponent, SceneComponent>();
    for (auto [enttEntity, probeComp, sceneComp] : probeView.each()) {
        if (!probeComp.enabled) {
            continue;
        }
        entries.push_back({
            enttEntity,
            probeComp.priority,
            probeComp.needsCapture,
            sceneComp.GetWorldPosition(),
            probeComp.resolution,
            probeComp.influenceRadius,
            probeComp.boxExtents
        });
    }

    std::sort(entries.begin(), entries.end(),
              [](const ProbeEntry& a, const ProbeEntry& b) {
                  return a.priority > b.priority;
              });

    const uint32_t activeCount = static_cast<uint32_t>(
        std::min(entries.size(), static_cast<size_t>(kMaxProbes)));

    // Find the first dirty probe and capture it
    for (uint32_t i = 0; i < activeCount; ++i) {
        auto& slot = m_probes[i];
        const auto& entry = entries[i];

        const bool needsCapture = entry.needsCapture || !slot.captured;
        if (!needsCapture) {
            continue;
        }
        // A capture already recorded for this slot is waiting for its bake.
        if (slot.capturePhase != CapturePhase::Idle) {
            continue;
        }

        // Ensure capture resources exist at the correct resolution
        if (!EnsureCaptureResources(i, entry.resolution)) {
            UZ_LOG(Renderer, Warning,
                   "ReflectionProbeRenderer: failed to create capture resources for probe {}", i);
            continue;
        }

        // Render the scene into this probe's cubemap (its own sun shadow cascades first)
        RenderProbeCubemap(i, entry.worldPos, entry.influenceRadius, entry.boxExtents, sceneDesc,
                           skyCubemapSrvIndex, skyCubemapTexture);

        UZ_LOG(Renderer, Info,
               "ReflectionProbeRenderer: captured scene for probe {} at ({:.1f}, {:.1f}, {:.1f}), res={}",
               i, entry.worldPos.x, entry.worldPos.y, entry.worldPos.z, entry.resolution);

        // Only capture one probe per frame to amortize GPU cost
        break;
    }
}

} // namespace uz
