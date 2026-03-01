#pragma once

// ---------------------------------------------------------------------------
// ipc_poller.h -- File-based IPC polling thread.
//
// Monitors build/preview/command.json for incoming commands from external
// clients (e.g. AI tools, VSCode extension).  Writes response.json after
// each command and status.json every ~1 second with live engine metrics.
// ---------------------------------------------------------------------------

#include <atomic>
#include <string>
#include <thread>

#include <windows.h>

#include <nlohmann/json.hpp>

#include "ipc_registry.h"
#include "../renderer/scene_params.h"

namespace uz {

// Live engine metrics, updated by the main loop, read by the poller thread.
struct EngineMetrics {
    std::atomic<float>    fps{0.0f};
    std::atomic<uint32_t> frameCount{0};
    std::string           encoderName; // set once at startup, read-only after
    std::string           resolution;  // set once at startup, read-only after
};

class IpcPoller {
public:
    // baseDir: absolute path to the directory containing the IPC files,
    // e.g. "E:/UzEngine/build/preview".
    explicit IpcPoller(std::string baseDir, IpcRegistry& registry,
                       SceneParams& scene, EngineMetrics& metrics);

    ~IpcPoller();

    // Non-copyable, non-movable (owns a thread).
    IpcPoller(const IpcPoller&)            = delete;
    IpcPoller& operator=(const IpcPoller&) = delete;
    IpcPoller(IpcPoller&&)                 = delete;
    IpcPoller& operator=(IpcPoller&&)      = delete;

    // Starts the polling thread.  Call once at startup.
    void start();

    // Signals the polling thread to stop and joins it.
    void stop();

private:
    void threadFn();
    void pollCommand();
    void writeStatus();
    void writeResponse(const nlohmann::json& response);

    std::string   baseDir_;
    IpcRegistry&  registry_;
    SceneParams&  scene_;
    EngineMetrics& metrics_;

    std::atomic<bool> running_{false};
    std::thread       thread_;

    FILETIME    lastCommandTime_{};  // last observed mtime of command.json
    std::string lastCommandId_;      // dedup: skip re-processing same id

    LARGE_INTEGER startTime_{};      // QPC at construction for uptime_s
    LARGE_INTEGER qpcFreq_{};
};

} // namespace uz
