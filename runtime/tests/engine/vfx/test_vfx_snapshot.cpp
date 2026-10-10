/// Unit tests for phase 8.19.0 — VFX emitter snapshot format.
///
/// Validates element-size helper, header validation, save/load round-trip
/// on a synthetic snapshot, corrupted-input rejection, and the stdout
/// dump format.
///
/// @ref engine/vfx/baker/vfx_snapshot.h
/// @ref docs/engine/25-vfx/36-emitter-snapshot.md

#include <gtest/gtest.h>

#include "engine/vfx/baker/vfx_snapshot.h"

#include <cstring>
#include <vector>

using namespace uz;

namespace {

// Build a minimal but non-trivial snapshot for round-trip tests.
VfxSnapshot MakeTestSnapshot() {
    VfxSnapshot s;
    s.header.emitterName = "fireball_head";
    s.header.frameIndex  = 12345;
    s.header.wallClockNs = 9876543210ull;
    s.header.rngSeed     = 0xCAFEBABEull;
    s.header.capacity    = 4;
    s.header.aliveCount  = 3;

    // positions — 4 float3
    VfxSnapshotChannel pos;
    pos.name         = "positions";
    pos.elementType  = VfxSnapshotElementType::Float3;
    pos.elementCount = 4;
    pos.data.resize(4 * 12);
    const float positions[4][3] = {
        { 1.0f,  2.0f,  3.0f},
        {10.0f, 20.0f, 30.0f},
        {-1.0f, -2.0f, -3.0f},
        { 0.0f,  0.0f,  0.0f},
    };
    std::memcpy(pos.data.data(), positions, sizeof(positions));
    s.channels.push_back(std::move(pos));

    // ages — 4 float
    VfxSnapshotChannel age;
    age.name         = "ages";
    age.elementType  = VfxSnapshotElementType::Float;
    age.elementCount = 4;
    age.data.resize(4 * 4);
    const float ages[4] = {0.0f, 0.5f, 1.0f, 1.5f};
    std::memcpy(age.data.data(), ages, sizeof(ages));
    s.channels.push_back(std::move(age));

    return s;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Element size / name
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnapshotElement, SizesMatchLayout) {
    EXPECT_EQ(VfxSnapshotElementSize(VfxSnapshotElementType::Float),  4u);
    EXPECT_EQ(VfxSnapshotElementSize(VfxSnapshotElementType::Float2), 8u);
    EXPECT_EQ(VfxSnapshotElementSize(VfxSnapshotElementType::Float3), 12u);
    EXPECT_EQ(VfxSnapshotElementSize(VfxSnapshotElementType::Float4), 16u);
    EXPECT_EQ(VfxSnapshotElementSize(VfxSnapshotElementType::Uint32), 4u);
    EXPECT_EQ(VfxSnapshotElementSize(VfxSnapshotElementType::Int32),  4u);
    EXPECT_EQ(VfxSnapshotElementSize(VfxSnapshotElementType::Half),   2u);
    EXPECT_EQ(VfxSnapshotElementSize(VfxSnapshotElementType::Bool),   1u);
}

TEST(VfxSnapshotElement, NamesStable) {
    EXPECT_STREQ(VfxSnapshotElementTypeName(VfxSnapshotElementType::Float),  "Float");
    EXPECT_STREQ(VfxSnapshotElementTypeName(VfxSnapshotElementType::Float3), "Float3");
    EXPECT_STREQ(VfxSnapshotElementTypeName(VfxSnapshotElementType::Uint32), "Uint32");
}

// ─────────────────────────────────────────────────────────────────────────────
// Validation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnapshot, DefaultIsValid) {
    VfxSnapshot s;
    EXPECT_TRUE(s.IsValid());
}

TEST(VfxSnapshot, InvalidWhenAliveExceedsCapacity) {
    VfxSnapshot s;
    s.header.capacity   = 10;
    s.header.aliveCount = 11;
    EXPECT_FALSE(s.IsValid());
}

TEST(VfxSnapshot, InvalidWhenChannelSizeMismatch) {
    VfxSnapshot s;
    VfxSnapshotChannel c;
    c.name = "pos";
    c.elementType = VfxSnapshotElementType::Float3;
    c.elementCount = 4;
    c.data.resize(10); // wrong — expected 4 * 12 = 48
    s.channels.push_back(std::move(c));
    EXPECT_FALSE(s.IsValid());
}

TEST(VfxSnapshot, InvalidWhenDuplicateChannelNames) {
    VfxSnapshot s;
    VfxSnapshotChannel c1;
    c1.name = "pos"; c1.elementType = VfxSnapshotElementType::Float;
    c1.elementCount = 0; c1.data = {};
    VfxSnapshotChannel c2 = c1;
    s.channels.push_back(c1);
    s.channels.push_back(c2);
    EXPECT_FALSE(s.IsValid());
}

