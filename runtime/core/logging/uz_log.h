#pragma once

/// UzEngine Logging System
/// Category-based, compile-time filtered, spdlog-backed.
/// Pattern: UE5's UE_LOG with fmt formatting.

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <memory>
#include <string>
#include <string_view>

namespace uz {

enum class LogLevel : uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Fatal = 5,
};

/// Log category — one per subsystem. Create with UZ_DECLARE_LOG_CATEGORY / UZ_DEFINE_LOG_CATEGORY.
struct LogCategory {
    const char* name;
    LogLevel compileTimeMin;
};

/// Global log system initialization/shutdown.
void LogInit(const char* logFilePath = "uzengine.log");
void LogShutdown();

/// Get or create a spdlog logger for a category.
spdlog::logger* LogGetLogger(const LogCategory& cat);

/// Set runtime minimum level for all loggers.
void LogSetLevel(LogLevel level);

} // namespace uz

// ── Macros ──────────────────────────────────────────────────────────────────

/// Declare a log category (header). Use in .h files.
#define UZ_DECLARE_LOG_CATEGORY(Name) \
    extern ::uz::LogCategory Log##Name

/// Define a log category (source). Use in ONE .cpp file.
#define UZ_DEFINE_LOG_CATEGORY(Name, MinLevel) \
    ::uz::LogCategory Log##Name { #Name, ::uz::LogLevel::MinLevel }

/// Core log macro. Checks compile-time minimum before formatting.
#define UZ_LOG(Category, Level, ...) \
    do { \
        if (static_cast<uint8_t>(::uz::LogLevel::Level) >= \
            static_cast<uint8_t>(Log##Category.compileTimeMin)) { \
            auto* _uz_logger = ::uz::LogGetLogger(Log##Category); \
            if (_uz_logger) { \
                _uz_logger->log(::uz::detail::ToSpdlogLevel(::uz::LogLevel::Level), __VA_ARGS__); \
                if (::uz::LogLevel::Level == ::uz::LogLevel::Fatal) { \
                    _uz_logger->flush(); \
                    std::abort(); \
                } \
            } \
        } \
    } while (0)

/// Shipping build strips Debug/Trace entirely.
#ifdef UZ_SHIPPING
#define UZ_LOG_COMPILE_MIN ::uz::LogLevel::Info
#else
#define UZ_LOG_COMPILE_MIN ::uz::LogLevel::Trace
#endif

namespace uz::detail {

constexpr spdlog::level::level_enum ToSpdlogLevel(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:   return spdlog::level::trace;
        case LogLevel::Debug:   return spdlog::level::debug;
        case LogLevel::Info:    return spdlog::level::info;
        case LogLevel::Warning: return spdlog::level::warn;
        case LogLevel::Error:   return spdlog::level::err;
        case LogLevel::Fatal:   return spdlog::level::critical;
    }
    return spdlog::level::info;
}

} // namespace uz::detail

// ── Built-in categories ─────────────────────────────────────────────────────
UZ_DECLARE_LOG_CATEGORY(Core);
UZ_DECLARE_LOG_CATEGORY(RHI);
UZ_DECLARE_LOG_CATEGORY(Renderer);
UZ_DECLARE_LOG_CATEGORY(Scene);
UZ_DECLARE_LOG_CATEGORY(Physics);
UZ_DECLARE_LOG_CATEGORY(Audio);
UZ_DECLARE_LOG_CATEGORY(Input);
UZ_DECLARE_LOG_CATEGORY(Platform);
UZ_DECLARE_LOG_CATEGORY(Memory);
UZ_DECLARE_LOG_CATEGORY(IO);
UZ_DECLARE_LOG_CATEGORY(Config);
UZ_DECLARE_LOG_CATEGORY(Window);
