// ---------------------------------------------------------------------------
// ipc_poller.cpp -- IpcPoller implementation.
//
// A background thread that:
//   1. Polls command.json every 50 ms for new commands (mtime-based check).
//   2. Dispatches commands to the IpcRegistry and writes response.json.
//   3. Writes status.json every ~1000 ms with live engine metrics.
//
// File writes are atomic: write to .tmp, then MoveFileExA with
// MOVEFILE_REPLACE_EXISTING to avoid partial reads by external clients.
// ---------------------------------------------------------------------------

#include "ipc_poller.h"

#include <chrono>
#include <fstream>
#include <sstream>

namespace uz {

namespace {

// Compare two FILETIME values.  Returns true if a != b.
bool fileTimeChanged(const FILETIME& a, const FILETIME& b) noexcept {
    return a.dwLowDateTime != b.dwLowDateTime
        || a.dwHighDateTime != b.dwHighDateTime;
}

// Read the entire contents of a file into a string.  Returns empty on failure.
std::string readFileContents(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Write |content| to |path| atomically via a .tmp rename.
void writeFileAtomic(const std::string& path, const std::string& content) {
    const std::string tmpPath = path + ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f) {
            return;
        }
        f << content;
    }
    MoveFileExA(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

IpcPoller::IpcPoller(std::string baseDir, IpcRegistry& registry,
                     SceneParams& scene, EngineMetrics& metrics)
    : baseDir_(std::move(baseDir))
    , registry_(registry)
    , scene_(scene)
    , metrics_(metrics)
{
    QueryPerformanceFrequency(&qpcFreq_);
    QueryPerformanceCounter(&startTime_);
}

IpcPoller::~IpcPoller() {
    stop();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void IpcPoller::start() {
    if (running_.load()) {
        return;
    }

    // Ensure the preview directory exists.
    CreateDirectoryA(baseDir_.c_str(), nullptr);

    running_.store(true);
    thread_ = std::thread(&IpcPoller::threadFn, this);
}

void IpcPoller::stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
}

// ---------------------------------------------------------------------------
// Thread body
// ---------------------------------------------------------------------------

void IpcPoller::threadFn() {
    using Clock = std::chrono::steady_clock;
    auto lastStatusWrite = Clock::now();

    // Write an initial status.json so clients know the engine is alive.
    writeStatus();

    while (running_.load()) {
        // Poll command.json for new commands.
        try {
            pollCommand();
        } catch (...) {
            // Never crash the poller thread on malformed input.
        }

        // Write status.json every ~1000 ms.
        const auto now = Clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastStatusWrite).count() >= 1000)
        {
            try {
                writeStatus();
            } catch (...) {
                // Swallow errors -- best-effort status reporting.
            }
            lastStatusWrite = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Final status: engine shutting down.
    try {
        nlohmann::json status;
        status["alive"] = false;
        writeFileAtomic(baseDir_ + "/status.json", status.dump(2));
    } catch (...) {
        // Best effort.
    }
}

// ---------------------------------------------------------------------------
// Command polling
// ---------------------------------------------------------------------------

void IpcPoller::pollCommand() {
    const std::string commandPath = baseDir_ + "/command.json";

    // Check mtime with a lightweight Win32 call (no file open).
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExA(commandPath.c_str(), GetFileExInfoStandard, &fad)) {
        return; // File does not exist yet -- nothing to do.
    }

    // Skip if mtime has not changed since our last read.
    if (!fileTimeChanged(fad.ftLastWriteTime, lastCommandTime_)) {
        return;
    }
    lastCommandTime_ = fad.ftLastWriteTime;

    // Read and parse the command.
    const std::string raw = readFileContents(commandPath);
    if (raw.empty()) {
        return;
    }

    nlohmann::json cmd;
    try {
        cmd = nlohmann::json::parse(raw);
    } catch (const nlohmann::json::parse_error&) {
        // Malformed JSON -- write an error response if we can extract an id.
        return;
    }

    // Dedup: skip if we already processed this command id.
    const std::string id = cmd.value("id", "");
    if (id.empty() || id == lastCommandId_) {
        return;
    }
    lastCommandId_ = id;

    // Dispatch to the registry.
    const std::string function = cmd.value("function", "");
    const nlohmann::json args  = cmd.value("args", nlohmann::json::object());

    nlohmann::json result;
    try {
        result = registry_.call(function, args);
    } catch (const std::exception& e) {
        result = {{"ok", false}, {"error", std::string("exception: ") + e.what()}};
    }

    // Build and write the response.
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const double timestampMs = static_cast<double>(now.QuadPart - startTime_.QuadPart)
                             * 1000.0 / static_cast<double>(qpcFreq_.QuadPart);

    nlohmann::json response;
    response["id"]           = id;
    response["ok"]           = result.value("ok", true);
    response["result"]       = result;
    response["error"]        = result.value("error", nullptr);
    response["timestamp_ms"] = timestampMs;

    writeResponse(response);
}

// ---------------------------------------------------------------------------
// Status writing
// ---------------------------------------------------------------------------

void IpcPoller::writeStatus() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const double uptimeS = static_cast<double>(now.QuadPart - startTime_.QuadPart)
                         / static_cast<double>(qpcFreq_.QuadPart);

    // Read background color under lock.
    float bgR = 0.1f;
    float bgG = 0.1f;
    float bgB = 0.1f;
    {
        std::lock_guard<std::mutex> lk(scene_.bgMutex);
        bgR = scene_.bgR;
        bgG = scene_.bgG;
        bgB = scene_.bgB;
    }

    // Geometry name from type index.
    const int geoType = scene_.geometryType.load();
    const char* geoName = (geoType == 1) ? "cube" : "pyramid";

    nlohmann::json status;
    status["alive"]      = true;
    status["fps"]        = metrics_.fps.load();
    status["frame"]      = metrics_.frameCount.load();
    status["uptime_s"]   = static_cast<int>(uptimeS);
    status["encoder"]    = metrics_.encoderName;
    status["resolution"] = metrics_.resolution;

    status["scene"] = {
        {"rotation_speed", {scene_.rotX.load(), scene_.rotY.load(), scene_.rotZ.load()}},
        {"background",     {bgR, bgG, bgB}},
        {"geometry",       geoName},
        {"paused",         scene_.paused.load()}
    };

    writeFileAtomic(baseDir_ + "/status.json", status.dump(2));
}

// ---------------------------------------------------------------------------
// Response writing
// ---------------------------------------------------------------------------

void IpcPoller::writeResponse(const nlohmann::json& response) {
    writeFileAtomic(baseDir_ + "/response.json", response.dump(2));
}

} // namespace uz
