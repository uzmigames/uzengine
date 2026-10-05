/// UzEngine Radiance Cache — Implementation.
///
/// Manages a 16^3 world-space probe grid for diffuse GI via L2 SH.
/// Each frame, dispatches a compute shader that traces rays from probes
/// through the global distance field and accumulates radiance into SH.
///
/// @ref Lumen GI — world-space probe clipmap layout with SH radiance storage.
/// Probe world offsets computed per-frame to follow camera movement.

#include "radiance_cache.h"

#include "global_distance_field.h"
#include "cb_ring_allocator.h"
#include "constant_buffers.h"
#include "log.h"
#include "rhi.h"
#include "cvar.h"

#include <cstring>
#include <string>
#include <algorithm>
#include <cmath>
#include <cmath>
#include <limits>
#include <utility>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_RadianceCache    = UZ_CVAR_BOOL("r.RadianceCache", false, "SDF radiance cache GI");
static auto* cv_RCGridExtent     = UZ_CVAR_FLOAT("r.GI.RadianceCache.GridExtent", 5000.f, "Probe grid half-extent (cm)", 100.f, 50000.f);
static auto* cv_RCKnownRadiance = UZ_CVAR_FLOAT("r.GI.Diagnostic.KnownRadiance", 0.0f,
    "Diagnostic: every probe ray of BOTH GI writers returns this constant radiance instead of tracing "
    "(0 = off). Isolates the atlas/SH, the query and the visibility test from the tracing", 0.0f, 100.0f);
static auto* cv_RCKnownRadianceMode = UZ_CVAR_INT("r.GI.Diagnostic.KnownRadianceMode", 0,
    "Diagnostic: 0 = the known radiance is a constant; 1 = L * (0.5 + 0.5 dot(dir, d0)), a smooth "
    "directional field that exposes a misplaced atlas border; 2 = per-probe parity (odd cells L, even L/4), "
    "a field that differs between neighbouring probes so visibility weights show", 0, 2);
static auto* cv_RCKnownDistance = UZ_CVAR_FLOAT("r.GI.Diagnostic.KnownDistance", 0.0f,
    "Diagnostic (DDGI): with KnownRadiance on, every probe ray reports this hit distance in cm instead of "
    "the traced one (0 = off), so the depth atlas holds known moments and Chebyshev can be predicted",
    0.0f, 100000.0f);
static auto* cv_RCKnownDistanceMode = UZ_CVAR_INT("r.GI.Diagnostic.KnownDistanceMode", 0,
    "Diagnostic (DDGI): 0 = the known distance is a constant; 1 = D * (0.75 + 0.25 dot(dir, d0)), which "
    "varies across each depth tile and exercises the depth atlas border", 0, 1);
static auto* cv_RCDdgiFarDepth = UZ_CVAR_BOOL("r.GI.Diagnostic.DdgiFarDepth", false,
    "Diagnostic (DDGI): the depth pass stores the far distance for every ray (visibility 1 at the consumer) "
    "while the radiance stays real - separates Chebyshev from the writer-side difference under real lighting");
static auto* cv_RCKnownRadianceKeepDepth = UZ_CVAR_BOOL("r.GI.Diagnostic.KnownRadianceKeepDepth", false,
    "Diagnostic: with KnownRadiance on, the DDGI keeps its real hit distances (Chebyshev acts) while "
    "the radiance stays constant");
static auto* cv_RCDdgiSequence = UZ_CVAR_BOOL("r.GI.RadianceCache.Diagnostic.DdgiSequence", false,
    "Diagnostic: trace the DDGI writer's ray sequence (index/count perturbation, identical for every probe) "
    "instead of this writer's per-frame + per-probe rotation, so the two writers sample the same directions");
static auto* cv_RCDiagnostic = UZ_CVAR_BOOL("r.GI.RadianceCache.Diagnostic", false, "Read back and log the actual SH probe buffer once after frame 120");
static auto* cv_RCRays = UZ_CVAR_INT("r.GI.RadianceCache.RaysPerProbe", 32,
    "Sphere tracing rays per updated irradiance probe", 4, 64);
static auto* cv_RCProbeBudget = UZ_CVAR_INT("r.GI.RadianceCache.ProbesPerFrame", 1024,
    "Irradiance probes updated per frame after initialization", 64, 4096);
static auto* cv_RCProbeClassification = UZ_CVAR_INT("r.GI.RadianceCache.ProbeClassification", 0,
    "Which probes the radiance cache treats as inside geometry. 0 = a biased field distance below 5 cm "
    "at the probe (the current rule: it cannot tell the two sides of a surface apart and discards free "
    "probes near walls). 1 = followup-01 candidate: behind the nearest mesh face is inside; probes the "
    "field puts in a surface's band are traced from a relocated origin and never reach receivers "
    "behind that face. 2 = as 1, and also inside when more than half of the classifier's fixed rays leave some mesh more often than they enter it (a probe inside a solid whose first hits are faces embedded in it). 3 = followup-01 candidate: a probe is valid when the air around it is connected to the camera (flood fill over the field from the camera, probes climb the field gradient to air); needs neither closed meshes nor a consistent orientation", 0, 3);
static auto* cv_RCReachPasses = UZ_CVAR_INT("r.GI.RadianceCache.ReachPasses", 64,
    "Classification mode 3: flood-fill dispatches per reclassification (each propagates up to 16 voxels "
    "inside an 8^3 tile); raise it to check the fill has converged", 1, 1024);
static auto* cv_RCReachRelocate = UZ_CVAR_BOOL("r.GI.RadianceCache.ReachRelocate", false,
    "Classification mode 3: move a probe that is inside geometry or in air the camera cannot reach to the "
    "nearest reached air within half a spacing; readers weight and guard it at the moved position");
static auto* cv_RCReachVisibleSeeds = UZ_CVAR_BOOL("r.GI.RadianceCache.ReachVisibleSeeds", false,
    "Classification mode 3: air voxels just in front of visible points (G-buffer, every 4th pixel) also "
    "seed the flood fill - visibility proves they are connected to the camera where the biased field "
    "seals the opening");
/// Frames between a readback copy and its CPU read: the host waits the fence of frame N-3
/// before recording frame N (three command-list frames in flight), so a copy recorded in
/// frame F is complete once frame F + 4 is being recorded.
static constexpr uint32_t kReadbackFrameLag = 4;
static auto* cv_RCReachDump = UZ_CVAR_BOOL("r.GI.RadianceCache.ReachDump", false,
    "Classification mode 3 diagnostic: log the air grid once after frame 120 (one 'RC reach dump' line per "
    "(z, y) row: '#' solid, '.' air not reached, 'R' reached from the camera)");

