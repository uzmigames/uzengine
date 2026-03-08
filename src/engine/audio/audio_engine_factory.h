#pragma once

// audio_engine_factory.h -- Creates the appropriate IAudioEngine backend.
//
// Usage:
//   auto engine = uz::AudioEngineFactory::create(uz::AudioBackendType::XAudio2);
//   if (!engine) { /* fallback */ }

#include "audio_engine.h"
#include "audio_types.h"

#include <memory>

namespace uz {

/// Factory for audio engine backends.
///
/// Currently supported:
///   - Null     -- always available
///   - XAudio2  -- reserved (returns nullptr)
///   - Fmod     -- reserved (returns nullptr)
///   - Wwise    -- reserved (returns nullptr)
struct AudioEngineFactory {
    /// Create an audio engine for the requested backend.
    /// Returns nullptr if the backend is unavailable or not yet implemented.
    [[nodiscard]] static std::unique_ptr<IAudioEngine> create(
        AudioBackendType backend);
};

} // namespace uz
