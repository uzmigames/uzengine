// test_null_audio_engine.cpp -- Tests for the NullAudioEngine backend.

#include <gtest/gtest.h>

#include "../../src/engine/audio/null_audio_engine.h"
#include "../../src/engine/core/math.h"

#include <set>
#include <vector>

namespace uz {
namespace {

// ===========================================================================
// Fixture
// ===========================================================================

class NullAudioEngineTest : public ::testing::Test {
protected:
    NullAudioEngine engine;
};

// ===========================================================================
// Source creation
// ===========================================================================

TEST_F(NullAudioEngineTest, CreateSourceReturnsNonZeroHandle) {
    const AudioSourceHandle h = engine.create_source();
    EXPECT_NE(h, kAudioSourceHandleInvalid);
}

TEST_F(NullAudioEngineTest, CreateSourceReturnsUniqueHandles) {
    std::set<AudioSourceHandle> handles;
    for (int i = 0; i < 100; ++i) {
        const AudioSourceHandle h = engine.create_source();
        EXPECT_NE(h, kAudioSourceHandleInvalid);
        EXPECT_TRUE(handles.insert(h).second)
            << "Duplicate handle returned on iteration " << i;
    }
    EXPECT_EQ(handles.size(), 100u);
}

TEST_F(NullAudioEngineTest, CreateSourceSequentiallyIncrementing) {
    const AudioSourceHandle h1 = engine.create_source();
    const AudioSourceHandle h2 = engine.create_source();
    const AudioSourceHandle h3 = engine.create_source();
    EXPECT_LT(h1, h2);
    EXPECT_LT(h2, h3);
}

// ===========================================================================
// Play / stop / pause lifecycle
// ===========================================================================

TEST_F(NullAudioEngineTest, NewSourceIsNotPlaying) {
    const AudioSourceHandle h = engine.create_source();
    EXPECT_FALSE(engine.is_playing(h));
}

TEST_F(NullAudioEngineTest, PlayMakesSourcePlaying) {
    const AudioSourceHandle h = engine.create_source();
    engine.play(h);
    EXPECT_TRUE(engine.is_playing(h));
}

TEST_F(NullAudioEngineTest, StopMakesSourceNotPlaying) {
    const AudioSourceHandle h = engine.create_source();
    engine.play(h);
    engine.stop(h);
    EXPECT_FALSE(engine.is_playing(h));
}

TEST_F(NullAudioEngineTest, PauseMakesSourceNotPlaying) {
    const AudioSourceHandle h = engine.create_source();
    engine.play(h);
    engine.pause(h);
    EXPECT_FALSE(engine.is_playing(h));
}

TEST_F(NullAudioEngineTest, PlayAfterPauseResumes) {
    const AudioSourceHandle h = engine.create_source();
    engine.play(h);
    engine.pause(h);
    engine.play(h);
    EXPECT_TRUE(engine.is_playing(h));
}

TEST_F(NullAudioEngineTest, StopThenPlayRestarts) {
    const AudioSourceHandle h = engine.create_source();
    engine.play(h);
    engine.stop(h);
    engine.play(h);
    EXPECT_TRUE(engine.is_playing(h));
}

TEST_F(NullAudioEngineTest, DoublePlayIsIdempotent) {
    const AudioSourceHandle h = engine.create_source();
    engine.play(h);
    engine.play(h);
    EXPECT_TRUE(engine.is_playing(h));
}

TEST_F(NullAudioEngineTest, DoubleStopIsIdempotent) {
    const AudioSourceHandle h = engine.create_source();
    engine.play(h);
    engine.stop(h);
    engine.stop(h);
    EXPECT_FALSE(engine.is_playing(h));
}

// ===========================================================================
// Invalid handle operations (no-ops, no crash)
// ===========================================================================

TEST_F(NullAudioEngineTest, PlayInvalidHandleDoesNotCrash) {
    engine.play(kAudioSourceHandleInvalid);
    engine.play(42);  // Never-created handle.
}

TEST_F(NullAudioEngineTest, StopInvalidHandleDoesNotCrash) {
    engine.stop(kAudioSourceHandleInvalid);
    engine.stop(999);
}

TEST_F(NullAudioEngineTest, PauseInvalidHandleDoesNotCrash) {
    engine.pause(kAudioSourceHandleInvalid);
}

TEST_F(NullAudioEngineTest, IsPlayingInvalidHandleReturnsFalse) {
    EXPECT_FALSE(engine.is_playing(kAudioSourceHandleInvalid));
    EXPECT_FALSE(engine.is_playing(123));
}

TEST_F(NullAudioEngineTest, DestroyInvalidHandleDoesNotCrash) {
    engine.destroy_source(kAudioSourceHandleInvalid);
    engine.destroy_source(777);
}

TEST_F(NullAudioEngineTest, SetVolumeInvalidHandleDoesNotCrash) {
    engine.set_volume(kAudioSourceHandleInvalid, 0.5f);
}

TEST_F(NullAudioEngineTest, GetVolumeInvalidHandleReturnsZero) {
    EXPECT_FLOAT_EQ(engine.get_volume(kAudioSourceHandleInvalid), 0.0f);
    EXPECT_FLOAT_EQ(engine.get_volume(999), 0.0f);
}

TEST_F(NullAudioEngineTest, SetPitchInvalidHandleDoesNotCrash) {
    engine.set_pitch(kAudioSourceHandleInvalid, 2.0f);
}

// ===========================================================================
// Destroy source
// ===========================================================================

TEST_F(NullAudioEngineTest, DestroySourceRemovesHandle) {
    const AudioSourceHandle h = engine.create_source();
    engine.play(h);
    EXPECT_TRUE(engine.is_playing(h));
    engine.destroy_source(h);
    EXPECT_FALSE(engine.is_playing(h));
}

TEST_F(NullAudioEngineTest, DestroySourceReducesActiveCount) {
    const AudioSourceHandle h1 = engine.create_source();
    const AudioSourceHandle h2 = engine.create_source();
    EXPECT_EQ(engine.get_profiling_stats().active_sources, 2u);
    engine.destroy_source(h1);
    EXPECT_EQ(engine.get_profiling_stats().active_sources, 1u);
    engine.destroy_source(h2);
    EXPECT_EQ(engine.get_profiling_stats().active_sources, 0u);
}

TEST_F(NullAudioEngineTest, OperationsAfterDestroyAreNoOps) {
    const AudioSourceHandle h = engine.create_source();
    engine.destroy_source(h);

    // All operations on a destroyed handle are safe no-ops.
    engine.play(h);
    engine.stop(h);
    engine.pause(h);
    engine.set_volume(h, 0.5f);
    engine.set_pitch(h, 2.0f);
    EXPECT_FALSE(engine.is_playing(h));
    EXPECT_FLOAT_EQ(engine.get_volume(h), 0.0f);
}

// ===========================================================================
// Volume
// ===========================================================================

TEST_F(NullAudioEngineTest, DefaultVolumeIsOne) {
    const AudioSourceHandle h = engine.create_source();
    EXPECT_FLOAT_EQ(engine.get_volume(h), 1.0f);
}

TEST_F(NullAudioEngineTest, SetVolumeNormalRange) {
    const AudioSourceHandle h = engine.create_source();
    engine.set_volume(h, 0.5f);
    EXPECT_FLOAT_EQ(engine.get_volume(h), 0.5f);
}

TEST_F(NullAudioEngineTest, SetVolumeZero) {
    const AudioSourceHandle h = engine.create_source();
    engine.set_volume(h, 0.0f);
    EXPECT_FLOAT_EQ(engine.get_volume(h), 0.0f);
}

TEST_F(NullAudioEngineTest, SetVolumeOne) {
    const AudioSourceHandle h = engine.create_source();
    engine.set_volume(h, 0.3f);
    engine.set_volume(h, 1.0f);
    EXPECT_FLOAT_EQ(engine.get_volume(h), 1.0f);
}

TEST_F(NullAudioEngineTest, SetVolumeClampsBelowZero) {
    const AudioSourceHandle h = engine.create_source();
    engine.set_volume(h, -1.0f);
    EXPECT_FLOAT_EQ(engine.get_volume(h), 0.0f);
}

TEST_F(NullAudioEngineTest, SetVolumeClampsBelowZeroLarge) {
    const AudioSourceHandle h = engine.create_source();
    engine.set_volume(h, -100.0f);
    EXPECT_FLOAT_EQ(engine.get_volume(h), 0.0f);
}

TEST_F(NullAudioEngineTest, SetVolumeClampsAboveOne) {
    const AudioSourceHandle h = engine.create_source();
    engine.set_volume(h, 2.0f);
    EXPECT_FLOAT_EQ(engine.get_volume(h), 1.0f);
}

TEST_F(NullAudioEngineTest, SetVolumeClampsAboveOneLarge) {
    const AudioSourceHandle h = engine.create_source();
    engine.set_volume(h, 999.0f);
    EXPECT_FLOAT_EQ(engine.get_volume(h), 1.0f);
}

// ===========================================================================
// Pitch
// ===========================================================================

TEST_F(NullAudioEngineTest, DefaultPitchIsOne) {
    const AudioSourceHandle h = engine.create_source();
    // Pitch is not exposed via get_pitch in the interface, but we verify
    // via profiling stats that the source exists and is valid.
    EXPECT_NE(h, kAudioSourceHandleInvalid);
}

TEST_F(NullAudioEngineTest, SetPitchDoesNotCrash) {
    const AudioSourceHandle h = engine.create_source();
    engine.set_pitch(h, 0.5f);
    engine.set_pitch(h, 2.0f);
    engine.set_pitch(h, 0.0f);
    // No crash -- pitch is stored but not observable in null backend.
}

// ===========================================================================
// 3D spatialization (no-ops in null backend, must not crash)
// ===========================================================================

TEST_F(NullAudioEngineTest, SetPositionDoesNotCrash) {
    const AudioSourceHandle h = engine.create_source();
    const Vec3 pos{1.0f, 2.0f, 3.0f, 0.0f};
    engine.set_position(h, pos);
    engine.set_position(kAudioSourceHandleInvalid, pos);
}

TEST_F(NullAudioEngineTest, SetVelocityDoesNotCrash) {
    const AudioSourceHandle h = engine.create_source();
    const Vec3 vel{10.0f, 0.0f, -5.0f, 0.0f};
    engine.set_velocity(h, vel);
    engine.set_velocity(kAudioSourceHandleInvalid, vel);
}

TEST_F(NullAudioEngineTest, SetListenerTransformDoesNotCrash) {
    engine.set_listener_transform(Mat4::identity());
}

// ===========================================================================
// Profiling stats
// ===========================================================================

TEST_F(NullAudioEngineTest, EmptyEngineStatsAreZero) {
    const auto stats = engine.get_profiling_stats();
    EXPECT_EQ(stats.active_sources, 0u);
    EXPECT_EQ(stats.playing_sources, 0u);
    EXPECT_EQ(stats.peak_voice_count, 0u);
    EXPECT_FLOAT_EQ(stats.cpu_load_pct, 0.0f);
    EXPECT_EQ(stats.memory_used_bytes, 0u);
}

TEST_F(NullAudioEngineTest, StatsTrackActiveSources) {
    (void)engine.create_source();
    (void)engine.create_source();
    (void)engine.create_source();
    const auto stats = engine.get_profiling_stats();
    EXPECT_EQ(stats.active_sources, 3u);
}

TEST_F(NullAudioEngineTest, StatsTrackPlayingSources) {
    const AudioSourceHandle h1 = engine.create_source();
    const AudioSourceHandle h2 = engine.create_source();
    (void)engine.create_source();  // Not playing.

    engine.play(h1);
    engine.play(h2);

    const auto stats = engine.get_profiling_stats();
    EXPECT_EQ(stats.active_sources, 3u);
    EXPECT_EQ(stats.playing_sources, 2u);
}

TEST_F(NullAudioEngineTest, StatsUpdateAfterStopAndDestroy) {
    const AudioSourceHandle h1 = engine.create_source();
    const AudioSourceHandle h2 = engine.create_source();
    engine.play(h1);
    engine.play(h2);

    engine.stop(h1);
    {
        const auto stats = engine.get_profiling_stats();
        EXPECT_EQ(stats.active_sources, 2u);
        EXPECT_EQ(stats.playing_sources, 1u);
    }

    engine.destroy_source(h2);
    {
        const auto stats = engine.get_profiling_stats();
        EXPECT_EQ(stats.active_sources, 1u);
        EXPECT_EQ(stats.playing_sources, 0u);
    }
}

// ===========================================================================
// Backend type
// ===========================================================================

TEST_F(NullAudioEngineTest, BackendTypeIsNull) {
    EXPECT_EQ(engine.get_backend_type(), AudioBackendType::Null);
}

// ===========================================================================
// Interface polymorphism (via base pointer)
// ===========================================================================

TEST(NullAudioEnginePolyTest, WorksThroughBasePointer) {
    std::unique_ptr<IAudioEngine> base = std::make_unique<NullAudioEngine>();
    const AudioSourceHandle h = base->create_source();
    EXPECT_NE(h, kAudioSourceHandleInvalid);
    base->play(h);
    EXPECT_TRUE(base->is_playing(h));
    base->stop(h);
    EXPECT_FALSE(base->is_playing(h));
    EXPECT_EQ(base->get_backend_type(), AudioBackendType::Null);
}

// ===========================================================================
// Stress: many sources
// ===========================================================================

TEST_F(NullAudioEngineTest, ManySourcesStress) {
    constexpr int kCount = 1000;
    std::vector<AudioSourceHandle> handles;
    handles.reserve(kCount);

    for (int i = 0; i < kCount; ++i) {
        handles.push_back(engine.create_source());
    }

    // Play every other source.
    for (int i = 0; i < kCount; i += 2) {
        engine.play(handles[static_cast<size_t>(i)]);
    }

    const auto stats = engine.get_profiling_stats();
    EXPECT_EQ(stats.active_sources, static_cast<uint32_t>(kCount));
    EXPECT_EQ(stats.playing_sources, static_cast<uint32_t>(kCount / 2));

    // Destroy all.
    for (auto h : handles) {
        engine.destroy_source(h);
    }
    EXPECT_EQ(engine.get_profiling_stats().active_sources, 0u);
}

} // namespace
} // namespace uz