namespace uz {

uint32_t RadianceCache::ClassificationMode() {
    return static_cast<uint32_t>(cv_RCProbeClassification->value);
}

// ─────────────────────────────────────────────────────────────────────────────
// Destruction
// ─────────────────────────────────────────────────────────────────────────────

RadianceCache::~RadianceCache() {
    DestroyResources();
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateResources
// ─────────────────────────────────────────────────────────────────────────────

bool RadianceCache::CreateResources(IRHIDevice* device, float worldExtent) {
    if (!device) {
        UZ_LOG(Renderer, Error, "RadianceCache::CreateResources: null device");
        return false;
    }

    DestroyResources();

    m_device      = device;
    m_gridExtent  = worldExtent;
    m_probeSpacing = (worldExtent * 2.0f) / static_cast<float>(kProbesPerAxis - 1);
    m_gridCenter  = Vec3{0.0f, 0.0f, 0.0f};
    m_gridOrigin  = Vec3{-worldExtent, -worldExtent, -worldExtent};
    m_completedGridOrigin = m_gridOrigin;
    m_pendingHistoryShift[0] = m_pendingHistoryShift[1] = m_pendingHistoryShift[2] = 0;
    m_historyInvalid = false;

    // ── Create StructuredBuffer for SH coefficients ────────────────────────
    // kTotalProbes * kSHFloat4PerProbe * sizeof(float4) = 4096 * 7 * 16 = 458752 bytes, followed by
    // one guard record per probe (kGuardFloat4PerProbe; zero under the current classification).
    const uint32_t totalFloat4s = kTotalProbes * (kSHFloat4PerProbe + kGuardFloat4PerProbe);
    const uint32_t bufferSize   = totalFloat4s * 4 * sizeof(float); // float4 = 16 bytes

    RHIBufferDesc bufDesc{};
    bufDesc.sizeBytes = bufferSize;
    bufDesc.stride    = 4 * sizeof(float); // sizeof(float4) = 16
    bufDesc.usage     = RHIBufferUsage::StructuredBuffer
                      | RHIBufferUsage::UnorderedAccess
                      | RHIBufferUsage::CopySrc | RHIBufferUsage::CopyDst;
    bufDesc.debugName = "RadianceCache_SH";

    m_shBuffer = device->CreateBuffer(bufDesc);
    if (!m_shBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "RadianceCache: failed to create SH buffer ({} bytes)", bufferSize);
        return false;
    }

    m_shBufferSrv = device->GetBufferSRVIndex(m_shBuffer);
    m_shBufferUav = device->GetBufferUAVIndex(m_shBuffer);

    bufDesc.debugName = "RadianceCache_SH_History";
    m_historyShBuffer = device->CreateBuffer(bufDesc);
    if (!m_historyShBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "RadianceCache: failed to create history SH buffer ({} bytes)", bufferSize);
        DestroyResources();
        return false;
    }
    m_historyShBufferSrv = device->GetBufferSRVIndex(m_historyShBuffer);
    m_historyShBufferUav = device->GetBufferUAVIndex(m_historyShBuffer);

    // Nearest face and back-face fraction per probe (probe_side_cs.hlsl, two float4), read by the
    // trace under the candidate classification. 128 KiB; created unconditionally so the
    // classification can be switched live.
    bufDesc.sizeBytes = kTotalProbes * 2 * 4 * sizeof(float);
    bufDesc.debugName = "RadianceCache_ProbeSide";
    m_probeSideBuffer = device->CreateBuffer(bufDesc);
    if (m_probeSideBuffer.IsValid()) {
        m_probeSideSrv = device->GetBufferSRVIndex(m_probeSideBuffer);
        m_probeSideUav = device->GetBufferUAVIndex(m_probeSideBuffer);
    }
    m_probeSideState = RHIResourceState::Common;
    m_probeSideValid = false;

    if (cv_RCDiagnostic->value) {
        // surface, traceStats, skyStats, pathStats, classification
        bufDesc.sizeBytes = kTotalProbes * kDiagnosticFloat4PerProbe * 4 * sizeof(float);
        bufDesc.debugName = "RadianceCache_TraceDiagnostics";
        m_diagnosticStats = device->CreateBuffer(bufDesc);
        if (m_diagnosticStats.IsValid())
            m_diagnosticStatsUav = device->GetBufferUAVIndex(m_diagnosticStats);
    }

    UZ_LOG(Renderer, Info,
           "RadianceCache: created SH buffer ({} probes, {} float4s, {} KB, SRV={}, UAV={})",
           kTotalProbes, totalFloat4s, bufferSize / 1024, m_shBufferSrv, m_shBufferUav);

