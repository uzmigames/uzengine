// null_audio_engine.cpp -- Silent audio backend implementation.

#include "null_audio_engine.h"

#include <algorithm>  // std::clamp

namespace uz {

// -- Source lifecycle --------------------------------------------------------

AudioSourceHandle NullAudioEngine::create_source() {
    const AudioSourceHandle handle = next_handle_++;
    sources_[handle] = SourceState{};
    return handle;
}

void NullAudioEngine::destroy_source(AudioSourceHandle handle) {
    sources_.erase(handle);  // No-op if handle not found.
}

// -- Playback ---------------------------------------------------------------

void NullAudioEngine::play(AudioSourceHandle handle) {
    auto it = sources_.find(handle);
    if (it == sources_.end()) return;
    it->second.playing = true;
}

void NullAudioEngine::stop(AudioSourceHandle handle) {
    auto it = sources_.find(handle);
    if (it == sources_.end()) return;
    it->second.playing = false;
}

void NullAudioEngine::pause(AudioSourceHandle handle) {
    auto it = sources_.find(handle);
    if (it == sources_.end()) return;
    it->second.playing = false;
}

bool NullAudioEngine::is_playing(AudioSourceHandle handle) const {
    auto it = sources_.find(handle);
    if (it == sources_.end()) return false;
    return it->second.playing;
}

// -- Parameters -------------------------------------------------------------

void NullAudioEngine::set_volume(AudioSourceHandle handle, float volume) {
    auto it = sources_.find(handle);
    if (it == sources_.end()) return;
    it->second.volume = std::clamp(volume, 0.0f, 1.0f);
}

float NullAudioEngine::get_volume(AudioSourceHandle handle) const {
    auto it = sources_.find(handle);
    if (it == sources_.end()) return 0.0f;
    return it->second.volume;
}

void NullAudioEngine::set_pitch(AudioSourceHandle handle, float pitch) {
    auto it = sources_.find(handle);
    if (it == sources_.end()) return;
    it->second.pitch = pitch;
}

// -- 3D spatialization ------------------------------------------------------

void NullAudioEngine::set_position(AudioSourceHandle /*handle*/,
                                   const Vec3& /*position*/) {
    // Null backend: no spatialization, intentionally empty.
}

void NullAudioEngine::set_velocity(AudioSourceHandle /*handle*/,
                                   const Vec3& /*velocity*/) {
    // Null backend: no Doppler, intentionally empty.
}

void NullAudioEngine::set_listener_transform(const Mat4& /*transform*/) {
    // Null backend: no listener, intentionally empty.
}

// -- Diagnostics ------------------------------------------------------------

AudioProfilingStats NullAudioEngine::get_profiling_stats() const {
    AudioProfilingStats stats{};
    stats.active_sources = static_cast<uint32_t>(sources_.size());

    uint32_t playing = 0;
    for (const auto& [handle, state] : sources_) {
        if (state.playing) ++playing;
    }
    stats.playing_sources = playing;

    // Null backend has no real resource usage.
    return stats;
}

AudioBackendType NullAudioEngine::get_backend_type() const {
    return AudioBackendType::Null;
}

} // namespace uz
