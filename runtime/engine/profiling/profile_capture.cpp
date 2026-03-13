/// ProfileCapture Implementation
///
/// Builds a call tree from enter/exit events and exports to Chrome DevTools
/// `.cpuprofile` JSON format. Uses QueryPerformanceCounter for timestamps
/// on Windows, clock_gettime(CLOCK_MONOTONIC) on Linux.

#include "profile_capture.h"
#include "../containers/fnv_hash.h"
#include "../logging/log.h"
#include "profiler.h"

#include <json.hpp>

#include "file_system.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

// ────────────────────────────────────────────────────────────────────────────
// Log Category
// ────────────────────────────────────────────────────────────────────────────

UZ_DEFINE_LOG_CATEGORY(Profiling, Trace);

namespace uz {
namespace {

/// Returns a high-resolution monotonic timestamp (platform-specific raw value).
[[nodiscard]] uint64_t GetTimestamp() {
#ifdef _WIN32
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>(counter.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
#endif
}

/// Returns the timer frequency (ticks per second).
[[nodiscard]] uint64_t GetTimerFrequency() {
#ifdef _WIN32
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return static_cast<uint64_t>(freq.QuadPart);
#else
    return 1'000'000'000ULL; // nanoseconds
#endif
}

/// Converts a raw timestamp delta to microseconds.
[[nodiscard]] uint64_t TicksToMicroseconds(uint64_t ticks, uint64_t frequency) {
    // Multiply first to avoid precision loss, but guard against overflow
    // by doing the division in two steps for large tick counts.
    if (frequency == 0) {
        return 0;
    }
    return (ticks * 1'000'000ULL) / frequency;
}

/// Strips the absolute path prefix, keeping only the portion after "runtime/".
/// If "runtime/" is not found, returns the full path.
///
/// # Returns
/// A view into the original string (no allocation).
[[nodiscard]] std::string_view StripPathPrefix(const char* fullPath) {
    if (fullPath == nullptr) {
        return "";
    }
    std::string_view sv(fullPath);

    // Normalize backslashes for search
    // Look for "runtime/" or "runtime\" in the path
    auto pos = sv.find("runtime/");
    if (pos == std::string_view::npos) {
        pos = sv.find("runtime\\");
    }
    if (pos != std::string_view::npos) {
        return sv.substr(pos);
    }
    return sv;
}

/// Key for identifying unique call-tree nodes: (name, file, line).
struct ScopeKey {
    const char* name;
    const char* file;
    uint32_t line;

    bool operator==(const ScopeKey& other) const {
        return line == other.line && std::strcmp(name, other.name) == 0 &&
               std::strcmp(file, other.file) == 0;
    }
};

/// Hash for ScopeKey.
struct ScopeKeyHash {
    size_t operator()(const ScopeKey& k) const {
        uint64_t h = kFnv64OffsetBasis;
        h = FnvMixString(h, k.name);
        h = FnvMixString(h, k.file);
        h = FnvMix(h, static_cast<uint64_t>(k.line));
        return static_cast<size_t>(h);
    }
};

/// A node in the reconstructed call tree.
struct TreeNode {
    int id;
    ScopeKey key;
    std::vector<int> children;     ///< Child node IDs (indices into nodes vector)
    uint64_t firstEnterTime = 0;   ///< Earliest enter timestamp (for ordering)
};

} // anonymous namespace

// ────────────────────────────────────────────────────────────────────────────
// ProfileCapture
// ────────────────────────────────────────────────────────────────────────────

ProfileCapture::ProfileCapture() : m_qpcFrequency(GetTimerFrequency()) {}

ProfileCapture& ProfileCapture::Get() {
    static ProfileCapture instance;
    return instance;
}

void ProfileCapture::StartCapture() {
    if (m_capturing.load(std::memory_order_relaxed)) {
        return; // Already capturing
    }

    m_events.clear();
    m_events.resize(kMaxEvents);
    m_eventCount.store(0, std::memory_order_relaxed);
    m_captureStartTime = GetTimestamp();
    m_capturing.store(true, std::memory_order_release);

    UZ_LOG(Profiling, Info, "Profile capture started (buffer: {} events)", kMaxEvents);
}

bool ProfileCapture::StopAndExport(const std::string& path) {
    UZ_PROFILE_FUNCTION();

    if (!m_capturing.exchange(false, std::memory_order_acq_rel)) {
        UZ_LOG(Profiling, Error, "StopAndExport called but no capture was active");
        return false;
    }

    const size_t eventCount =
        std::min(m_eventCount.load(std::memory_order_acquire), kMaxEvents);
    const uint64_t captureEndTime = GetTimestamp();

    UZ_LOG(Profiling, Info, "Profile capture stopped: {} events recorded", eventCount);

    if (eventCount == 0) {
        UZ_LOG(Profiling, Warning, "No events captured, skipping export");
        m_events.clear();
        return false;
    }

    // ── Build call tree from enter/exit events ──────────────────────────────

    // Node ID 1 is the root "(root)" node
    std::vector<TreeNode> nodes;
    nodes.push_back(TreeNode{
        1,
        ScopeKey{"(root)", "", 0},
        {},
        m_captureStartTime,
    });

    // Maps a ScopeKey to its node ID (1-based)
    std::unordered_map<ScopeKey, int, ScopeKeyHash> keyToNodeId;

    // Stack of (nodeId) representing the current call depth.
    // Starts with root on the stack.
    std::vector<int> callStack;
    callStack.push_back(1); // root

    // Track which children each parent has seen, to avoid duplicates in children[]
    // Key: parentNodeId, Value: set of child node IDs already added
    std::unordered_map<int, std::vector<int>> parentChildMap;

    for (size_t i = 0; i < eventCount; ++i) {
        const auto& event = m_events[i];

        if (event.type == ProfileEventType::Enter) {
            const ScopeKey key{event.name, event.file, event.line};

            int nodeId = 0;
            auto it = keyToNodeId.find(key);
            if (it != keyToNodeId.end()) {
                nodeId = it->second;
            } else {
                nodeId = static_cast<int>(nodes.size()) + 1; // 1-based
                keyToNodeId[key] = nodeId;
                nodes.push_back(TreeNode{nodeId, key, {}, event.timestamp});
            }

            // Add as child of current top-of-stack (parent)
            const int parentId = callStack.back();
            auto& parentChildren = parentChildMap[parentId];
            if (std::find(parentChildren.begin(), parentChildren.end(), nodeId) ==
                parentChildren.end()) {
                parentChildren.push_back(nodeId);
                // Also add to the TreeNode's children list
                // Find the parent node (id is 1-based, index is id-1)
                nodes[static_cast<size_t>(parentId - 1)].children.push_back(nodeId);
            }

            callStack.push_back(nodeId);
        } else {
            // Exit — pop the stack (guard against underflow past root)
            if (callStack.size() > 1) {
                callStack.pop_back();
            }
        }
    }

    // ── Generate samples and timeDeltas ─────────────────────────────────────

    // Replay events to determine the deepest active scope at regular intervals.
    // Sample interval: 10 microseconds (100kHz sampling rate).
    constexpr uint64_t kSampleIntervalUs = 10;
    const uint64_t captureStartUs = 0;
    const uint64_t captureDurationUs =
        TicksToMicroseconds(captureEndTime - m_captureStartTime, m_qpcFrequency);

    std::vector<int> samples;
    std::vector<int64_t> timeDeltas;

    // Rebuild call stack with timestamps for sampling
    struct StackEntry {
        int nodeId;
        uint64_t enterTimeUs;
    };
    std::vector<StackEntry> sampleStack;
    sampleStack.push_back({1, 0}); // root

    size_t eventIdx = 0;
    uint64_t currentSampleTimeUs = 0;

    while (currentSampleTimeUs <= captureDurationUs) {
        // Advance events up to current sample time
        while (eventIdx < eventCount) {
            const auto& event = m_events[eventIdx];
            const uint64_t eventTimeUs =
                TicksToMicroseconds(event.timestamp - m_captureStartTime, m_qpcFrequency);

            if (eventTimeUs > currentSampleTimeUs) {
                break;
            }

            if (event.type == ProfileEventType::Enter) {
                const ScopeKey key{event.name, event.file, event.line};
                auto it = keyToNodeId.find(key);
                const int nodeId = (it != keyToNodeId.end()) ? it->second : 1;
                sampleStack.push_back({nodeId, eventTimeUs});
            } else {
                if (sampleStack.size() > 1) {
                    sampleStack.pop_back();
                }
            }
            ++eventIdx;
        }

        // Record the deepest active scope
        const int deepestNodeId = sampleStack.back().nodeId;
        samples.push_back(deepestNodeId);

        if (timeDeltas.empty()) {
            timeDeltas.push_back(0);
        } else {
            timeDeltas.push_back(static_cast<int64_t>(kSampleIntervalUs));
        }

        currentSampleTimeUs += kSampleIntervalUs;
    }

    // ── Serialize to .cpuprofile JSON ───────────────────────────────────────

    nlohmann::json jsonNodes = nlohmann::json::array();

    for (const auto& node : nodes) {
        const auto relPath = StripPathPrefix(node.key.file);
        const int lineNum = (node.id == 1) ? -1 : static_cast<int>(node.key.line);
        const std::string scriptId =
            (node.id == 1) ? "0" : std::to_string(node.id - 1);

        nlohmann::json callFrame = {
            {"functionName", node.key.name},
            {"scriptId", scriptId},
            {"url", std::string(relPath)},
            {"lineNumber", lineNum},
            {"columnNumber", (node.id == 1) ? -1 : 0},
        };

        nlohmann::json jsonNode = {
            {"id", node.id},
            {"callFrame", callFrame},
        };

        if (!node.children.empty()) {
            jsonNode["children"] = node.children;
        }

        jsonNodes.push_back(jsonNode);
    }

    nlohmann::json profile = {
        {"nodes", jsonNodes},
        {"startTime", captureStartUs},
        {"endTime", static_cast<int64_t>(captureDurationUs)},
        {"samples", samples},
        {"timeDeltas", timeDeltas},
    };

    // Write to file
    const std::string jsonText = profile.dump(2);
    const FileResult writeResult = FileWriteAllText(path.c_str(), jsonText.c_str());
    if (writeResult != FileResult::Success) {
        UZ_LOG(Profiling, Error, "Failed to write profile data: path='{}': {}",
               path, FileResultToString(writeResult));
        m_events.clear();
        return false;
    }

    UZ_LOG(Profiling, Info,
           "Profile exported: path='{}', nodes={}, samples={}, duration={}us",
           path, nodes.size(), samples.size(), captureDurationUs);

    m_events.clear();
    return true;
}

bool ProfileCapture::IsCapturing() const {
    return m_capturing.load(std::memory_order_relaxed);
}

void ProfileCapture::RecordEnter(const char* name, const char* file, uint32_t line) {
    if (!m_capturing.load(std::memory_order_relaxed)) {
        return;
    }

    const size_t idx = m_eventCount.fetch_add(1, std::memory_order_relaxed);
    if (idx >= kMaxEvents) {
        return; // Buffer full — drop event
    }

    m_events[idx] = ProfileEvent{
        name,
        file,
        line,
        GetTimestamp(),
        ProfileEventType::Enter,
    };
}

void ProfileCapture::RecordExit(const char* name) {
    if (!m_capturing.load(std::memory_order_relaxed)) {
        return;
    }

    const size_t idx = m_eventCount.fetch_add(1, std::memory_order_relaxed);
    if (idx >= kMaxEvents) {
        return; // Buffer full — drop event
    }

    m_events[idx] = ProfileEvent{
        name,
        nullptr,
        0,
        GetTimestamp(),
        ProfileEventType::Exit,
    };
}

} // namespace uz
