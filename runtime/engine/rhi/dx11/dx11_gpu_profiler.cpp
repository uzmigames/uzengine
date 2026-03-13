/// DX11 GPU Timestamp Profiler Implementation
///
/// Uses individual `ID3D11Query` objects (TIMESTAMP and TIMESTAMP_DISJOINT) to measure
/// GPU pass timing. Triple-buffered: each frame writes to its own set of pre-created
/// query objects, and results are read back from 2 frames ago via `GetData()`.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx11_gpu_profiler.h"
#include "dx11_device.h"
#include "log.h"
#include "profiler.h"

#include <algorithm>

namespace uz {

/// Creates all timestamp and disjoint query objects up front.
bool DX11GpuProfiler::Init(DX11Device* device) {
    UZ_PROFILE_FUNCTION();
    m_device = device;
    m_context = device->GetImmediateContext();

    auto* d3dDevice = device->GetDevice();

    // Create disjoint queries (one per in-flight frame)
    D3D11_QUERY_DESC disjointDesc = {};
    disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;

    for (uint32_t f = 0; f < kProfilerFramesInFlight; ++f) {
        HRESULT hr = d3dDevice->CreateQuery(&disjointDesc, &m_disjointQueries.at(f));
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "Failed to create disjoint query for frame {}, hr=0x{:08X}",
                   f, static_cast<uint32_t>(hr));
            return false;
        }
    }

    // Create timestamp queries (kTimestampsPerFrame per in-flight frame)
    D3D11_QUERY_DESC tsDesc = {};
    tsDesc.Query = D3D11_QUERY_TIMESTAMP;

    for (uint32_t f = 0; f < kProfilerFramesInFlight; ++f) {
        for (uint32_t q = 0; q < kTimestampsPerFrame; ++q) {
            HRESULT hr = d3dDevice->CreateQuery(&tsDesc, &m_timestampQueries.at(f).at(q));
            if (FAILED(hr)) {
                UZ_LOG(RHI, Error, "Failed to create timestamp query frame={} idx={}, hr=0x{:08X}",
                       f, q, static_cast<uint32_t>(hr));
                return false;
            }
        }
    }

    UZ_LOG(RHI, Info, "DX11 GPU profiler initialized: maxScopes={}/frame, framesInFlight={}",
           kMaxGpuTimestamps, kProfilerFramesInFlight);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame Recording
// ─────────────────────────────────────────────────────────────────────────────

/// Resets timestamp counter and begins the disjoint query for this frame.
void DX11GpuProfiler::BeginFrame(ID3D11DeviceContext1* ctx) {
    m_timestampCount = 0;
    ctx->Begin(m_disjointQueries.at(m_currentFrame).Get());
}

/// Records a begin timestamp at the current query index.
void DX11GpuProfiler::BeginTimestamp(ID3D11DeviceContext1* ctx, const char* name) {
    if (m_timestampCount >= kMaxGpuTimestamps) {
        return;
    }

    m_names.at(m_timestampCount) = name;

    uint32_t queryIdx = m_timestampCount * 2;
    ctx->End(m_timestampQueries.at(m_currentFrame).at(queryIdx).Get());
}

/// Records an end timestamp for the most recently begun scope.
void DX11GpuProfiler::EndTimestamp(ID3D11DeviceContext1* ctx, const char* /*name*/) {
    if (m_timestampCount >= kMaxGpuTimestamps) {
        return;
    }

    uint32_t queryIdx = (m_timestampCount * 2) + 1;
    ctx->End(m_timestampQueries.at(m_currentFrame).at(queryIdx).Get());

    m_timestampCount++;
}

/// Ends the disjoint query and advances the frame index.
void DX11GpuProfiler::EndFrame(ID3D11DeviceContext1* ctx) {
    ctx->End(m_disjointQueries.at(m_currentFrame).Get());

    // Store how many pairs were recorded so ResolveFrame knows the count
    m_framePairCounts.at(m_currentFrame) = m_timestampCount;

    m_currentFrame = (m_currentFrame + 1) % kProfilerFramesInFlight;
}

// ─────────────────────────────────────────────────────────────────────────────
// Readback & Resolution
// ─────────────────────────────────────────────────────────────────────────────

/// Reads timestamps from frame N-2 and computes millisecond deltas.
void DX11GpuProfiler::ResolveFrame() {
    UZ_PROFILE_FUNCTION();
    m_results.clear();
    m_frameTimeMs = 0.0F;

    // currentFrame was already advanced in EndFrame, so it points to the oldest frame
    uint32_t readFrame = m_currentFrame;
    uint32_t pairCount = m_framePairCounts.at(readFrame);

    if (pairCount == 0) {
        return;
    }

    // Read disjoint query result — contains frequency and validity flag
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData = {};
    HRESULT hr = m_context->GetData(
        m_disjointQueries.at(readFrame).Get(),
        &disjointData, sizeof(disjointData), 0);

    if (FAILED(hr)) {
        return;
    }

    // If GPU frequency changed mid-frame (power state transition), discard results
    if (disjointData.Disjoint != 0 || disjointData.Frequency == 0) {
        return;
    }

    double ticksToMs = 1000.0 / static_cast<double>(disjointData.Frequency);
    uint64_t frameBegin = UINT64_MAX;
    uint64_t frameEnd = 0;

    for (uint32_t i = 0; i < pairCount; ++i) {
        uint64_t begin = 0;
        uint64_t end = 0;

        auto beginIdx = static_cast<size_t>(i) * 2;
        auto endIdx = (static_cast<size_t>(i) * 2) + 1;

        hr = m_context->GetData(
            m_timestampQueries.at(readFrame).at(beginIdx).Get(),
            &begin, sizeof(begin), 0);
        if (FAILED(hr)) {
            continue;
        }

        hr = m_context->GetData(
            m_timestampQueries.at(readFrame).at(endIdx).Get(),
            &end, sizeof(end), 0);
        if (FAILED(hr)) {
            continue;
        }

        auto ms = static_cast<float>(static_cast<double>(end - begin) * ticksToMs);
        m_results.push_back({m_names.at(i), ms});

        frameBegin = std::min(frameBegin, begin);
        frameEnd = std::max(frameEnd, end);
    }

    if (frameBegin < frameEnd) {
        m_frameTimeMs = static_cast<float>(static_cast<double>(frameEnd - frameBegin) * ticksToMs);
    }
}

} // namespace uz
