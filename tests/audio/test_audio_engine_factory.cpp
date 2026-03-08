// test_audio_engine_factory.cpp -- Tests for AudioEngineFactory.

#include <gtest/gtest.h>

#include "../../src/engine/audio/audio_engine_factory.h"
#include "../../src/engine/audio/null_audio_engine.h"

namespace uz {
namespace {

// ===========================================================================
// Null backend creation
// ===========================================================================

TEST(AudioEngineFactoryTest, CreateNullBackendReturnsNonNull) {
    auto engine = AudioEngineFactory::create(AudioBackendType::Null);
    ASSERT_NE(engine, nullptr);
}

TEST(AudioEngineFactoryTest, CreateNullBackendReturnsCorrectType) {
    auto engine = AudioEngineFactory::create(AudioBackendType::Null);
    ASSERT_NE(engine, nullptr);
    EXPECT_EQ(engine->get_backend_type(), AudioBackendType::Null);
}

TEST(AudioEngineFactoryTest, CreateNullBackendIsFunctional) {
    auto engine = AudioEngineFactory::create(AudioBackendType::Null);
    ASSERT_NE(engine, nullptr);

    const AudioSourceHandle h = engine->create_source();
    EXPECT_NE(h, kAudioSourceHandleInvalid);

    engine->play(h);
    EXPECT_TRUE(engine->is_playing(h));

    engine->set_volume(h, 0.7f);
    EXPECT_FLOAT_EQ(engine->get_volume(h), 0.7f);

    engine->stop(h);
    EXPECT_FALSE(engine->is_playing(h));

    engine->destroy_source(h);
    EXPECT_EQ(engine->get_profiling_stats().active_sources, 0u);
}

// ===========================================================================
// Reserved backends return nullptr
// ===========================================================================

TEST(AudioEngineFactoryTest, CreateXAudio2ReturnsNullptr) {
    auto engine = AudioEngineFactory::create(AudioBackendType::XAudio2);
    EXPECT_EQ(engine, nullptr);
}

TEST(AudioEngineFactoryTest, CreateFmodReturnsNullptr) {
    auto engine = AudioEngineFactory::create(AudioBackendType::Fmod);
    EXPECT_EQ(engine, nullptr);
}

TEST(AudioEngineFactoryTest, CreateWwiseReturnsNullptr) {
    auto engine = AudioEngineFactory::create(AudioBackendType::Wwise);
    EXPECT_EQ(engine, nullptr);
}

// ===========================================================================
// Unknown backend (cast from invalid int) returns nullptr
// ===========================================================================

TEST(AudioEngineFactoryTest, CreateUnknownBackendReturnsNullptr) {
    auto engine = AudioEngineFactory::create(static_cast<AudioBackendType>(255));
    EXPECT_EQ(engine, nullptr);
}

// ===========================================================================
// Multiple independent instances
// ===========================================================================

TEST(AudioEngineFactoryTest, MultipleNullInstancesAreIndependent) {
    auto engine1 = AudioEngineFactory::create(AudioBackendType::Null);
    auto engine2 = AudioEngineFactory::create(AudioBackendType::Null);
    ASSERT_NE(engine1, nullptr);
    ASSERT_NE(engine2, nullptr);

    const AudioSourceHandle h1 = engine1->create_source();
    engine1->play(h1);

    // engine2 should have no sources.
    EXPECT_EQ(engine2->get_profiling_stats().active_sources, 0u);
    EXPECT_EQ(engine1->get_profiling_stats().active_sources, 1u);
    EXPECT_EQ(engine1->get_profiling_stats().playing_sources, 1u);
}

// ===========================================================================
// No COM initialization required
// ===========================================================================

TEST(AudioEngineFactoryTest, WorksWithoutComInitialization) {
    // This test verifies that creating a NullAudioEngine does not depend
    // on COM (CoInitializeEx). If it did, this would crash or fail on
    // a thread without COM initialized.
    auto engine = AudioEngineFactory::create(AudioBackendType::Null);
    ASSERT_NE(engine, nullptr);
    const AudioSourceHandle h = engine->create_source();
    EXPECT_NE(h, kAudioSourceHandleInvalid);
    engine->destroy_source(h);
}

} // namespace
} // namespace uz