    m_initialized = true;
    m_dirty       = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// DestroyResources
// ─────────────────────────────────────────────────────────────────────────────

void RadianceCache::DestroyResources() {
    if (m_device && m_diagnosticStats.IsValid()) {
        m_device->DestroyBuffer(m_diagnosticStats);
    }
    if (m_device && m_diagnosticReadback.IsValid()) {
        m_device->DestroyBuffer(m_diagnosticReadback);
    }
    if (m_device && m_shBuffer.IsValid()) {
        m_device->DestroyBuffer(m_shBuffer);
    }
    if (m_device && m_historyShBuffer.IsValid()) {
        m_device->DestroyBuffer(m_historyShBuffer);
    }
    if (m_device && m_probeSideBuffer.IsValid()) {
        m_device->DestroyBuffer(m_probeSideBuffer);
    }
    m_probeSideBuffer = {};
    m_probeSideSrv = UINT32_MAX;
    m_probeSideUav = UINT32_MAX;
    m_probeSideValid = false;
    m_probeSideMode = 0;
    if (m_device && m_reachAirBuffer.IsValid()) {
        m_device->DestroyBuffer(m_reachAirBuffer);
    }
    if (m_device && m_reachStatsBuffer.IsValid()) {
        m_device->DestroyBuffer(m_reachStatsBuffer);
    }
    m_reachAirBuffer = {};
    m_reachStatsBuffer = {};
    m_reachAirUav = UINT32_MAX;
    m_reachStatsUav = UINT32_MAX;
    m_reachAirState = RHIResourceState::Common;
    m_reachStatsState = RHIResourceState::Common;
    if (m_device && m_reachReadback.IsValid()) {
        m_device->DestroyBuffer(m_reachReadback);
    }
    m_reachReadback = {};
    m_reachDumpFrame = UINT32_MAX;
    m_reachDumped = false;

    m_device = nullptr;
    m_shBuffer = RHIBufferHandle{};
    m_shBufferSrv = UINT32_MAX;
    m_shBufferUav = UINT32_MAX;
    m_historyShBuffer = {};
    m_historyShBufferSrv = UINT32_MAX;
    m_historyShBufferUav = UINT32_MAX;
    m_shBufferState = RHIResourceState::Common;
    m_historyShBufferState = RHIResourceState::Common;
    m_gridCenter = Vec3{};
    m_gridOrigin = Vec3{};
    m_completedGridOrigin = Vec3{};
    m_pendingHistoryShift[0] = m_pendingHistoryShift[1] = m_pendingHistoryShift[2] = 0;
    m_gridExtent = 0.0f;
    m_probeSpacing = 0.0f;
    m_frameCounter = 0;
    m_probeUpdateCursor = 0;
    m_dirty = true;
    m_firstWritePending = true;
    m_historyInvalid = false;
    m_initialized = false;
    m_diagnosticReadback = {};
    m_diagnosticStats = {};
    m_diagnosticStatsUav = UINT32_MAX;
    m_diagnosticStatsState = RHIResourceState::Common;
    m_diagnosticCopyFrame = UINT32_MAX;
    m_diagnosticComplete = false;
    m_diagnosticOrigin = {};
    m_diagnosticSpacing = 0.0f;
}

void RadianceCache::QueueDiagnosticReadback(IRHICommandList* cmdList, uint32_t frameIndex) {
    if (!cv_RCDiagnostic->value || m_diagnosticComplete || m_diagnosticReadback.IsValid() || frameIndex < 120) return;
    RHIBufferDesc desc{};
    const uint32_t shBytes = kTotalProbes * kSHFloat4PerProbe * 4 * sizeof(float);
    const uint32_t statsBytes = m_diagnosticStats.IsValid()
        ? kTotalProbes * kDiagnosticFloat4PerProbe * 4 * sizeof(float) : 0;
    desc.sizeBytes = shBytes + statsBytes;
    desc.memory = RHIMemoryType::Readback;
    desc.debugName = "RadianceCache_DiagnosticReadback";
    m_diagnosticReadback = m_device->CreateBuffer(desc);
    if (!m_diagnosticReadback.IsValid()) {
        UZ_LOG(Renderer, Error, "RC diagnostic: readback allocation failed");
        m_diagnosticComplete = true;
        return;
    }
    RHIBarrierDesc barrier{};
    barrier.buffer = m_shBuffer;
    barrier.stateBefore = RHIResourceState::ShaderResource;
    barrier.stateAfter = RHIResourceState::CopySrc;
    cmdList->ResourceBarrier(&barrier, 1);
    cmdList->CopyBuffer(m_shBuffer, 0, m_diagnosticReadback, 0, shBytes);
    barrier.stateBefore = RHIResourceState::CopySrc;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);
    if (statsBytes != 0) {
        barrier.buffer = m_diagnosticStats;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter = RHIResourceState::CopySrc;
        cmdList->ResourceBarrier(&barrier, 1);
        cmdList->CopyBuffer(m_diagnosticStats, 0, m_diagnosticReadback, shBytes, statsBytes);
        barrier.stateBefore = RHIResourceState::CopySrc;
        barrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }
    m_diagnosticCopyFrame = frameIndex;
    m_diagnosticOrigin = m_gridOrigin;
    m_diagnosticSpacing = m_probeSpacing;
    UZ_LOG(Renderer, Info, "RC diagnostic: queued SH snapshot frame={} SRV={} UAV={} bytes={} origin=({},{},{}) spacing={}",
           frameIndex, m_shBufferSrv, m_shBufferUav, desc.sizeBytes, m_gridOrigin.x, m_gridOrigin.y, m_gridOrigin.z, m_probeSpacing);
}

void RadianceCache::DispatchProbeSide(IRHICommandList* cmdList, const GlobalDistanceField& gdf,
                                      RHIPipelineHandle sidePipeline, CBRingAllocator* cbRing) {
    const auto& entries = gdf.GetMeshEntries();
    const auto meshCount = static_cast<uint32_t>(entries.size());
    // Each ray keeps its nearest hit over every instance, so all instances go in one dispatch: one
    // composite batch. Beyond it the classifier does not run and the trace keeps the current rule.
    if (meshCount > kGdfMeshesPerBatch) {
        if (!m_probeSideTooManyLogged) {
            m_probeSideTooManyLogged = true;
            UZ_LOG(Renderer, Warning, "RadianceCache: probe side classifier supports {} instances, the field has {}; "
                   "keeping the current classification", kGdfMeshesPerBatch, meshCount);
        }
        m_probeSideValid = false;
        return;
    }
    auto allocation = cbRing->Allocate(kGdfMeshesPerBatch * sizeof(GdfMeshSdfGPU));
    if (!allocation.cpuPtr) {
        UZ_LOG(Renderer, Error, "RadianceCache: failed to allocate the probe side instances");
        m_probeSideValid = false;
        return;
    }
    std::memset(allocation.cpuPtr, 0, kGdfMeshesPerBatch * sizeof(GdfMeshSdfGPU));
    if (meshCount > 0) {
        std::memcpy(allocation.cpuPtr, entries.data(), meshCount * sizeof(GdfMeshSdfGPU));
    }

    struct ProbeSideConstants {       // probe_side_cs.hlsl RootConstants (9 DWORDs)
        float    originX, originY, originZ, spacing;
        uint32_t probesPerAxis, totalProbes, outputUav, meshCount;
        float    maxDistance;
    } constants{m_gridOrigin.x, m_gridOrigin.y, m_gridOrigin.z, m_probeSpacing,
                kProbesPerAxis, kTotalProbes, m_probeSideUav, meshCount,
                m_gridExtent * 2.0f};   // the cache's own trace distance (maxTraceDistance)
    static_assert(sizeof(ProbeSideConstants) == 9 * sizeof(uint32_t));

    RHIBarrierDesc barrier{};
    barrier.buffer      = m_probeSideBuffer;
    barrier.stateBefore = m_probeSideState;
    barrier.stateAfter  = RHIResourceState::UnorderedAccess;
    cmdList->ResourceBarrier(&barrier, 1);
    cmdList->SetPipeline(sidePipeline);
    cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&constants), 9);
    cmdList->SetRootCBV(2, allocation.bufferHandle, allocation.offsetBytes,
                        allocation.cpuPtr, kGdfMeshesPerBatch * sizeof(GdfMeshSdfGPU));
    cmdList->Dispatch((kTotalProbes + 63) / 64, 1, 1);
    barrier.stateBefore = RHIResourceState::UnorderedAccess;
    barrier.stateAfter  = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);
    m_probeSideState  = RHIResourceState::ShaderResource;
    m_probeSideValid  = true;
    m_probeSideHash   = gdf.GetMeshEntriesHash();
    m_probeSideOrigin = m_gridOrigin;
    m_probeSideMode   = ClassificationMode();
}

