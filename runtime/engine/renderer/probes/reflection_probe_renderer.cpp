/// UzEngine Reflection Probe Renderer — Implementation.
///
/// Gathers ReflectionProbeComponent entities from the ECS, bakes dirty probes
/// via IBLBaker (one per frame), and uploads a StructuredBuffer<ReflectionProbeGPU>
/// for the deferred lighting shader.

#include "reflection_probe_renderer.h"

#include "constant_buffers.h"
#include "ibl_baker.h"
#include "world.h"
#include "reflection_probe_component.h"
#include "scene_component.h"
#include "rhi.h"
#include "log.h"

#include <algorithm>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

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
    , m_probeBuffer(other.m_probeBuffer)
    , m_probeBufferSrvIndex(other.m_probeBufferSrvIndex)
{
    for (uint32_t i = 0; i < kMaxProbes; ++i) {
        m_probes[i] = other.m_probes[i];
    }

    other.m_device = nullptr;
    other.m_iblBaker = nullptr;
    other.m_initialized = false;
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
        m_probeBuffer = other.m_probeBuffer;
        m_probeBufferSrvIndex = other.m_probeBufferSrvIndex;
        for (uint32_t i = 0; i < kMaxProbes; ++i) {
            m_probes[i] = other.m_probes[i];
        }

        other.m_device = nullptr;
        other.m_iblBaker = nullptr;
        other.m_initialized = false;
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

    // Destroy the structured buffer
    if (m_probeBuffer.IsValid()) {
        m_device->DestroyBuffer(m_probeBuffer);
        m_probeBuffer = RHIBufferHandle{};
    }

    // Clear all probe slots (IBL textures are owned by IBLBaker, not us)
    for (uint32_t i = 0; i < kMaxProbes; ++i) {
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

    // Skip update if the sky cubemap is not yet available (first frames)
    if (skyCubemapSrvIndex == UINT32_MAX) {
        return result;
    }

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

        if (needsBake && !capturedThisFrame) {
            // Bake IBL for this probe using the sky cubemap as source.
            // In the future, per-probe scene rendering would replace this with
            // a cubemap captured from the probe's world position.
            auto bakeResult = m_iblBaker->BakeEnvironment(skyCubemapSrvIndex, skyCubemapTexture);

            if (bakeResult.irradianceSrvIndex != UINT32_MAX &&
                bakeResult.prefilteredSrvIndex != UINT32_MAX) {

                slot.irradianceSrvIndex  = bakeResult.irradianceSrvIndex;
                slot.prefilteredSrvIndex = bakeResult.prefilteredSrvIndex;
                slot.captured = true;

                // Clear the dirty flag on the ECS component
                auto& probeComp = registry.get<ReflectionProbeComponent>(entry.entity);
                probeComp.needsCapture = false;

                // Store the baked SRV indices back on the component for external queries
                probeComp.irradianceSrvIndex  = bakeResult.irradianceSrvIndex;
                probeComp.prefilteredSrvIndex = bakeResult.prefilteredSrvIndex;

                UZ_LOG(Renderer, Info,
                       "ReflectionProbeRenderer: baked probe {} (irrad={}, prefilt={})",
                       i, bakeResult.irradianceSrvIndex, bakeResult.prefilteredSrvIndex);
            } else {
                UZ_LOG(Renderer, Warning,
                       "ReflectionProbeRenderer: IBL bake failed for probe {}", i);
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
        gpu._pad0             = 0.0f;
        gpu.irradianceSrvIndex  = slot.irradianceSrvIndex;
        gpu.prefilteredSrvIndex = slot.prefilteredSrvIndex;

        ++written;
    }

    // Zero out remaining slots to prevent stale GPU reads
    for (uint32_t i = written; i < kMaxProbes; ++i) {
        std::memset(&mapped[i], 0, sizeof(ReflectionProbeGPU));
    }

    m_device->UnmapBuffer(m_probeBuffer);

    return written;
}

} // namespace uz
