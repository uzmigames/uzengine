#pragma once

// audio_types.h -- Core audio type definitions for the audio abstraction layer.
//
// These types are shared across the engine's audio subsystem and are
// backend-agnostic. No platform headers included here.

#include <cstddef>
#include <cstdint>

namespace uz {

// --- Handle type -----------------------------------------------------------

/// Opaque handle to an audio source managed by IAudioEngine.
/// Zero is reserved as the invalid/null handle.
using AudioSourceHandle = uint32_t;
inline constexpr AudioSourceHandle kAudioSourceHandleInvalid = 0;

// --- Enums -----------------------------------------------------------------

/// Identifies which audio backend is active at runtime.
enum class AudioBackendType : uint8_t {
    Null     = 0,   // Silent no-op backend (testing, headless servers)
    XAudio2  = 1,   // Windows XAudio2 backend
    Fmod     = 2,   // FMOD Studio (reserved -- not yet implemented)
    Wwise    = 3,   // Audiokinetic Wwise (reserved -- not yet implemented)
};

// --- Structs ---------------------------------------------------------------

/// Configuration for an audio bus (submix group).
struct AudioBusConfig {
    char  name[64]       = {};    // Human-readable bus name (e.g. "Music", "SFX")
    float volume         = 1.0f;  // Linear gain [0.0, 1.0]
    char  parent_bus[64] = {};    // Name of parent bus ("" = master)
};

/// Profiling snapshot returned by IAudioEngine::get_profiling_stats().
struct AudioProfilingStats {
    uint32_t active_sources   = 0;     // Number of source handles alive
    uint32_t playing_sources  = 0;     // Number of sources currently playing
    uint32_t peak_voice_count = 0;     // High-water mark of simultaneous voices
    float    cpu_load_pct     = 0.0f;  // Audio thread CPU usage [0.0, 100.0]
    uint64_t memory_used_bytes = 0;    // Approximate memory consumed by audio data
};

} // namespace uz
