#pragma once

// null_audio_engine.h -- Silent audio backend with full state tracking.
//
// NullAudioEngine implements IAudioEngine without any hardware dependency.
// It tracks source state (playing, volume, pitch) in memory so that game
// logic can function identically whether real audio hardware is present.
//
// Use cases:
//   - Headless / dedicated server builds
//   - Automated testing
//   - Fallback when no real backend is available

#include "audio_engine.h"

#include <cstdint>
#include <unordered_map>

namespace uz {

class NullAudioEngine final : public IAudioEngine {
public:
    NullAudioEngine() = default;
    ~NullAudioEngine() override = default;

    // Non-copyable, non-movable (owns mutable map state).
    NullAudioEngine(const NullAudioEngine&) = delete;
    NullAudioEngine& operator=(const NullAudioEngine&) = delete;
    NullAudioEngine(NullAudioEngine&&) = delete;
    NullAudioEngine& operator=(NullAudioEngine&&) = delete;

    // -- Source lifecycle ----------------------------------------------------
    [[nodiscard]] AudioSourceHandle create_source() override;
    void destroy_source(AudioSourceHandle handle) override;

    // -- Playback -----------------------------------------------------------
    void play(AudioSourceHandle handle) override;
    void stop(AudioSourceHandle handle) override;
    void pause(AudioSourceHandle handle) override;
    [[nodiscard]] bool is_playing(AudioSourceHandle handle) const override;

    // -- Parameters ---------------------------------------------------------
    void set_volume(AudioSourceHandle handle, float volume) override;
    [[nodiscard]] float get_volume(AudioSourceHandle handle) const override;
    void set_pitch(AudioSourceHandle handle, float pitch) override;

    // -- 3D spatialization --------------------------------------------------
    void set_position(AudioSourceHandle handle, const Vec3& position) override;
    void set_velocity(AudioSourceHandle handle, const Vec3& velocity) override;
    void set_listener_transform(const Mat4& transform) override;

    // -- Diagnostics --------------------------------------------------------
    [[nodiscard]] AudioProfilingStats get_profiling_stats() const override;
    [[nodiscard]] AudioBackendType get_backend_type() const override;

private:
    struct SourceState {
        bool  playing = false;
        float volume  = 1.0f;
        float pitch   = 1.0f;
    };

    std::unordered_map<AudioSourceHandle, SourceState> sources_;
    uint32_t next_handle_ = 1;  // Monotonically incrementing; 0 is invalid.
};

} // namespace uz
