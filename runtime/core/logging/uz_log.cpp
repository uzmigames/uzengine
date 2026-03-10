#include "uz_log.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>

// ── Built-in category definitions (global scope, matching declarations) ─────
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

namespace uz {

// ── Internal state ──────────────────────────────────────────────────────────

static std::vector<spdlog::sink_ptr> g_sinks;
static std::mutex g_loggerMutex;
static std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> g_loggers;
static bool g_initialized = false;

void LogInit(const char* logFilePath) {
    if (g_initialized) return;

    // Console sink (colored)
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
    g_sinks.push_back(consoleSink);

    // File sink (rotating, 10MB max, 3 files)
    if (logFilePath && logFilePath[0] != '\0') {
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFilePath, 10 * 1024 * 1024, 3);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
        g_sinks.push_back(fileSink);
    }

    g_initialized = true;
}

void LogShutdown() {
    if (!g_initialized) return;

    spdlog::shutdown();

    std::lock_guard<std::mutex> lock(g_loggerMutex);
    g_loggers.clear();
    g_sinks.clear();
    g_initialized = false;
}

spdlog::logger* LogGetLogger(const LogCategory& cat) {
    if (!g_initialized) return nullptr;

    std::lock_guard<std::mutex> lock(g_loggerMutex);

    auto it = g_loggers.find(cat.name);
    if (it != g_loggers.end()) {
        return it->second.get();
    }

    // Create new logger with shared sinks
    auto logger = std::make_shared<spdlog::logger>(
        cat.name, g_sinks.begin(), g_sinks.end());
    logger->set_level(detail::ToSpdlogLevel(cat.compileTimeMin));
    logger->flush_on(spdlog::level::err);

    g_loggers[cat.name] = logger;
    return logger.get();
}

void LogSetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_loggerMutex);
    auto spdLevel = detail::ToSpdlogLevel(level);
    for (auto& [name, logger] : g_loggers) {
        logger->set_level(spdLevel);
    }
}

} // namespace uz
