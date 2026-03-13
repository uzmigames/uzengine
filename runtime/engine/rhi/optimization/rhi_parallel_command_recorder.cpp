/// UzEngine Parallel Command Recorder — Implementation.
///
/// See parallel_command_recorder.h for design overview and thread-safety contract.

#include "rhi_parallel_command_recorder.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(RHI);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

ParallelCommandRecorder::~ParallelCommandRecorder() {
    if (m_device) {
        Shutdown();
    }
}

bool ParallelCommandRecorder::Init(IRHIDevice* device, const ParallelRecorderConfig& config) {
    UZ_CHECK(device != nullptr);

    if (m_device) {
        UZ_LOG(RHI, Warning, "ParallelCommandRecorder::Init called while already initialized");
        return false;
    }

    if (config.maxThreads == 0) {
        UZ_LOG(RHI, Error, "ParallelCommandRecorder::Init: maxThreads must be > 0");
        return false;
    }

    m_device = device;
    m_config = config;
    m_slots.resize(config.maxThreads);

    for (uint32_t i = 0; i < config.maxThreads; ++i) {
        m_slots[i].cmdList = device->CreateCommandList(config.queueType);
        if (!m_slots[i].cmdList) {
            UZ_LOG(RHI, Error, "ParallelCommandRecorder::Init: failed to create command list {}/{}",
                   i, config.maxThreads);
            // Clean up successfully created lists.
            for (uint32_t j = 0; j < i; ++j) {
                device->DestroyCommandList(m_slots[j].cmdList);
                m_slots[j].cmdList = nullptr;
            }
            m_slots.clear();
            m_device = nullptr;
            return false;
        }
    }

    UZ_LOG(RHI, Info, "ParallelCommandRecorder initialized: {} slots, queue={}",
           config.maxThreads, static_cast<int>(config.queueType));
    return true;
}

void ParallelCommandRecorder::Shutdown() {
    if (!m_device) {
        return;
    }

    for (auto& slot : m_slots) {
        if (slot.cmdList) {
            m_device->DestroyCommandList(slot.cmdList);
            slot.cmdList = nullptr;
        }
    }
    m_slots.clear();

    UZ_LOG(RHI, Info, "ParallelCommandRecorder shut down");
    m_device = nullptr;
    m_nextOrder = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void ParallelCommandRecorder::BeginFrame() {
    UZ_CHECK(m_device != nullptr);
    UZ_PROFILE_SCOPE("ParallelRecorder::BeginFrame");

    // No lock needed — main thread only, no workers should be active.
    m_nextOrder = 0;

    for (auto& slot : m_slots) {
        UZ_CHECK(!slot.acquired);  // Leak detection: slot still held from previous frame.
        slot.acquired = false;
        slot.recorded = false;
        slot.acquisitionOrder = 0;
        slot.cmdList->Begin();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Thread-safe acquisition / release
// ─────────────────────────────────────────────────────────────────────────────

IRHICommandList* ParallelCommandRecorder::AcquireCommandList() {
    std::lock_guard lock(m_mutex);

    for (auto& slot : m_slots) {
        if (!slot.acquired && !slot.recorded) {
            slot.acquired = true;
            slot.acquisitionOrder = m_nextOrder++;
            return slot.cmdList;
        }
    }

    UZ_LOG(RHI, Warning, "ParallelCommandRecorder::AcquireCommandList: all {} slots in use",
           m_config.maxThreads);
    return nullptr;
}

void ParallelCommandRecorder::ReleaseCommandList(IRHICommandList* cmdList) {
    if (!cmdList) {
        UZ_LOG(RHI, Warning, "ParallelCommandRecorder::ReleaseCommandList: null pointer");
        return;
    }

    std::lock_guard lock(m_mutex);

    for (auto& slot : m_slots) {
        if (slot.cmdList == cmdList) {
            if (!slot.acquired) {
                UZ_LOG(RHI, Warning, "ParallelCommandRecorder::ReleaseCommandList: "
                       "slot not acquired (double release?)");
                return;
            }
            slot.acquired = false;
            slot.recorded = true;
            return;
        }
    }

    UZ_LOG(RHI, Warning, "ParallelCommandRecorder::ReleaseCommandList: unknown command list pointer");
}

// ─────────────────────────────────────────────────────────────────────────────
// Submission
// ─────────────────────────────────────────────────────────────────────────────

uint32_t ParallelCommandRecorder::SubmitAll() {
    UZ_CHECK(m_device != nullptr);
    UZ_PROFILE_SCOPE("ParallelRecorder::SubmitAll");

    // Collect indices of recorded slots.
    std::vector<uint32_t> recordedIndices;
    recordedIndices.reserve(m_config.maxThreads);

    for (uint32_t i = 0; i < static_cast<uint32_t>(m_slots.size()); ++i) {
        if (m_slots[i].acquired) {
            UZ_LOG(RHI, Warning, "ParallelCommandRecorder::SubmitAll: slot {} still acquired "
                   "(worker thread did not release)", i);
        }
        if (m_slots[i].recorded) {
            recordedIndices.push_back(i);
        }
    }

    if (recordedIndices.empty()) {
        return 0;
    }

    // Sort by acquisition order for deterministic execution.
    std::sort(recordedIndices.begin(), recordedIndices.end(),
              [this](uint32_t a, uint32_t b) {
                  return m_slots[a].acquisitionOrder < m_slots[b].acquisitionOrder;
              });

    // Execute each command list in order.
    for (uint32_t idx : recordedIndices) {
        m_slots[idx].cmdList->Execute();
    }

    const auto count = static_cast<uint32_t>(recordedIndices.size());
    UZ_LOG(RHI, Debug, "ParallelCommandRecorder::SubmitAll: executed {} command lists", count);
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

uint32_t ParallelCommandRecorder::GetActiveCount() const {
    std::lock_guard lock(m_mutex);
    uint32_t count = 0;
    for (const auto& slot : m_slots) {
        if (slot.acquired) {
            ++count;
        }
    }
    return count;
}

} // namespace uz
