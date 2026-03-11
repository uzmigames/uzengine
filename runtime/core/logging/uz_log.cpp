#include "uz_log.h"

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

    // Console sink with color support
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
    g_sinks.push_back(consoleSink);

    // File sink: rotating (10MB max, 3 files kept)
    if (logFilePath && logFilePath[0] != '\0') {
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFilePath,
                                                                               10 * 1024 * 1024, 3);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
        g_sinks.push_back(fileSink);
    }

    g_initialized = true;
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
    logger->set_level(detail::ToSpdlogLevel(cat.compileTimeMin));
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
    for (auto& [name, logger] : g_loggers) {
        logger->set_level(spdLevel);
    }
}

} // namespace uz
