/// DX12 GPU Timestamp Profiler Implementation
///
/// Uses ID3D12QueryHeap (TIMESTAMP) with a readback buffer to measure GPU pass timing.
/// Triple-buffered: each frame writes to its own region of the query heap and readback buffer.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_gpu_profiler.h"
#include "dx12_device.h"
#include "dx12_guids.h"
#include "log.h"

namespace uz {

/// Initializes the timestamp query heap and readback buffer.
bool DX12GpuProfiler::Init(DX12Device* device) {
    m_device = device;
    auto* d3dDevice = device->GetDevice();

    // Query GPU timestamp frequency from the direct queue
    HRESULT hr = device->GetDirectQueue()->GetTimestampFrequency(&m_gpuFrequency);
    if (FAILED(hr) || m_gpuFrequency == 0) {
        UZ_LOG(RHI, Warning, "GPU timestamps not supported on this queue");
        return false;
    }

    // Create timestamp query heap (all frames share one heap)
    D3D12_QUERY_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heapDesc.Count = kTimestampsPerFrame * kProfilerFramesInFlight;
    heapDesc.NodeMask = 0;

    hr = d3dDevice->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_queryHeap));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to create timestamp query heap, hr=0x{:08X}",
               static_cast<uint32_t>(hr));
        return false;
    }

    // Create readback buffer for resolved timestamps
    uint64_t bufferSize = sizeof(uint64_t) * kTimestampsPerFrame * kProfilerFramesInFlight;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = bufferSize;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = d3dDevice->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_readbackBuffer));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to create timestamp readback buffer, hr=0x{:08X}",
               static_cast<uint32_t>(hr));
        return false;
    }

    UZ_LOG(RHI, Info, "GPU profiler initialized: frequency={}Hz, maxScopes={}/frame",
           m_gpuFrequency, kMaxGpuTimestamps);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame Recording
// ─────────────────────────────────────────────────────────────────────────────

/// Resets timestamp counter for the new frame.
void DX12GpuProfiler::BeginFrame(ID3D12GraphicsCommandList* /*cmdList*/) {
    m_timestampCount = 0;
}

/// Records a begin timestamp at the current query index.
void DX12GpuProfiler::BeginTimestamp(ID3D12GraphicsCommandList* cmdList, const char* name) {
    if (m_timestampCount >= kMaxGpuTimestamps) return;

    m_names[m_timestampCount] = name;

    uint32_t baseQuery = m_currentFrame * kTimestampsPerFrame;
    uint32_t queryIdx = baseQuery + (m_timestampCount * 2);
    cmdList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIdx);
}

/// Records an end timestamp for the most recently begun scope.
void DX12GpuProfiler::EndTimestamp(ID3D12GraphicsCommandList* cmdList, const char* /*name*/) {
    if (m_timestampCount >= kMaxGpuTimestamps) return;

    uint32_t baseQuery = m_currentFrame * kTimestampsPerFrame;
    uint32_t queryIdx = baseQuery + (m_timestampCount * 2) + 1;
    cmdList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIdx);

    m_timestampCount++;
}

/// Resolves all timestamps for this frame into the readback buffer.
void DX12GpuProfiler::EndFrame(ID3D12GraphicsCommandList* cmdList) {
    if (m_timestampCount == 0) {
        m_currentFrame = (m_currentFrame + 1) % kProfilerFramesInFlight;
        return;
    }

    uint32_t baseQuery = m_currentFrame * kTimestampsPerFrame;
    uint32_t queryCount = m_timestampCount * 2;
    uint64_t destOffset = static_cast<uint64_t>(baseQuery) * sizeof(uint64_t);

    cmdList->ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                              baseQuery, queryCount,
                              m_readbackBuffer.Get(), destOffset);

    m_currentFrame = (m_currentFrame + 1) % kProfilerFramesInFlight;
}

// ─────────────────────────────────────────────────────────────────────────────
// Readback & Resolution
// ─────────────────────────────────────────────────────────────────────────────

/// Reads timestamps from frame N-2 and computes millisecond deltas.
void DX12GpuProfiler::ResolveFrame() {
    m_results.clear();
    m_frameTimeMs = 0.0f;

    if (m_gpuFrequency == 0) return;

    // Read from the oldest frame (N-2 relative to current)
    uint32_t readFrame = m_currentFrame;  // currentFrame was already advanced in EndFrame
    uint32_t baseQuery = readFrame * kTimestampsPerFrame;

    uint64_t* mapped = nullptr;
    D3D12_RANGE readRange = {};
    readRange.Begin = baseQuery * sizeof(uint64_t);
    readRange.End = readRange.Begin + kTimestampsPerFrame * sizeof(uint64_t);

    HRESULT hr = m_readbackBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
    if (FAILED(hr)) return;

    double ticksToMs = 1000.0 / static_cast<double>(m_gpuFrequency);
    uint64_t frameBegin = UINT64_MAX;
    uint64_t frameEnd = 0;

    for (uint32_t i = 0; i < kMaxGpuTimestamps; ++i) {
        uint64_t begin = mapped[baseQuery + i * 2];
        uint64_t end   = mapped[baseQuery + i * 2 + 1];

        // Skip unwritten slots (both zero)
        if (begin == 0 && end == 0) break;

        float ms = static_cast<float>(static_cast<double>(end - begin) * ticksToMs);
        m_results.push_back({m_names[i], ms});

        if (begin < frameBegin) frameBegin = begin;
        if (end > frameEnd) frameEnd = end;
    }

    if (frameBegin < frameEnd) {
        m_frameTimeMs = static_cast<float>(static_cast<double>(frameEnd - frameBegin) * ticksToMs);
    }

    D3D12_RANGE writeRange = {0, 0};  // CPU didn't write
    m_readbackBuffer->Unmap(0, &writeRange);
}

} // namespace uz
