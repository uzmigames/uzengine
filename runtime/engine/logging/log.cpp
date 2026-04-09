/// UzEngine Logging System Implementation
///
/// Initializes spdlog with colored stdout + rotating file sinks.
/// Manages per-category loggers with runtime severity filtering.
/// Thread-safe logging via spdlog's internal mutex.

#include "log.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <vector>

/// Built-in log category definitions (global scope, matching declarations in header)
///
/// Each category starts at Trace level, allowing compile-time and runtime filtering.
UZ_DEFINE_LOG_CATEGORY(Core, Trace);
UZ_DEFINE_LOG_CATEGORY(RHI, Trace);
UZ_DEFINE_LOG_CATEGORY(Renderer, Trace);
UZ_DEFINE_LOG_CATEGORY(Scene, Trace);
UZ_DEFINE_LOG_CATEGORY(Physics, Trace);
UZ_DEFINE_LOG_CATEGORY(Audio, Trace);
UZ_DEFINE_LOG_CATEGORY(Input, Trace);
UZ_DEFINE_LOG_CATEGORY(Platform, Trace);
UZ_DEFINE_LOG_CATEGORY(Memory, Trace);
UZ_DEFINE_LOG_CATEGORY(IO, Trace);
UZ_DEFINE_LOG_CATEGORY(Engine, Trace);
UZ_DEFINE_LOG_CATEGORY(Asset, Trace);
UZ_DEFINE_LOG_CATEGORY(Shadow, Trace);

namespace uz {

/// Internal logging state
/// - g_sinks: Shared sinks (console + file) used by all loggers
/// - g_loggerMutex: Protects g_loggers map (thread-safe logger creation)
/// - g_loggers: Cache of logger instances by category name
/// - g_initialized: Tracks initialization state

static std::vector<spdlog::sink_ptr> g_sinks;
static std::mutex g_loggerMutex;
static std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> g_loggers;
static bool g_initialized = false;
static spdlog::level::level_enum g_runtimeLevel = spdlog::level::err;
static std::string g_logFilePath;

/// Builds the default log file path in the user's local app data directory.
///
/// On Windows: %LOCALAPPDATA%/UzEngine/logs/uzengine_YYYY-MM-DD_HH-MM-SS.log
/// On Linux/macOS: ~/.local/share/uzengine/logs/uzengine_YYYY-MM-DD_HH-MM-SS.log
///
/// Creates the directory tree if it doesn't exist. Falls back to "uzengine.log"
/// in the current working directory if the directory cannot be created.
static std::string BuildDefaultLogPath() {
    // Resolve the base directory for log storage
    std::filesystem::path baseDir;
#ifdef _WIN32
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData && localAppData[0] != '\0') {
        baseDir = std::filesystem::path(localAppData) / "UzEngine" / "logs";
    }
#else
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        baseDir = std::filesystem::path(home) / ".local" / "share" / "uzengine" / "logs";
    }
#endif

    // Fall back to CWD if the environment variable is missing
    if (baseDir.empty()) {
        return "uzengine.log";
    }

    // Create the directory tree (no-op if it already exists)
    std::error_code ec;
    std::filesystem::create_directories(baseDir, ec);
    if (ec) {
        return "uzengine.log";
    }

    // Format timestamp: uzengine_YYYY-MM-DD_HH-MM-SS.log
    std::time_t now = std::time(nullptr);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif

    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H-%M-%S", &localTime);

    std::filesystem::path logFile = baseDir / ("uzengine_" + std::string(timestamp) + ".log");
    return logFile.string();
}

/// Initializes logging with console and optional file output.
///
/// # Setup
/// - **Console**: stdout_color_sink with timestamp, category, colored level, message
/// - **File**: rotating_file_sink (10MB per file, keeps 3 files)
///
/// # Pattern
/// Console: `[2024-03-10 14:32:45.123] [RHI] [ERROR] Device lost`
/// File: Same but without color codes
void LogInit(const char* logFilePath) {
    if (g_initialized)
        return;

    // Resolve the log file path. The default sentinel "uzengine.log" (or null/empty)
    // triggers auto-computation to a timestamped file in the user's app data directory.
    // Explicit paths (e.g., from tests) are used as-is.
    std::string resolvedPath;
    if (!logFilePath || logFilePath[0] == '\0' ||
        std::string_view(logFilePath) == "uzengine.log") {
        resolvedPath = BuildDefaultLogPath();
    } else {
        resolvedPath = logFilePath;
    }

    // Console sink with color support
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
    g_sinks.push_back(consoleSink);

    // File sink: rotating (10MB max, 3 files kept)
    if (!resolvedPath.empty()) {
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            resolvedPath, 10 * 1024 * 1024, 3);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
        g_sinks.push_back(fileSink);
    }

    g_logFilePath = std::move(resolvedPath);
    g_initialized = true;
}