TEST(VfxSnapshot, InvalidWhenEmitterNameTooLong) {
    VfxSnapshot s;
    s.header.emitterName = std::string(64, 'x'); // limit is 63 chars
    EXPECT_FALSE(s.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// Save + Load round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnapshot, SaveLoadRoundTripPreservesHeader) {
    const auto s = MakeTestSnapshot();
    const auto bytes = VfxSnapshotSaveToBytes(s);
    ASSERT_FALSE(bytes.empty());

    VfxSnapshot loaded;
    std::string err;
    ASSERT_TRUE(VfxSnapshotLoadFromBytes(bytes, loaded, err)) << err;

    EXPECT_EQ(loaded.header.emitterName, s.header.emitterName);
    EXPECT_EQ(loaded.header.frameIndex,  s.header.frameIndex);
    EXPECT_EQ(loaded.header.wallClockNs, s.header.wallClockNs);
    EXPECT_EQ(loaded.header.rngSeed,     s.header.rngSeed);
    EXPECT_EQ(loaded.header.capacity,    s.header.capacity);
    EXPECT_EQ(loaded.header.aliveCount,  s.header.aliveCount);
}

TEST(VfxSnapshot, SaveLoadRoundTripPreservesChannelData) {
    const auto s = MakeTestSnapshot();
    const auto bytes = VfxSnapshotSaveToBytes(s);
    VfxSnapshot loaded;
    std::string err;
    ASSERT_TRUE(VfxSnapshotLoadFromBytes(bytes, loaded, err));

    ASSERT_EQ(loaded.channels.size(), s.channels.size());
    for (size_t i = 0; i < s.channels.size(); ++i) {
        EXPECT_EQ(loaded.channels[i].name,         s.channels[i].name);
        EXPECT_EQ(loaded.channels[i].elementType,  s.channels[i].elementType);
        EXPECT_EQ(loaded.channels[i].elementCount, s.channels[i].elementCount);
        ASSERT_EQ(loaded.channels[i].data.size(),  s.channels[i].data.size());
        EXPECT_EQ(std::memcmp(loaded.channels[i].data.data(),
                              s.channels[i].data.data(),
                              s.channels[i].data.size()), 0);
    }
}

TEST(VfxSnapshot, InvalidSnapshotFailsToSave) {
    VfxSnapshot s;
    s.header.capacity = 1; s.header.aliveCount = 2; // invalid
    EXPECT_TRUE(VfxSnapshotSaveToBytes(s).empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Load error handling
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnapshot, LoadRejectsTooSmall) {
    std::vector<uint8_t> tiny{0, 1, 2, 3};
    VfxSnapshot s;
    std::string err;
    EXPECT_FALSE(VfxSnapshotLoadFromBytes(tiny, s, err));
    EXPECT_FALSE(err.empty());
}

TEST(VfxSnapshot, LoadRejectsBadMagic) {
    std::vector<uint8_t> bad(256, 0);
    bad[0] = 'X'; // wrong magic
    VfxSnapshot s;
    std::string err;
    EXPECT_FALSE(VfxSnapshotLoadFromBytes(bad, s, err));
    EXPECT_NE(err.find("magic"), std::string::npos);
}

TEST(VfxSnapshot, LoadRejectsFutureVersion) {
    auto bytes = VfxSnapshotSaveToBytes(MakeTestSnapshot());
    ASSERT_FALSE(bytes.empty());
    // Version is at bytes[8..12]. Bump it to 99.
    bytes[8] = 99; bytes[9] = 0; bytes[10] = 0; bytes[11] = 0;
    VfxSnapshot s;
    std::string err;
    EXPECT_FALSE(VfxSnapshotLoadFromBytes(bytes, s, err));
    EXPECT_NE(err.find("version"), std::string::npos);
}

TEST(VfxSnapshot, LoadRejectsTruncatedChannelData) {
    auto bytes = VfxSnapshotSaveToBytes(MakeTestSnapshot());
    ASSERT_GT(bytes.size(), 256u);
    bytes.resize(bytes.size() - 16); // truncate tail of last channel
    VfxSnapshot s;
    std::string err;
    EXPECT_FALSE(VfxSnapshotLoadFromBytes(bytes, s, err));
    EXPECT_FALSE(err.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Dump
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnapshot, DumpContainsEmitterNameAndChannels) {
    const auto s = MakeTestSnapshot();
    const auto dump = VfxSnapshotDump(s);
    EXPECT_NE(dump.find("fireball_head"), std::string::npos);
    EXPECT_NE(dump.find("positions"),     std::string::npos);
    EXPECT_NE(dump.find("ages"),          std::string::npos);
    EXPECT_NE(dump.find("Float3"),        std::string::npos);
    EXPECT_NE(dump.find("12345"),         std::string::npos); // frame
}
