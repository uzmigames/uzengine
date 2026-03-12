#pragma once

/// Standalone CPU Profile Capture System
///
/// Captures scope enter/exit events into a fixed-size buffer and exports
/// to `.cpuprofile` JSON format (Chrome DevTools Protocol), which VSCode
/// opens natively as a flame chart.
///
/// Independent of Tracy — always available, even in release builds.
/// When not capturing, the recording functions are near-zero cost
/// (single relaxed atomic load).
///
/// # Usage
/// ```cpp
/// // Instrument code with macros:
/// void MyFunction() {
///     UZ_CAPTURE_FUNCTION();
///     // ... work ...
///     {
///         UZ_CAPTURE_SCOPE(SubTask);
///         // ... sub-work ...
///     }
/// }
///
/// // Start/stop capture:
/// uz::ProfileCapture::Get().StartCapture();
/// RunSomeFrames();
/// uz::ProfileCapture::Get().StopAndExport("profile.cpuprofile");
/// ```

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Scope event type — enter or exit a named scope.
enum class ProfileEventType : uint8_t {
    Enter, ///< Scope entry (push onto call stack)
    Exit,  ///< Scope exit (pop from call stack)
};

/// A single profiling event in the capture buffer.
///
/// Stores metadata about the scope (name, file, line) along with a
/// high-resolution timestamp. Name and file pointers reference string
/// literals and are NOT owned by this struct.
struct ProfileEvent {
    const char* name;      ///< Scope name (string literal from macro, not owned)
    const char* file;      ///< Source file path (__FILE__, not owned)
    uint32_t line;         ///< Source line number (__LINE__)
    uint64_t timestamp;    ///< QueryPerformanceCounter timestamp
    ProfileEventType type; ///< Whether this is a scope enter or exit
};

/// Standalone CPU profile capture with `.cpuprofile` JSON export.
///
/// Singleton that records scope enter/exit events into a pre-allocated
/// buffer when capturing is active. Events are timestamped with
/// `QueryPerformanceCounter` for sub-microsecond precision.
///
/// The buffer has a fixed capacity of 1M events. When full, new events
/// are silently dropped (first-N strategy preserves startup profiling).
///
/// # Thread Safety
/// `StartCapture()` and `StopAndExport()` must be called from a single
/// control thread. `RecordEnter()` / `RecordExit()` are safe to call
/// from any thread (atomic event count with relaxed ordering).
///
/// # Export Format
/// Produces Chrome DevTools `.cpuprofile` JSON with:
/// - `nodes[]` — call tree with `callFrame` containing function name,
///   file path (relative), and line number
/// - `samples[]` — deepest active node ID at regular intervals
/// - `timeDeltas[]` — microsecond deltas between samples
/// - `startTime` / `endTime` — capture duration in microseconds
class ProfileCapture {
public:
    /// Returns the process-wide singleton instance.
    ///
    /// # Returns
    /// Reference to the singleton. First call initializes QPC frequency.
    static ProfileCapture& Get();

    /// Begins capturing profile events.
    ///
    /// Pre-allocates the event buffer and records the capture start timestamp.
    /// If already capturing, this is a no-op.
    ///
    /// # Side Effects
    /// - Allocates ~32MB for the event buffer (1M events)
    /// - Logs `Profiling::Info` on start
    void StartCapture();

    /// Stops capturing and exports all recorded events to a `.cpuprofile` file.
    ///
    /// Builds a call tree from enter/exit events, generates synthetic samples
    /// at 10-microsecond intervals, and writes the result as JSON.
    ///
    /// # Arguments
    /// * `path` — Output file path (should end in `.cpuprofile`)
    ///
    /// # Returns
    /// `true` if the file was written successfully, `false` on I/O error
    /// or if no capture was active.
    ///
    /// # Side Effects
    /// - Writes file to disk
    /// - Logs event count, node count, and duration via `Profiling::Info`
    /// - Logs errors via `Profiling::Error` on failure
    /// - Frees the event buffer
    [[nodiscard]] bool StopAndExport(const std::string& path);