void RadianceCache::HandleReachDump(IRHICommandList* cmdList, uint32_t frameIndex) {
    if (!cv_RCReachDump->value || m_reachDumped || !m_reachAirBuffer.IsValid()) return;
    const uint32_t voxels = kReachResolution * kReachResolution * kReachResolution;
    if (!m_reachReadback.IsValid()) {
        if (frameIndex < 120) return;
        RHIBufferDesc desc{};
        desc.sizeBytes = voxels * sizeof(uint32_t);
        desc.memory = RHIMemoryType::Readback;
        desc.debugName = "RadianceCache_ReachReadback";
        m_reachReadback = m_device->CreateBuffer(desc);
        if (!m_reachReadback.IsValid()) {
            UZ_LOG(Renderer, Error, "RC reach dump: readback allocation failed");
            m_reachDumped = true;
            return;
        }
        RHIBarrierDesc barrier{};
        barrier.buffer = m_reachAirBuffer;
        barrier.stateBefore = m_reachAirState;
        barrier.stateAfter = RHIResourceState::CopySrc;
        cmdList->ResourceBarrier(&barrier, 1);
        cmdList->CopyBuffer(m_reachAirBuffer, 0, m_reachReadback, 0, desc.sizeBytes);
        barrier.stateBefore = RHIResourceState::CopySrc;
        barrier.stateAfter = m_reachAirState;
        cmdList->ResourceBarrier(&barrier, 1);
        m_reachDumpFrame = frameIndex;
        m_reachDumpGridOrigin = m_probeSideOrigin;
        return;
    }
    if (frameIndex - m_reachDumpFrame < kReadbackFrameLag) return;
    // The copy was recorded kReadbackFrameLag frames ago; the host waited that frame's fence
    // before recording this one, so the readback is complete without a WaitIdle (the WaitIdle
    // that stood here was one of the two candidates for the unexplained hang of H-71).
    m_reachDumped = true;
    const auto* data = static_cast<const uint32_t*>(m_device->MapBuffer(m_reachReadback));
    if (!data) {
        UZ_LOG(Renderer, Error, "RC reach dump: readback map failed");
        return;
    }
    const float voxel = m_probeSpacing * static_cast<float>(kProbesPerAxis) / static_cast<float>(kReachResolution);
    UZ_LOG(Renderer, Info, "RC reach dump: header frame={} gridOrigin=({},{},{}) airOrigin=({},{},{}) voxel={} resolution={}",
           m_reachDumpFrame, m_reachDumpGridOrigin.x, m_reachDumpGridOrigin.y, m_reachDumpGridOrigin.z,
           m_reachDumpGridOrigin.x - 0.5f * m_probeSpacing, m_reachDumpGridOrigin.y - 0.5f * m_probeSpacing,
           m_reachDumpGridOrigin.z - 0.5f * m_probeSpacing, voxel, kReachResolution);
    std::string row(kReachResolution, ' ');
    for (uint32_t z = 0; z < kReachResolution; ++z) {
        for (uint32_t y = 0; y < kReachResolution; ++y) {
            for (uint32_t x = 0; x < kReachResolution; ++x) {
                const uint32_t state = data[x + y * kReachResolution + z * kReachResolution * kReachResolution];
                row[x] = state == 2u ? 'R' : state == 1u ? '.' : '#';
            }
            UZ_LOG(Renderer, Info, "RC reach dump: z={} y={} row={}", z, y, row);
        }
    }
    m_device->UnmapBuffer(m_reachReadback);
}

bool RadianceCache::DispatchProbeReach(IRHICommandList* cmdList, RHIPipelineHandle reachPipeline,
                                       RHIBufferHandle traceConstants, uint64_t traceConstantsOffset,
                                       const void* traceConstantsCpu, uint32_t traceConstantsSize) {
    if (!reachPipeline.IsValid() || !m_probeSideBuffer.IsValid()) {
        return false;
    }
    if (!m_reachAirBuffer.IsValid()) {
        RHIBufferDesc desc{};
        desc.sizeBytes = kReachResolution * kReachResolution * kReachResolution * sizeof(uint32_t);
        desc.stride    = sizeof(uint32_t);
        desc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.debugName = "RadianceCache_ReachAir";
        m_reachAirBuffer = m_device->CreateBuffer(desc);
        desc.sizeBytes = 4 * sizeof(uint32_t);
        desc.debugName = "RadianceCache_ReachStats";
        m_reachStatsBuffer = m_device->CreateBuffer(desc);
        if (!m_reachAirBuffer.IsValid() || !m_reachStatsBuffer.IsValid()) {
            UZ_LOG(Renderer, Error, "RadianceCache: failed to create the reachability buffers");
            return false;
        }
        m_reachAirUav = m_device->GetBufferUAVIndex(m_reachAirBuffer);
        m_reachStatsUav = m_device->GetBufferUAVIndex(m_reachStatsBuffer);
    }

    RHIBarrierDesc toUav[3]{};
    toUav[0].buffer = m_reachAirBuffer;   toUav[0].stateBefore = m_reachAirState;
    toUav[1].buffer = m_reachStatsBuffer; toUav[1].stateBefore = m_reachStatsState;
    toUav[2].buffer = m_probeSideBuffer;  toUav[2].stateBefore = m_probeSideState;
    for (auto& b : toUav) b.stateAfter = RHIResourceState::UnorderedAccess;
    cmdList->ResourceBarrier(toUav, 3);

    struct ReachConstants {           // probe_reach_cs.hlsl RootConstants (12 DWORDs)
        uint32_t pass, airUav, sideUav, statsUav;
        float    cameraX, cameraY, cameraZ;
        uint32_t localIterations, relocate, positionSrv, viewWidth, viewHeight;
    } constants{0, m_reachAirUav, m_probeSideUav, m_reachStatsUav,
                m_cameraPos.x, m_cameraPos.y, m_cameraPos.z, 16, cv_RCReachRelocate->value ? 1u : 0u,
                m_seedPositionSrv, m_seedWidth, m_seedHeight};
    static_assert(sizeof(ReachConstants) == 12 * sizeof(uint32_t));

    cmdList->SetPipeline(reachPipeline);
    cmdList->SetRootCBV(2, traceConstants, traceConstantsOffset, traceConstantsCpu, traceConstantsSize);
    RHIBarrierDesc uavBarrier[3]{};
    uavBarrier[0].buffer = m_reachAirBuffer;
    uavBarrier[1].buffer = m_reachStatsBuffer;
    uavBarrier[2].buffer = m_probeSideBuffer;
    for (auto& b : uavBarrier) b.stateBefore = b.stateAfter = RHIResourceState::UnorderedAccess;
    const uint32_t groups = kReachResolution / 8;
    const auto run = [&](uint32_t pass, uint32_t x, uint32_t y, uint32_t z) {
        constants.pass = pass;
        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&constants), 12);
        cmdList->Dispatch(x, y, z);
        cmdList->ResourceBarrier(uavBarrier, 3);
    };
    run(3, 1, 1, 1);                                   // reset the seed counter
    run(0, groups, groups, groups);                    // air + seed
    const bool visibleSeeds = cv_RCReachVisibleSeeds->value && m_seedPositionSrv != UINT32_MAX
                           && m_seedWidth > 0 && m_seedHeight > 0;
    if (visibleSeeds) {
        const uint32_t cells = ((m_seedWidth + 3) / 4) * ((m_seedHeight + 3) / 4);
        run(4, (cells + 511) / 512, 1, 1);             // air in front of visible points
    }
    const auto passes = static_cast<uint32_t>(cv_RCReachPasses->value);
    for (uint32_t i = 0; i < passes; ++i) {
        run(1, groups, groups, groups);                // flood
    }
    run(2, 1, 1, (kTotalProbes + 511) / 512);          // per-probe decision

    RHIBarrierDesc toSrv{};
    toSrv.buffer = m_probeSideBuffer;
    toSrv.stateBefore = RHIResourceState::UnorderedAccess;
    toSrv.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&toSrv, 1);
    m_probeSideState = RHIResourceState::ShaderResource;
    m_reachAirState = RHIResourceState::UnorderedAccess;
    m_reachStatsState = RHIResourceState::UnorderedAccess;
    m_probeSideValid = true;
    m_probeSideOrigin = m_gridOrigin;
    m_probeSideMode = 3;
    if (cv_RCDiagnostic->value) {
        UZ_LOG(Renderer, Info, "RC reach: frame={} origin=({},{},{}) camera=({},{},{}) passes={} resolution={} visibleSeeds={} relocate={}",
               m_frameCounter, m_gridOrigin.x, m_gridOrigin.y, m_gridOrigin.z,
               m_cameraPos.x, m_cameraPos.y, m_cameraPos.z, passes, kReachResolution, visibleSeeds,
               cv_RCReachRelocate->value);
    }
    return true;
}