/// Removes the console sink from all loggers so stdout stays clean.
///
/// Call after LogInit(). Existing loggers are updated to remove
/// the console sink — only the file sink remains active.
void LogSuppressConsole() {
    if (!g_initialized) return;

    std::lock_guard<std::mutex> lock(g_loggerMutex);

    // Remove console sink (first sink added in LogInit is always the console)
    if (!g_sinks.empty()) {
        g_sinks.erase(g_sinks.begin());
    }

    // Rebuild all cached loggers with the remaining sinks
    for (auto& [name, logger] : g_loggers) {
        auto newLogger = std::make_shared<spdlog::logger>(
            std::string(name), g_sinks.begin(), g_sinks.end());
        newLogger->set_level(logger->level());
        newLogger->flush_on(spdlog::level::err);
        logger = newLogger;
    }
}

/// Adds a sink to the global sink list and retrofits it into all existing loggers.
///
/// This solves the problem where a callback sink registered after LogInit()
/// is not picked up by loggers created before the sink was added (BUG-8).
void LogAddSink(spdlog::sink_ptr sink) {
    if (!g_initialized || !sink) return;

    std::lock_guard<std::mutex> lock(g_loggerMutex);
    g_sinks.push_back(sink);

    // Rebuild all cached loggers to include the new sink.
    for (auto& [name, logger] : g_loggers) {
        auto newLogger = std::make_shared<spdlog::logger>(
            std::string(name), g_sinks.begin(), g_sinks.end());
        newLogger->set_level(logger->level());
        newLogger->flush_on(spdlog::level::err);
        logger = newLogger;
    }
}

/// Removes a sink from the global sink list and retrofits into all existing loggers.
void LogRemoveSink(spdlog::sink_ptr sink) {
    if (!g_initialized || !sink) return;

    std::lock_guard<std::mutex> lock(g_loggerMutex);
    g_sinks.erase(std::remove(g_sinks.begin(), g_sinks.end(), sink), g_sinks.end());

    for (auto& [name, logger] : g_loggers) {
        auto newLogger = std::make_shared<spdlog::logger>(
            std::string(name), g_sinks.begin(), g_sinks.end());
        newLogger->set_level(logger->level());
        newLogger->flush_on(spdlog::level::err);
        logger = newLogger;
    }
}

/// Flushes all active loggers immediately.
void LogFlushAll() {
    if (!g_initialized) return;
    spdlog::apply_all([](std::shared_ptr<spdlog::logger> l) { l->flush(); });
}

/// Returns the resolved path of the current log file.
///
/// Returns an empty string if LogInit() has not been called yet.
const std::string& LogGetFilePath() {
    return g_logFilePath;
}

/// Shuts down logging and releases all resources.
///
/// # Behavior
/// - Flushes all pending messages to sinks
/// - Clears all cached logger instances
/// - Clears all sinks
/// - Sets g_initialized = false (allows re-init)
void LogShutdown() {
    if (!g_initialized)
        return;

    spdlog::shutdown();

    std::lock_guard<std::mutex> lock(g_loggerMutex);
    g_loggers.clear();
    g_sinks.clear();
    g_logFilePath.clear();
    g_runtimeLevel = spdlog::level::err;
    g_initialized = false;
}

/// Gets or creates a logger for a category (lazy initialization).
///
/// # Thread Safety
/// Protected by g_loggerMutex — multiple threads can safely call concurrently.
///
/// # Lazy Initialization
/// Loggers are created on first access for a category, then cached.
/// Subsequent calls for the same category are O(1) hash lookup.
///
/// # Returns
/// - Pointer to spdlog logger (never nullptr if initialized)
/// - nullptr if LogInit() not called
spdlog::logger* LogGetLogger(const LogCategory& cat) {
    if (!g_initialized)
        return nullptr;

    std::lock_guard<std::mutex> lock(g_loggerMutex);

    auto it = g_loggers.find(cat.name);
    if (it != g_loggers.end()) {
        return it->second.get();
    }

    // Create new logger with shared sinks (console + file)
    auto logger = std::make_shared<spdlog::logger>(cat.name, g_sinks.begin(), g_sinks.end());
    auto minLevel = std::max(detail::ToSpdlogLevel(cat.compileTimeMin), g_runtimeLevel);
    logger->set_level(minLevel);
    logger->flush_on(spdlog::level::err); // Auto-flush on Error and Fatal

    g_loggers[cat.name] = logger;
    return logger.get();
}

/// Updates runtime log level for all active loggers.
///
/// # Behavior
/// - Iterates all cached loggers and updates their level
/// - Does not affect compile-time filtering (that's in UZ_LOG macro)
/// - Thread-safe via mutex
///
/// # Use Case
/// Dynamically lower verbosity at runtime without recompiling:
/// ```cpp
/// LogSetLevel(LogLevel::Warning);  // Hide Info/Debug/Trace
/// ```
void LogSetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_loggerMutex);
    auto spdLevel = detail::ToSpdlogLevel(level);
    g_runtimeLevel = spdLevel;
    for (auto& [name, logger] : g_loggers) {
        logger->set_level(spdLevel);
    }
}

} // namespace uz