    /// Returns whether capture is currently active.
    ///
    /// # Returns
    /// `true` if `StartCapture()` was called and `StopAndExport()` has not
    /// yet been called.
    [[nodiscard]] bool IsCapturing() const;

    /// Records a scope-enter event. Called by `UZ_CAPTURE_SCOPE` macro.
    ///
    /// No-op if not currently capturing or if the buffer is full.
    ///
    /// # Arguments
    /// * `name` — Scope name (must be a string literal)
    /// * `file` — Source file path (must be a string literal, typically __FILE__)
    /// * `line` — Source line number (typically __LINE__)
    void RecordEnter(const char* name, const char* file, uint32_t line);

    /// Records a scope-exit event. Called by `CaptureScope` destructor.
    ///
    /// No-op if not currently capturing or if the buffer is full.
    ///
    /// # Arguments
    /// * `name` — Scope name (must match the corresponding `RecordEnter` call)
    void RecordExit(const char* name);

    // Non-copyable, non-movable (singleton)
    ProfileCapture(const ProfileCapture&) = delete;
    ProfileCapture& operator=(const ProfileCapture&) = delete;
    ProfileCapture(ProfileCapture&&) = delete;
    ProfileCapture& operator=(ProfileCapture&&) = delete;

private:
    ProfileCapture();

    /// Maximum number of events in the capture buffer.
    static constexpr size_t kMaxEvents = 1024 * 1024;

    std::atomic<bool> m_capturing{false};
    std::vector<ProfileEvent> m_events;
    std::atomic<size_t> m_eventCount{0};

    uint64_t m_qpcFrequency = 0;
    uint64_t m_captureStartTime = 0;
};

/// RAII scope guard for profile capture.
///
/// Records a scope-enter event on construction and a scope-exit event
/// on destruction. Created by the `UZ_CAPTURE_SCOPE` / `UZ_CAPTURE_FUNCTION`
/// macros.
///
/// # Lifetime
/// The `name`, `file` pointers must be string literals (they are not copied).
struct CaptureScope {
    const char* name; ///< Scope name (string literal, not owned)

    /// Constructs the scope guard, recording a scope-enter event.
    ///
    /// # Arguments
    /// * `n` — Scope name (string literal)
    /// * `file` — Source file (__FILE__)
    /// * `line` — Source line (__LINE__)
    CaptureScope(const char* n, const char* file, uint32_t line) : name(n) {
        ProfileCapture::Get().RecordEnter(n, file, line);
    }

    /// Destructor records a scope-exit event.
    ~CaptureScope() { ProfileCapture::Get().RecordExit(name); }

    CaptureScope(const CaptureScope&) = delete;
    CaptureScope& operator=(const CaptureScope&) = delete;
};

} // namespace uz

// ────────────────────────────────────────────────────────────────────────────
// Capture Macros — always available, no Tracy dependency
// ────────────────────────────────────────────────────────────────────────────

/// Captures a named scope. Only records events when `ProfileCapture::IsCapturing()`.
///
/// # Arguments
/// * `Name` — Unquoted identifier used as the scope name (stringified by macro)
///
/// # Example
/// ```cpp
/// void LoadAssets() {
///     UZ_CAPTURE_SCOPE(LoadAssets);
///     // ...
/// }
/// ```
#define UZ_CAPTURE_SCOPE(Name)                                                                     \
    ::uz::CaptureScope _captureScope_##Name(#Name, __FILE__, __LINE__)

/// Captures the current function as a scope. Uses `__FUNCTION__` as the name.
///
/// # Example
/// ```cpp
/// void Tick(float dt) {
///     UZ_CAPTURE_FUNCTION();
///     // ...
/// }
/// ```
#define UZ_CAPTURE_FUNCTION()                                                                      \
    ::uz::CaptureScope _captureScopeFunc(__FUNCTION__, __FILE__, __LINE__)
