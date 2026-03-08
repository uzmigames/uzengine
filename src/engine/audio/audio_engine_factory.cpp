// audio_engine_factory.cpp -- Backend creation logic.

#include "audio_engine_factory.h"
#include "null_audio_engine.h"

#include <memory>

namespace uz {

std::unique_ptr<IAudioEngine> AudioEngineFactory::create(
    AudioBackendType backend)
{
    switch (backend) {
    case AudioBackendType::Null:
        return std::make_unique<NullAudioEngine>();

    case AudioBackendType::XAudio2:
        // Reserved -- XAudio2 backend not yet implemented.
        return nullptr;

    case AudioBackendType::Fmod:
        // Reserved -- FMOD backend not yet implemented.
        return nullptr;

    case AudioBackendType::Wwise:
        // Reserved -- Wwise backend not yet implemented.
        return nullptr;

    default:
        // Unknown backend type.
        return nullptr;
    }
}

} // namespace uz
