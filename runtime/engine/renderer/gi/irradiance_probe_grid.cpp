/// UzEngine IrradianceProbeGrid — Implementation.
///
/// Allocates a structured buffer sized to the volume's probe count and
/// provides CPU-side fill + GPU upload. Compute-based ray-march updates
/// (tasks 3.x / 4.x) will read/write the same buffer via UAV.

#include "irradiance_probe_grid.h"

#include "engine/ecs/environment/light_probe_volume_component.h"
#include "engine/renderer/ibl/sh_projection.h"
#include "log.h"
#include "rhi.h"

#include <cstdlib>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────
// Destruction
// ─────────────────────────────────────────────────────────────────────────

IrradianceProbeGrid::~IrradianceProbeGrid() {
    DestroyResources();
}

// ─────────────────────────────────────────────────────────────────────────
// CreateResources
// ─────────────────────────────────────────────────────────────────────────

bool IrradianceProbeGrid::CreateResources(IRHIDevice*                      device,
                                          const LightProbeVolumeComponent& volume) {
    if (!device) {
        UZ_LOG(Renderer, Error, "IrradianceProbeGrid::CreateResources: null device");
        return false;
    }

    if (m_initialized) {
        DestroyResources();
    }

    m_device = device;
    return Rebuild(volume);
}

// ─────────────────────────────────────────────────────────────────────────
// DestroyResources
// ─────────────────────────────────────────────────────────────────────────

void IrradianceProbeGrid::DestroyResources() {
    if (m_device && m_shBuffer.IsValid()) {
        m_device->DestroyBuffer(m_shBuffer);
    }
    m_shBuffer   = {};
    m_srvIndex   = UINT32_MAX;
    m_uavIndex   = UINT32_MAX;

    std::free(m_cpuProbes);
    m_cpuProbes     = nullptr;
    m_cpuProbeCount = 0;

    m_dimX = m_dimY = m_dimZ = 0;
    m_initialized = false;
}

// ─────────────────────────────────────────────────────────────────────────
// Rebuild
// ─────────────────────────────────────────────────────────────────────────

