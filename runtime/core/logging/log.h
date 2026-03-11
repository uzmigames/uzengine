#pragma once

/// UzEngine Structured Logging System
///
/// Provides category-based, compile-time filtered logging via spdlog backend.
/// Inspired by Unreal Engine 5's UE_LOG macro pattern with format string support.
///
/// # Design
/// - **Categories**: Each subsystem defines its own LogCategory for filtering
/// - **Compile-time filtering**: Trace/Debug stripped in shipping builds
/// - **Thread-safe**: Uses mutex-protected logger pool
/// - **Dual output**: Console (colored) + rotating file sink (10MB, 3 files)
///
/// # Usage
/// ```cpp
/// UZ_DEFINE_LOG_CATEGORY(MySystem, Trace);
/// UZ_LOG(MySystem, Info, "Initialized with {} items", count);
/// UZ_LOG(MySystem, Error, "Failed: {}", reason);
/// ```

#include <memory>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>

namespace uz {

/// Log severity level (ascending order: Trace is most verbose, Fatal is least).
enum class LogLevel : uint8_t {
    Trace = 0,   ///< Very detailed diagnostic output (stripped in shipping)
    Debug = 1,   ///< Debug messages (stripped in shipping)
    Info = 2,    ///< General informational messages
    Warning = 3, ///< Warning conditions (non-fatal)
    Error = 4,   ///< Error conditions (recoverable)
    Fatal = 5,   ///< Fatal errors (calls std::abort after logging)
};

/// Log category metadata — one per subsystem.
///
/// Categories are typically created via UZ_DECLARE_LOG_CATEGORY (header)
/// and UZ_DEFINE_LOG_CATEGORY (single .cpp file).
/// Each category has a name and compile-time minimum level for filtering.
struct LogCategory {
    const char* name;        ///< Category identifier (e.g., "RHI", "Scene")
    LogLevel compileTimeMin; ///< Minimum level at compile time (stripped below)
};

/// Initializes the global logging system.
///
/// # Arguments
/// * `logFilePath` - Path to rotating log file. If empty or nullptr, only console logging.
///
/// # Behavior
/// - Sets up console sink (colored output)
/// - Sets up file sink (rotating: 10MB max, keeps 3 files)
/// - Safe to call multiple times (no-op if already initialized)
void LogInit(const char* logFilePath = "uzengine.log");

/// Shuts down the global logging system.
///
/// # Behavior
/// - Flushes all pending log messages
/// - Destroys all logger instances
/// - Safe to call multiple times (no-op if not initialized)
void LogShutdown();

/// Gets or creates a spdlog logger for a category.
///
/// # Thread Safety
/// Thread-safe via internal mutex.
///
/// # Returns
/// Pointer to spdlog logger, or nullptr if LogInit not called.
///
/// # Caching
/// Loggers are cached by category name — repeated calls are fast.
spdlog::logger* LogGetLogger(const LogCategory& cat);

/// Sets runtime minimum level for all active loggers.
///
/// # Arguments
/// * `level` - New minimum level (e.g., LogLevel::Warning to suppress Info)
///
/// # Thread Safety
/// Thread-safe via internal mutex.
void LogSetLevel(LogLevel level);

} // namespace uz

// ── Macros ──────────────────────────────────────────────────────────────────

/// Declare a log category (header). Use in .h files.
#define UZ_DECLARE_LOG_CATEGORY(Name) extern ::uz::LogCategory Log##Name

/// Define a log category (source). Use in ONE .cpp file.
#define UZ_DEFINE_LOG_CATEGORY(Name, MinLevel)                                                     \
    ::uz::LogCategory Log##Name {                                                                  \
        #Name, ::uz::LogLevel::MinLevel                                                            \
    }

/// Core log macro. Checks compile-time minimum before formatting.
#define UZ_LOG(Category, Level, ...)                                                               \
    do {                                                                                           \
        if (static_cast<uint8_t>(::uz::LogLevel::Level) >=                                         \
            static_cast<uint8_t>(Log##Category.compileTimeMin)) {                                  \
            auto* _uz_logger = ::uz::LogGetLogger(Log##Category);                                  \
            if (_uz_logger) {                                                                      \
                _uz_logger->log(::uz::detail::ToSpdlogLevel(::uz::LogLevel::Level), __VA_ARGS__);  \
                if (::uz::LogLevel::Level == ::uz::LogLevel::Fatal) {                              \
                    _uz_logger->flush();                                                           \
                    std::abort();                                                                  \
                }                                                                                  \
            }                                                                                      \
        }                                                                                          \
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
    case LogLevel::Trace:
        return spdlog::level::trace;
    case LogLevel::Debug:
        return spdlog::level::debug;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Warning:
        return spdlog::level::warn;
    case LogLevel::Error:
        return spdlog::level::err;
    case LogLevel::Fatal:
        return spdlog::level::critical;
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
UZ_DECLARE_LOG_CATEGORY(Engine);
