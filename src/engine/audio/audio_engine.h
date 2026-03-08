#pragma once

// audio_engine.h -- Abstract interface for the audio subsystem.
//
// All audio backends (XAudio2, FMOD, Wwise, Null) implement this interface.
// Engine systems interact with audio exclusively through IAudioEngine, keeping
// the backend completely swappable at runtime or compile time.
//
// Design:
//   - No exceptions. Operations on invalid handles are silent no-ops.
//   - No RTTI. Backend type is queryable via get_backend_type().
//   - Source handles are opaque uint32_t values; zero is invalid.

#include "audio_types.h"

#include <cstdint>

namespace uz {

// Forward declarations for math types (avoids pulling in the full math header
// in downstream translation units that only need the interface).
struct Vec3;
struct Mat4;

/// Pure interface for the audio subsystem.
///
/// Lifetime: created via AudioEngineFactory::create(), owned by the caller as
/// a unique_ptr<IAudioEngine>. Destroying the engine releases all backend
/// resources (voices, buffers, submixes).
class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;

    // -- Source lifecycle ----------------------------------------------------

    /// Allocate a new audio source. Returns kAudioSourceHandleInvalid on failure.
    [[nodiscard]] virtual AudioSourceHandle create_source() = 0;

    /// Release a source and all associated resources. No-op if handle is invalid.
    virtual void destroy_source(AudioSourceHandle handle) = 0;

    // -- Playback -----------------------------------------------------------

    /// Begin playback. No-op if handle is invalid or source is already playing.
    virtual void play(AudioSourceHandle handle) = 0;

    /// Stop playback and rewind. No-op if handle is invalid.
    virtual void stop(AudioSourceHandle handle) = 0;

    /// Pause playback (resume with play()). No-op if handle is invalid.
    virtual void pause(AudioSourceHandle handle) = 0;

    /// Returns true if the source is currently playing (not paused, not stopped).
    [[nodiscard]] virtual bool is_playing(AudioSourceHandle handle) const = 0;

    // -- Parameters ---------------------------------------------------------

    /// Set linear volume. Clamped to [0.0, 1.0]. No-op if handle is invalid.
    virtual void set_volume(AudioSourceHandle handle, float volume) = 0;

    /// Get current volume. Returns 0.0 if handle is invalid.
    [[nodiscard]] virtual float get_volume(AudioSourceHandle handle) const = 0;

    /// Set pitch multiplier (1.0 = normal). No-op if handle is invalid.
    virtual void set_pitch(AudioSourceHandle handle, float pitch) = 0;

    // -- 3D spatialization --------------------------------------------------

    /// Set world-space position. No-op if handle is invalid.
    virtual void set_position(AudioSourceHandle handle, const Vec3& position) = 0;

    /// Set velocity for Doppler effect. No-op if handle is invalid.
    virtual void set_velocity(AudioSourceHandle handle, const Vec3& velocity) = 0;

    /// Set the listener's world transform (position + orientation from Mat4).
    virtual void set_listener_transform(const Mat4& transform) = 0;

    // -- Diagnostics --------------------------------------------------------

    /// Snapshot of current audio engine statistics for profiling overlays.
    [[nodiscard]] virtual AudioProfilingStats get_profiling_stats() const = 0;

    /// Which backend is driving this engine instance.
    [[nodiscard]] virtual AudioBackendType get_backend_type() const = 0;

protected:
    // Non-copyable, non-movable (pointer-semantic interface type).
    IAudioEngine() = default;
    IAudioEngine(const IAudioEngine&) = default;
    IAudioEngine& operator=(const IAudioEngine&) = default;
};

} // namespace uz