bool IrradianceProbeGrid::Rebuild(const LightProbeVolumeComponent& volume) {
    if (!m_device) {
        UZ_LOG(Renderer, Error, "IrradianceProbeGrid::Rebuild: no device bound");
        return false;
    }

    const uint32_t probeCount =
        volume.gridDimensionsX * volume.gridDimensionsY * volume.gridDimensionsZ;

    if (probeCount == 0) {
        UZ_LOG(Renderer, Warning, "IrradianceProbeGrid::Rebuild: zero probes requested");
        return false;
    }

    if (probeCount > kMaxProbes) {
        UZ_LOG(Renderer, Error,
               "IrradianceProbeGrid::Rebuild: probeCount {} exceeds cap {}",
               probeCount, kMaxProbes);
        return false;
    }

    // Re-allocate the GPU buffer only when the probe count changes.
    if (probeCount != m_cpuProbeCount) {
        if (!AllocateBuffer(probeCount)) {
            return false;
        }
    }

    m_dimX      = volume.gridDimensionsX;
    m_dimY      = volume.gridDimensionsY;
    m_dimZ      = volume.gridDimensionsZ;
    m_extentMin = volume.extentMin;
    m_extentMax = volume.extentMax;

    ClearCpuProbes();
    m_initialized = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// AllocateBuffer
// ─────────────────────────────────────────────────────────────────────────

bool IrradianceProbeGrid::AllocateBuffer(uint32_t probeCount) {
    if (m_shBuffer.IsValid()) {
        m_device->DestroyBuffer(m_shBuffer);
        m_shBuffer = {};
        m_srvIndex = UINT32_MAX;
        m_uavIndex = UINT32_MAX;
    }

    const uint32_t bufferBytes = probeCount * kBytesPerProbe;

    RHIBufferDesc bufDesc{};
    bufDesc.sizeBytes = bufferBytes;
    bufDesc.stride    = 16; // float4
    // Upload-heap structured buffer — CPU writes via persistent map, GPU
    // reads via SRV. UAV is intentionally omitted for now: compute writes
    // happen through the radiance cache / DDGI shaders into their own
    // default-heap buffers and atlases (LPV is a CPU-driven seed today).
    bufDesc.usage     = RHIBufferUsage::StructuredBuffer;
    bufDesc.memory    = RHIMemoryType::Upload;
    bufDesc.debugName = "IrradianceProbeGrid_SH";

    m_shBuffer = m_device->CreateBuffer(bufDesc);
    if (!m_shBuffer.IsValid()) {
        UZ_LOG(Renderer, Error,
               "IrradianceProbeGrid: failed to allocate SH buffer ({} probes, {} bytes)",
               probeCount, bufferBytes);
        return false;
    }

    m_srvIndex = m_device->GetBufferSRVIndex(m_shBuffer);
    m_uavIndex = m_device->GetBufferUAVIndex(m_shBuffer);

    // ── Resize CPU-side staging ───────────────────────────────────────────
    std::free(m_cpuProbes);
    const uint32_t floatCount = probeCount * kSHFloat4PerProbe * 4;
    m_cpuProbes     = static_cast<float*>(std::calloc(floatCount, sizeof(float)));
    m_cpuProbeCount = probeCount;

    if (!m_cpuProbes) {
        UZ_LOG(Renderer, Error,
               "IrradianceProbeGrid: CPU probe allocation failed ({} floats)",
               floatCount);
        m_device->DestroyBuffer(m_shBuffer);
        m_shBuffer      = {};
        m_srvIndex      = UINT32_MAX;
        m_uavIndex      = UINT32_MAX;
        m_cpuProbeCount = 0;
        return false;
    }

    UZ_LOG(Renderer, Info,
           "IrradianceProbeGrid: allocated {} probes ({} KB, SRV={}, UAV={})",
           probeCount, bufferBytes / 1024, m_srvIndex, m_uavIndex);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// FillFromSkySH
// ─────────────────────────────────────────────────────────────────────────

void IrradianceProbeGrid::FillFromSkySH(const SHCoefficients& skySH) {
    if (!m_cpuProbes || m_cpuProbeCount == 0) {
        return;
    }

    // Broadcast the 7-float4 sky SH into every probe slot.
    // The source layout mirrors the destination layout exactly.
    const float* src = reinterpret_cast<const float*>(&skySH.coeffs[0]);
    for (uint32_t p = 0; p < m_cpuProbeCount; ++p) {
        float* dst = m_cpuProbes + p * kSHFloat4PerProbe * 4;
        std::memcpy(dst, src, kBytesPerProbe);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// CaptureFromSkyCubemap
// ─────────────────────────────────────────────────────────────────────────

bool IrradianceProbeGrid::CaptureFromSkyCubemap(IRHICommandList* cmdList,
                                                RHITextureHandle skyCubemap,
                                                uint32_t         faceSize) {
    if (!m_device || !m_cpuProbes || m_cpuProbeCount == 0) {
        UZ_LOG(Renderer, Warning,
               "IrradianceProbeGrid::CaptureFromSkyCubemap: grid not initialized");
        return false;
    }
    if (!cmdList || !skyCubemap.IsValid() || faceSize == 0) {
        UZ_LOG(Renderer, Error,
               "IrradianceProbeGrid::CaptureFromSkyCubemap: invalid arguments");
        return false;
    }

    // Project the sky cubemap to L2 SH once. Sky is at infinity, so the
    // projection is identical for every probe position — spatial variation
    // is introduced by the SDF/DXR update passes (tasks 3.x / 4.x).
    const SHCoefficients skySH =
        ProjectCubemapToSH(m_device, cmdList, skyCubemap, faceSize);

    FillFromSkySH(skySH);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// ClearCpuProbes
// ─────────────────────────────────────────────────────────────────────────

void IrradianceProbeGrid::ClearCpuProbes() {
    if (!m_cpuProbes || m_cpuProbeCount == 0) {
        return;
    }
    const size_t bytes = static_cast<size_t>(m_cpuProbeCount) * kBytesPerProbe;
    std::memset(m_cpuProbes, 0, bytes);
}

// ─────────────────────────────────────────────────────────────────────────
// UploadToGpu
// ─────────────────────────────────────────────────────────────────────────

void IrradianceProbeGrid::UploadToGpu() {
    if (!m_device || !m_cpuProbes || !m_shBuffer.IsValid()) {
        return;
    }

    const uint32_t bytes = m_cpuProbeCount * kBytesPerProbe;
    void* mapped = m_device->MapBuffer(m_shBuffer);
    if (!mapped) {
        UZ_LOG(Renderer, Error, "IrradianceProbeGrid::UploadToGpu: MapBuffer failed");
        return;
    }
    std::memcpy(mapped, m_cpuProbes, bytes);
    m_device->UnmapBuffer(m_shBuffer);
}

// ─────────────────────────────────────────────────────────────────────────
// GetProbeSpacing
// ─────────────────────────────────────────────────────────────────────────

Vec3 IrradianceProbeGrid::GetProbeSpacing() const noexcept {
    // Spacing is the distance between adjacent probe cells. With N probes
    // along an axis the endpoints sit at extentMin and extentMax, so the
    // spacing divisor is (N - 1), clamped to 1 to avoid divide-by-zero
    // when a single-probe axis is configured.
    const float dx = m_extentMax.x - m_extentMin.x;
    const float dy = m_extentMax.y - m_extentMin.y;
    const float dz = m_extentMax.z - m_extentMin.z;
    const float nx = static_cast<float>(m_dimX > 1 ? m_dimX - 1 : 1);
    const float ny = static_cast<float>(m_dimY > 1 ? m_dimY - 1 : 1);
    const float nz = static_cast<float>(m_dimZ > 1 ? m_dimZ - 1 : 1);
    return Vec3{dx / nx, dy / ny, dz / nz};
}

} // namespace uz