void RadianceCache::ReadDiagnosticResults(uint32_t frameIndex) {
    if (!m_diagnosticReadback.IsValid() || m_diagnosticComplete || frameIndex - m_diagnosticCopyFrame < kReadbackFrameLag) return;
    // Complete by construction (see HandleReachDump): no WaitIdle on the recording thread.
    m_diagnosticComplete = true;
    const auto* data = static_cast<const float*>(m_device->MapBuffer(m_diagnosticReadback));
    if (!data) {
        UZ_LOG(Renderer, Error, "RC diagnostic: readback map failed");
        return;
    }
    uint32_t finite = 0, invalid = 0, nonzero = 0;
    float minimum = std::numeric_limits<float>::max(), maximum = std::numeric_limits<float>::lowest();
    double dc[3]{}, axisSum[6][3]{};
    uint32_t axisFinite[6][3]{};
    const auto evaluate = [](const float* probe, uint32_t axis, float sign, uint32_t channel) {
        const float* a = probe + channel * 4;
        const float* b = probe + (channel + 3) * 4;
        float value = a[axis] * sign + a[3];
        if (axis == 2) value += b[2];
        if (axis == 0) value += probe[24 + channel];
        if (axis == 1) value -= probe[24 + channel];
        return value;
    };
    double gridRadiance = 0.0, gridSky = 0.0; uint32_t gridStats = 0;
    for (uint32_t index = 0; index < kTotalProbes; ++index) {
        const float* probe = data + index * 28;
        bool hasEnergy = false;
        if (m_diagnosticStats.IsValid()) {
            const float* stats = data + kTotalProbes * kSHFloat4PerProbe * 4 + index * kDiagnosticFloat4PerProbe * 4;
            // stats[0] = field distance at the probe (< kSurfaceEpsilon → inside geometry, no trace)
            if (std::isfinite(stats[7]) && std::isfinite(stats[4]) && stats[0] >= 5.0f) {
                gridRadiance += stats[7]; gridSky += stats[4]; ++gridStats;
            }
            // Every probe, one line each, pairing with "DDGI transport: probe=..." by index (same
            // x-fastest layout, same origin and spacing). fieldDistance is the inside-geometry
            // criterion (< 5 cm = no trace), reported rather than applied, so a comparison can
            // decide the filter itself. frontHit = fraction of hits facing the ray; sunLum = mean
            // direct-sun luminance at the hits (followup-01, 2026-09-16).
            {
                const uint32_t gx = index % kProbesPerAxis;
                const uint32_t gy = (index / kProbesPerAxis) % kProbesPerAxis;
                const uint32_t gz = index / (kProbesPerAxis * kProbesPerAxis);
                // skyLum = mean luminance carried by sky rays over ALL rays (divide by skyFraction for
                // the per-sky-ray mean); lost = fraction of rays that exhausted the budget; hitLum = the
                // hits' share. skyLum + hitLum = meanRadiance up to rounding.
                // classification (followup-01): sideDistance = distance to the nearest mesh face,
                // negative when the probe is behind it (65504 none; 0 when the classifier did not
                // run); relocation = how far the trace origin moved; class = 0 valid / 1 inside by the
                // field rule / 2 valid by the rays / 3 inside by the rays / 4 relocated / 5 relocation
                // failed / 6 inside by the crossings alone (mode 2); 7 unreachable / 8 reachable / 9 reachable,
                // traced from its climb / 10 relocated (mode 3); backFraction = back-face fraction
                // of the classifier's fixed rays; crossingFraction = fraction of those rays that leave
                // some mesh more often than they enter it.
                UZ_LOG(Renderer, Info, "RC transport: probe={} cell=({},{},{}) fieldDistance={} skyFraction={} frontHit={} sunLum={} meanRadiance={} skyLum={} lost={} hitLum={} escapeMinDist={} hitSunLum={} hitIndirectLum={} hitCount={} sideDistance={} relocation={} class={} backFraction={} crossingFraction={} originOffset=({},{},{})",
                       index, gx, gy, gz, stats[0], stats[4], stats[5], stats[6], stats[7], stats[8], stats[10], stats[11],
                       stats[12], stats[13], stats[14], stats[15], stats[16], stats[17], static_cast<int>(stats[18]), stats[19], stats[20], stats[21], stats[22], stats[23]);
            }
        }
        for (uint32_t coefficient = 0; coefficient < 27; ++coefficient) {
            const float value = probe[coefficient];
            if (!std::isfinite(value)) { ++invalid; continue; }
            ++finite;
            minimum = std::min(minimum, value); maximum = std::max(maximum, value);
            hasEnergy |= value != 0.0f;
        }
        nonzero += hasEnergy;
        for (uint32_t channel = 0; channel < 3; ++channel) {
            const float constant = probe[channel * 4 + 3];
            if (std::isfinite(constant)) dc[channel] += constant;
            for (uint32_t direction = 0; direction < 6; ++direction) {
                const float value = evaluate(probe, direction / 2, direction % 2 == 0 ? 1.0f : -1.0f, channel);
                if (std::isfinite(value)) { axisSum[direction][channel] += value; ++axisFinite[direction][channel]; }
            }
        }
    }
    if (m_diagnosticStats.IsValid()) {
        // Pairs with "DDGI transport: grid ..." — both are the unweighted mean over probes of the
        // mean ray radiance luminance seen by the writer, in linear radiance (scripts/test_gi_energy_gpu.ps1).
        UZ_LOG(Renderer, Info, "RC transport: grid snapshotFrame={} readFrame={} probes={} meanRadiance={} skyFraction={}",
               m_diagnosticCopyFrame, frameIndex, gridStats,
               gridStats ? gridRadiance / gridStats : 0.0, gridStats ? gridSky / gridStats : 0.0);
    }
    UZ_LOG(Renderer, Info, "RC diagnostic: snapshotFrame={} readFrame={} probes={} nonzeroProbes={} finite={} invalid={} min={} max={} meanPolynomialDC=({},{},{})",
           m_diagnosticCopyFrame, frameIndex, kTotalProbes, nonzero, finite, invalid,
           finite ? minimum : 0.0f, finite ? maximum : 0.0f, dc[0] / kTotalProbes, dc[1] / kTotalProbes, dc[2] / kTotalProbes);
    for (uint32_t direction = 0; direction < 6; ++direction) {
        UZ_LOG(Renderer, Info, "RC diagnostic: axis={} sign={} meanIrradiance=({},{},{}) finiteRGB=({},{},{})",
               direction / 2, direction % 2 == 0 ? 1 : -1,
               axisSum[direction][0] / std::max(axisFinite[direction][0], 1u),
               axisSum[direction][1] / std::max(axisFinite[direction][1], 1u),
               axisSum[direction][2] / std::max(axisFinite[direction][2], 1u),
               axisFinite[direction][0], axisFinite[direction][1], axisFinite[direction][2]);
    }
    for (uint32_t z : {7u, 8u}) for (uint32_t y : {7u, 8u}) for (uint32_t x : {7u, 8u}) {
        const uint32_t index = x + y * kProbesPerAxis + z * kProbesPerAxis * kProbesPerAxis;
        const float* probe = data + index * 28;
        UZ_LOG(Renderer, Info, "RC diagnostic: centralProbe={} world=({},{},{}) polynomialDC=({},{},{}) plusZ=({},{},{}) minusZ=({},{},{})",
               index, m_diagnosticOrigin.x + x * m_diagnosticSpacing, m_diagnosticOrigin.y + y * m_diagnosticSpacing,
               m_diagnosticOrigin.z + z * m_diagnosticSpacing, probe[3], probe[7], probe[11],
               evaluate(probe, 2, 1, 0), evaluate(probe, 2, 1, 1), evaluate(probe, 2, 1, 2),
               evaluate(probe, 2, -1, 0), evaluate(probe, 2, -1, 1), evaluate(probe, 2, -1, 2));
        if (m_diagnosticStats.IsValid()) {
            const float* stats = data + kTotalProbes * kSHFloat4PerProbe * 4 + index * kDiagnosticFloat4PerProbe * 4;
            UZ_LOG(Renderer, Info, "RC transport: probe={} distance={} albedo=({},{},{}) skyFraction={} frontHitFraction={} meanDirect={} meanRadiance={}",
                   index, stats[0], stats[1], stats[2], stats[3], stats[4], stats[5], stats[6], stats[7]);
        }
    }
    m_device->UnmapBuffer(m_diagnosticReadback);
}

