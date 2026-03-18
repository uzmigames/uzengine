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

/// Suppresses console (stdout) logging output.
///
/// Removes the console sink so logs only go to the file. Call AFTER LogInit().
/// Use when stdout must be reserved for structured output (e.g., --screenshot-stdout
/// prints base64 PNG data that would be corrupted by interleaved log messages).
void LogSuppressConsole();

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

/// Declares a log category in a header file.
///
/// Creates an `extern` declaration for the category's `LogCategory` variable.
/// Must be paired with exactly one `UZ_DEFINE_LOG_CATEGORY` in a `.cpp` file.
///
/// # Arguments
/// * `Name` - Category identifier (e.g., `RHI`, `Scene`, `MySystem`)
///
/// # Examples
/// ```cpp
/// // In my_system.h
/// UZ_DECLARE_LOG_CATEGORY(MySystem);
/// ```
#define UZ_DECLARE_LOG_CATEGORY(Name) extern ::uz::LogCategory Log##Name

/// Defines a log category in a source file.
///
/// Creates the `LogCategory` variable with a compile-time minimum severity level.
/// Messages below this level are eliminated at compile time (zero overhead).
///
/// # Arguments
/// * `Name` - Category identifier (must match a prior `UZ_DECLARE_LOG_CATEGORY`)
/// * `MinLevel` - Compile-time minimum: `Trace`, `Debug`, `Info`, `Warning`, `Error`, `Fatal`
///
/// # Examples
/// ```cpp
/// // In my_system.cpp
/// UZ_DEFINE_LOG_CATEGORY(MySystem, Info);  // strips Trace/Debug at compile time
/// ```
#define UZ_DEFINE_LOG_CATEGORY(Name, MinLevel)                                                     \
    ::uz::LogCategory Log##Name {                                                                  \
        #Name, ::uz::LogLevel::MinLevel                                                            \
    }

/// Logs a message to a category at the specified severity level.
///
/// Messages below the category's compile-time minimum are eliminated with zero overhead.
/// Messages below the runtime level (set via `LogSetLevel`) are discarded at runtime.
///
/// # Arguments
/// * `Category` - Log category name (must be declared with `UZ_DECLARE_LOG_CATEGORY`)
/// * `Level` - Severity: `Trace`, `Debug`, `Info`, `Warning`, `Error`, `Fatal`
/// * `...` - fmt-lib format string and arguments
///
/// # Panics
/// `Fatal` level flushes the logger and calls `std::abort()` after logging.
///
/// # Examples
/// ```cpp
/// UZ_LOG(RHI, Info, "Device created: {}", deviceName);
/// UZ_LOG(Scene, Error, "Entity {} not found", entityId);
/// UZ_LOG(Core, Fatal, "Unrecoverable: {}", reason);  // aborts after logging
/// ```
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

/// Global compile-time minimum log level.
///
/// In shipping builds (`UZ_SHIPPING` defined), set to `Info` — all `Trace`/`Debug`
/// messages are stripped at compile time across all categories.
/// In development builds, set to `Trace` (everything passes compile-time gate).
#ifdef UZ_SHIPPING
#define UZ_LOG_COMPILE_MIN ::uz::LogLevel::Info
#else
#define UZ_LOG_COMPILE_MIN ::uz::LogLevel::Trace
#endif

namespace uz::detail {

/// Converts `uz::LogLevel` to the equivalent `spdlog::level::level_enum`.
///
/// # Arguments
/// * `level` - Engine log level to convert
///
/// # Returns
/// Corresponding spdlog level. Falls back to `spdlog::level::info` for unrecognized values.
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

// ── Built-in Log Categories ──────────────────────────────────────────────────
//
// Engine-provided categories for each major subsystem.
// Custom systems should declare their own via UZ_DECLARE_LOG_CATEGORY.

UZ_DECLARE_LOG_CATEGORY(Core);      ///< Core engine systems (startup, shutdown, tick)
UZ_DECLARE_LOG_CATEGORY(RHI);       ///< Render Hardware Interface (DX12, Vulkan, Metal)
UZ_DECLARE_LOG_CATEGORY(Renderer);  ///< High-level rendering (passes, materials, lighting)
UZ_DECLARE_LOG_CATEGORY(Scene);     ///< Scene graph, entity management, transforms
UZ_DECLARE_LOG_CATEGORY(Physics);   ///< Physics simulation (collision, dynamics, queries)
UZ_DECLARE_LOG_CATEGORY(Audio);     ///< Audio engine (playback, mixing, spatial)
UZ_DECLARE_LOG_CATEGORY(Input);     ///< Input handling (keyboard, mouse, gamepad)
UZ_DECLARE_LOG_CATEGORY(Platform);  ///< Platform abstraction layer (OS, windowing)
UZ_DECLARE_LOG_CATEGORY(Memory);    ///< Memory management (allocators, pools, tracking)
UZ_DECLARE_LOG_CATEGORY(IO);        ///< File I/O, asset streaming, virtual filesystem
UZ_DECLARE_LOG_CATEGORY(Config);    ///< Configuration loading and parsing
UZ_DECLARE_LOG_CATEGORY(Window);    ///< Window creation, resize, focus events
UZ_DECLARE_LOG_CATEGORY(Engine);    ///< Top-level engine orchestration
UZ_DECLARE_LOG_CATEGORY(Asset);     ///< Asset pipeline (import, cook, cache)
UZ_DECLARE_LOG_CATEGORY(Shadow);    ///< Cascaded shadow map system (splits, light matrices)
UZ_DECLARE_LOG_CATEGORY(Profiling); ///< CPU profile capture and export
UZ_DECLARE_LOG_CATEGORY(Property);      ///< Property system (reflection, serialization, registry)
UZ_DECLARE_LOG_CATEGORY(CrashHandler); ///< Crash handling and diagnostics (minidump, stack trace)
UZ_DECLARE_LOG_CATEGORY(App);          ///< Demo/application framework (lifecycle, frame timing)
UZ_DECLARE_LOG_CATEGORY(Scripting);    ///< Lua scripting VM (sandbox, bindings, hot-reload)
UZ_DECLARE_LOG_CATEGORY(Material);     ///< Material graph system (expressions, compiler, serialization)