// ─────────────────────────────────────────────────────────────────────────────
// Update — recenter probe grid on camera
// ─────────────────────────────────────────────────────────────────────────────

void RadianceCache::Update(const Vec3& cameraPos) {
    if (!m_initialized) return;
    m_cameraPos = cameraPos;

    using namespace DirectX;

    // Snap camera position to probe spacing grid for stable interpolation coordinates
    const XMVECTOR vCamPos    = cameraPos.toXM();
    const XMVECTOR vSpacing   = XMVectorReplicate(m_probeSpacing);
    const XMVECTOR vInvSpacing = XMVectorReciprocal(vSpacing);
    const XMVECTOR vSnapped   = XMVectorMultiply(
        XMVectorFloor(XMVectorMultiply(vCamPos, vInvSpacing)), vSpacing);

    // Check if grid shifted (any axis moved by >= probeSpacing)
    const XMVECTOR vGridCenter = m_gridCenter.toXM();
    const XMVECTOR vDiff       = XMVectorAbs(XMVectorSubtract(vSnapped, vGridCenter));
    const XMVECTOR vCmp        = XMVectorGreaterOrEqual(vDiff, vSpacing);

    // Check if any of the first 3 components are true (nonzero).
    // GreaterOrEqual sets all bits for true lanes; test components directly
    // so this stays portable (SSE movemask is x86-only).
    if (XMVectorGetX(vCmp) != 0.0f || XMVectorGetY(vCmp) != 0.0f ||
        XMVectorGetZ(vCmp) != 0.0f) {
        const Vec3 snappedCenter(vSnapped);
        // Both centres are snapped to the spacing, so the shift is a whole number
        // of probes (lround absorbs the float error). Accumulate: Update() may run
        // more than once before the next UpdateProbes() consumes it.
        const float invSpacing = 1.0f / m_probeSpacing;
        m_pendingHistoryShift[0] += static_cast<int32_t>(std::lround((snappedCenter.x - m_gridCenter.x) * invSpacing));
        m_pendingHistoryShift[1] += static_cast<int32_t>(std::lround((snappedCenter.y - m_gridCenter.y) * invSpacing));
        m_pendingHistoryShift[2] += static_cast<int32_t>(std::lround((snappedCenter.z - m_gridCenter.z) * invSpacing));
        m_gridCenter = snappedCenter;
        m_gridOrigin = Vec3(XMVectorSubtract(vSnapped, XMVectorReplicate(m_gridExtent)));
        m_dirty      = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// UpdateProbes — dispatch compute shader to trace rays and accumulate SH
// ─────────────────────────────────────────────────────────────────────────────

void RadianceCache::UpdateProbes(
    IRHICommandList* cmdList,
    const GlobalDistanceField& gdf,
    RHIPipelineHandle tracePipeline,
    CBRingAllocator* cbRing,
    RHIBufferHandle perFrameBuffer,
    uint32_t perFrameOffset,
    const void* perFrameCpuPtr,
    uint32_t perFrameSize,
    uint32_t frameIndex,
    RHIPipelineHandle sidePipeline)
{
    if (!m_initialized || !cmdList || !tracePipeline.IsValid() || !cbRing) {
        return;
    }

    if (!gdf.IsInitialized()) {
        return;
    }

    ReadDiagnosticResults(frameIndex);
    HandleReachDump(cmdList, frameIndex);

    // Diagnostic freeze: once the cache has produced a result, hold it. The buffers are left
    // exactly as they are (no ping-pong, no trace), so the consumer keeps reading the converged
    // probes and stays on the same lighting path.
    if (m_frozen && !m_firstWritePending) {
        m_dirty = false;
        return;
    }

    CBAllocation constAlloc = cbRing->Allocate(sizeof(RadianceCacheTraceConstants));
    if (!constAlloc.cpuPtr) {
        UZ_LOG(Renderer, Error, "RadianceCache: failed to allocate trace constants");
        return;
    }

    m_frameCounter = frameIndex;
    // History is discarded only when there is none (first write) or when the
    // scene under the probes changed (InvalidateHistory). A grid recentre
    // (`m_dirty` from Update()) retraces every probe this frame but *keeps* each
    // probe's history: the shader reads it at the shifted coordinate
    // (`historyShift`), so only the slab of probes that entered the grid starts
    // from scratch and the multi-bounce feedback stays alive.
    const bool resetHistory = m_firstWritePending || m_historyInvalid;
    const bool fullRetrace  = resetHistory || m_dirty;

    // Candidate classification (followup-01): the nearest mesh face of every probe, recomputed when
    // the probes moved (a full retrace follows every recentre) or the instances the field was built
    // from changed. Without the pipeline or the buffer the trace falls back to the current rule.
    const uint32_t classification = ClassificationMode();
    const bool sideReady = classification != 0 && sidePipeline.IsValid() && m_probeSideBuffer.IsValid();
    const bool sideStale = fullRetrace || !m_probeSideValid || m_probeSideMode != classification
                        || m_probeSideHash != gdf.GetMeshEntriesHash()
                        || m_probeSideOrigin.x != m_gridOrigin.x || m_probeSideOrigin.y != m_gridOrigin.y
                        || m_probeSideOrigin.z != m_gridOrigin.z;
    // Mode 3 reads the trace constants, so its passes run below, once those are uploaded.
    const bool reachPending = sideReady && classification == 3 && sideStale;
    if (sideReady && classification != 3 && sideStale) {
        DispatchProbeSide(cmdList, gdf, sidePipeline, cbRing);
    }
    if (reachPending) {
        m_probeSideHash = gdf.GetMeshEntriesHash();
    }
    const bool sideActive = sideReady && (reachPending || (m_probeSideValid && m_probeSideMode == classification));

    std::swap(m_shBuffer, m_historyShBuffer);
    std::swap(m_shBufferSrv, m_historyShBufferSrv);
    std::swap(m_shBufferUav, m_historyShBufferUav);
    std::swap(m_shBufferState, m_historyShBufferState);

    // Cross-probe feedback reads an immutable previous frame (Majercik 2019, section 5.3).
    // Preserve probes outside this frame's update budget in the new output buffer
    // (a full retrace writes every probe, so nothing to preserve).
    if (!fullRetrace) {
        RHIBarrierDesc toCopy[2]{};
        toCopy[0].buffer = m_historyShBuffer;
        toCopy[0].stateBefore = m_historyShBufferState;
        toCopy[0].stateAfter = RHIResourceState::CopySrc;
        toCopy[1].buffer = m_shBuffer;
        toCopy[1].stateBefore = m_shBufferState;
        toCopy[1].stateAfter = RHIResourceState::CopyDst;
        cmdList->ResourceBarrier(toCopy, 2);
        cmdList->CopyBuffer(m_historyShBuffer, 0, m_shBuffer, 0,
                            kTotalProbes * (kSHFloat4PerProbe + kGuardFloat4PerProbe) * 4 * sizeof(float));
        toCopy[0].stateBefore = RHIResourceState::CopySrc;
        toCopy[0].stateAfter = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(toCopy, 1);
        m_historyShBufferState = RHIResourceState::ShaderResource;
        m_shBufferState = RHIResourceState::CopyDst;
    }

    // Transition SH buffer to UAV for compute read/write
    RHIBarrierDesc toUav{};
    toUav.buffer      = m_shBuffer;
    toUav.stateBefore = m_shBufferState;
    toUav.stateAfter  = RHIResourceState::UnorderedAccess;
    cmdList->ResourceBarrier(&toUav, 1);

    // Set the trace pipeline
    cmdList->SetPipeline(tracePipeline);

    // ── Build trace constants ──────────────────────────────────────────────
    RadianceCacheTraceConstants constants{};
    constants.gridOriginX       = m_gridOrigin.x;
    constants.gridOriginY       = m_gridOrigin.y;
    constants.gridOriginZ       = m_gridOrigin.z;
    constants.probeSpacing      = m_probeSpacing;
    constants.probesPerAxis     = kProbesPerAxis;
    constants.totalProbes       = kTotalProbes;
    constants.raysPerProbe      = static_cast<uint32_t>(cv_RCRays->value);
    constants.frameIndex        = frameIndex;
    constants.maxTraceDistance   = m_gridExtent * 2.0f; // Trace up to the grid diameter
    constants.temporalBlendAlpha = kTemporalBlendAlpha;
    constants.shBufferUav       = m_shBufferUav;
    // The SH buffer is created unwritten; the first dispatch must replace
    // rather than blend with undefined contents.
    constants.firstWrite        = resetHistory ? 1u : 0u;
    // Grid shift since the history buffer was traced (whole probes); the shader
    // looks history up at probe + shift. Irrelevant when the history is dropped.
    constants.historyShiftX     = resetHistory ? 0 : m_pendingHistoryShift[0];
    constants.historyShiftY     = resetHistory ? 0 : m_pendingHistoryShift[1];
    constants.historyShiftZ     = resetHistory ? 0 : m_pendingHistoryShift[2];

    // Fill GDF clipmap parameters from the global distance field
    const uint32_t numClipmaps = GlobalDistanceField::kNumClipmaps;
    constants.numGdfClipmaps = numClipmaps;

    for (uint32_t i = 0; i < numClipmaps && i < 4; ++i) {
        const auto& clip = gdf.GetClipmap(i);
        float* centerPtr = nullptr;
        float* extentPtr = nullptr;
        uint32_t* srvPtr = nullptr;

        switch (i) {
            case 0:
                centerPtr = &constants.gdfCenter0X;
                extentPtr = &constants.gdfExtent0;
                srvPtr    = &constants.gdfClipmapSrv0;
                break;
            case 1:
                centerPtr = &constants.gdfCenter1X;
                extentPtr = &constants.gdfExtent1;
                srvPtr    = &constants.gdfClipmapSrv1;
                break;
            case 2:
                centerPtr = &constants.gdfCenter2X;
                extentPtr = &constants.gdfExtent2;
                srvPtr    = &constants.gdfClipmapSrv2;
                break;
            case 3:
                centerPtr = &constants.gdfCenter3X;
                extentPtr = &constants.gdfExtent3;
                srvPtr    = &constants.gdfClipmapSrv3;
                break;
        }

        if (centerPtr && extentPtr && srvPtr) {
            centerPtr[0] = clip.center.x;
            centerPtr[1] = clip.center.y;
            centerPtr[2] = clip.center.z;
            *extentPtr    = clip.extent;
            *srvPtr       = clip.srvIndex;
        }
    }

    // Read light/sky data from PerFrameCB for accurate GI bounce
    const auto* pf = static_cast<const PerFrameCB*>(perFrameCpuPtr);

    // Sky color for missed rays — use sky atmosphere color scaled by intensity.
    // No attenuation factor — the probes should capture the full sky radiance.
    constants.skyColorR = pf->skyLightColorR * pf->skyLightIntensity;
    constants.skyColorG = pf->skyLightColorG * pf->skyLightIntensity;
    constants.skyColorB = pf->skyLightColorB * pf->skyLightIntensity;


    // Primary directional light for single-bounce GI at hit points
    constants.lightDirX     = pf->lightDir.x;
    constants.lightDirY     = pf->lightDir.y;
    constants.lightDirZ     = pf->lightDir.z;
    constants.lightColorR   = pf->lightColor.x;
    constants.lightColorG   = pf->lightColor.y;
    constants.lightColorB   = pf->lightColor.z;
    constants.lightIntensity = 1.0f;
    constants.groundAlbedo  = 0.18f; // Average scene albedo (18% grey card)

    // Temporal probe spreading: update kProbesPerFrame probes per frame,
    // wrapping the cursor across the grid. The shader skips probes outside
    // [g_probeOffset, g_probeOffset + g_probeBudget).
    const uint32_t probeBudget = fullRetrace ? kTotalProbes
        : std::min<uint32_t>(static_cast<uint32_t>(cv_RCProbeBudget->value), kTotalProbes);
    const uint32_t probeOffset = fullRetrace ? 0u : m_probeUpdateCursor;

    // Root constants at b0 (root param 0, 8 uint32s):
    //   [0] SH UAV index, [1] previous-frame SH SRV index, [2] frame index,
    //   [3] probe offset, [4] probe budget, [5] sky radiance SRV, [6..7] padding.
    uint32_t rootConstants[12] = {};
    rootConstants[0] = m_shBufferUav;
    rootConstants[1] = resetHistory ? UINT32_MAX : m_historyShBufferSrv;
    rootConstants[2] = frameIndex;
    rootConstants[3] = probeOffset;
    rootConstants[4] = probeBudget;
    rootConstants[5] = pf->prefilteredSrvIndex;
    if (m_diagnosticStats.IsValid() && !m_diagnosticComplete) {
        RHIBarrierDesc diagnosticBarrier{};
        diagnosticBarrier.buffer = m_diagnosticStats;
        diagnosticBarrier.stateBefore = m_diagnosticStatsState;
        diagnosticBarrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&diagnosticBarrier, 1);
        m_diagnosticStatsState = RHIResourceState::UnorderedAccess;
        rootConstants[6] = m_diagnosticStatsUav;
    }
    // [7] the ray sequence: 0 this writer's, 1 the DDGI writer's (diagnostic, followup-01).
    rootConstants[7] = cv_RCDdgiSequence->value != 0.0f ? 1u : 0u;
    {
        const float knownL = cv_RCKnownRadiance->value;
        std::memcpy(&rootConstants[8], &knownL, sizeof(float));
    }
    rootConstants[9] = static_cast<uint32_t>(cv_RCKnownRadianceMode->value);
    // [10] nearest-face side per probe, [11] classification (0 = the current rule).
    rootConstants[10] = sideActive ? m_probeSideSrv : UINT32_MAX;
    rootConstants[11] = sideActive ? classification : 0u;
    cmdList->SetRootConstants(0, rootConstants, 12);
    // Logged once the snapshot can be taken (frame >= 120), so it describes what the snapshot
    // sampled: on the first dispatch the IBL is not baked yet and the SRV is the sentinel.
    if (cv_RCDiagnostic->value && !m_identityLogged && frameIndex >= 120u) {
        m_identityLogged = true;
        UZ_LOG(Renderer, Info, "RC transport: identity skySrv={} mip=0 swizzle=(x,z,y) rays={} sequence={} skyMultiplier=({},{},{}) knownRadiance={} knownMode={}",
               pf->prefilteredSrvIndex, static_cast<uint32_t>(cv_RCRays->value),
               cv_RCDdgiSequence->value != 0.0f ? "ddgi-index-count" : "cache-frame-probe-rotation",
               constants.skyColorR, constants.skyColorG, constants.skyColorB, cv_RCKnownRadiance->value,
               static_cast<int>(cv_RCKnownRadianceMode->value));
    }

    // Upload bulk trace constants via CB ring and bind at b2 (root param 2)
    std::memcpy(constAlloc.cpuPtr, &constants, sizeof(constants));
    if (reachPending) {
        if (!DispatchProbeReach(cmdList, sidePipeline, constAlloc.bufferHandle, constAlloc.offsetBytes,
                                constAlloc.cpuPtr, sizeof(constants))) {
            m_probeSideValid = false;
            rootConstants[10] = UINT32_MAX;
            rootConstants[11] = 0u;
        }
        cmdList->SetPipeline(tracePipeline);
        cmdList->SetRootConstants(0, rootConstants, 12);
    }
    cmdList->SetRootCBV(2, constAlloc.bufferHandle,
                        constAlloc.offsetBytes,
                        constAlloc.cpuPtr, sizeof(constants));

    // Dispatch only as many groups as needed for this frame's probe budget.
    // 64 threads per group (matches [numthreads(64,1,1)] in the trace shader).
    const uint32_t groupCount = (probeBudget + 63) / 64;
    cmdList->Dispatch(groupCount, 1, 1);

    // Transition back to SRV for lighting shader reads
    RHIBarrierDesc toSrv{};
    toSrv.buffer      = m_shBuffer;
    toSrv.stateBefore = RHIResourceState::UnorderedAccess;
    toSrv.stateAfter  = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&toSrv, 1);
    m_shBufferState = RHIResourceState::ShaderResource;

    m_probeUpdateCursor = (probeOffset + probeBudget) % kTotalProbes;
    m_completedGridOrigin = m_gridOrigin;
    m_pendingHistoryShift[0] = m_pendingHistoryShift[1] = m_pendingHistoryShift[2] = 0;
    m_dirty = false;
    m_firstWritePending = false;
    m_historyInvalid = false;
    QueueDiagnosticReadback(cmdList, frameIndex);
    if (m_diagnosticCopyFrame == frameIndex) {
        UZ_LOG(Renderer, Info, "RC diagnostic: frame={} pfEnabled={} pfSHSrv={} pfGiIntensity={} skySRV={} skyMultiplier=({},{},{}) lightColor=({},{},{}) lightDir=({},{},{}) probeOffset={} probeBudget={} firstWrite={} historyShift=({},{},{})",
               frameIndex, pf->radianceCacheEnabled, pf->rcProbeSHBufferSrv, pf->rcGiIntensity, rootConstants[5],
               constants.skyColorR, constants.skyColorG, constants.skyColorB,
               constants.lightColorR, constants.lightColorG, constants.lightColorB,
               constants.lightDirX, constants.lightDirY, constants.lightDirZ, probeOffset, probeBudget, constants.firstWrite,
               constants.historyShiftX, constants.historyShiftY, constants.historyShiftZ);
    }
}

} // namespace uz
