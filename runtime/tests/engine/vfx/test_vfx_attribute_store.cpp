/// Unit tests for VfxAttributeStore — scoped VFX attribute storage.
///
/// Covers:
///   - Scope isolation (Particles.foo independent of Emitter.foo / System.foo)
///   - Typed setter/getter round-trips for float / Vec3 / Vec4
///   - Enumeration (GetNames / GetCount) per scope
///   - Per-scope and global Clear
///   - CB layout determinism (PackScopeToBuffer) — independent scope sizes
///   - Canonical `<Scope>.<name>` key round-trip
///
/// @ref phase7_7.1.0-vfx-namespaced-attributes

#include <gtest/gtest.h>

#include "engine/vfx/parameters/vfx_attribute_store.h"

#include <array>
#include <cstdint>
#include <string>

using namespace uz;

// ─── Scope enum <-> string ─────────────────────────────────────────────────

TEST(VfxAttributeStore, ScopeToStringAllBranches) {
    EXPECT_EQ(VfxAttributeScopeToString(VfxAttributeScope::Particles), "Particles");
    EXPECT_EQ(VfxAttributeScopeToString(VfxAttributeScope::Emitter),   "Emitter");
    EXPECT_EQ(VfxAttributeScopeToString(VfxAttributeScope::System),    "System");
}

// ─── VfxAttributeKey — canonical form ──────────────────────────────────────

TEST(VfxAttributeKey, ToStringProducesScopeDotName) {
    VfxAttributeKey k{VfxAttributeScope::System, "intensity"};
    EXPECT_EQ(k.ToString(), "System.intensity");
}

TEST(VfxAttributeKey, FromStringRoundTrip) {
    VfxAttributeKey out;
    ASSERT_TRUE(VfxAttributeKey::FromString("Emitter.LoopCount", out));
    EXPECT_EQ(out.scope, VfxAttributeScope::Emitter);
    EXPECT_EQ(out.name, "LoopCount");
}

TEST(VfxAttributeKey, FromStringRejectsMissingDot) {
    VfxAttributeKey out;
    EXPECT_FALSE(VfxAttributeKey::FromString("NoDotHere", out));
}

TEST(VfxAttributeKey, FromStringRejectsUnknownScope) {
    VfxAttributeKey out;
    EXPECT_FALSE(VfxAttributeKey::FromString("Bogus.foo", out));
}

TEST(VfxAttributeKey, FromStringRejectsEmptyName) {
    VfxAttributeKey out;
    EXPECT_FALSE(VfxAttributeKey::FromString("System.", out));
}

TEST(VfxAttributeKey, FromStringRejectsEmptyScope) {
    VfxAttributeKey out;
    EXPECT_FALSE(VfxAttributeKey::FromString(".foo", out));
}

TEST(VfxAttributeKey, EqualityComparesScopeAndName) {
    VfxAttributeKey a{VfxAttributeScope::System, "foo"};
    VfxAttributeKey b{VfxAttributeScope::System, "foo"};
    VfxAttributeKey c{VfxAttributeScope::Emitter, "foo"};
    VfxAttributeKey d{VfxAttributeScope::System, "bar"};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
}

// ─── Scope isolation ───────────────────────────────────────────────────────

TEST(VfxAttributeStore, ScopesAreIndependent) {
    VfxAttributeStore store;
    store.SetFloat(VfxAttributeScope::Particles, "foo", 1.0f);
    store.SetFloat(VfxAttributeScope::Emitter,   "foo", 2.0f);
    store.SetFloat(VfxAttributeScope::System,    "foo", 3.0f);

    EXPECT_FLOAT_EQ(store.GetFloat(VfxAttributeScope::Particles, "foo"), 1.0f);
    EXPECT_FLOAT_EQ(store.GetFloat(VfxAttributeScope::Emitter,   "foo"), 2.0f);
    EXPECT_FLOAT_EQ(store.GetFloat(VfxAttributeScope::System,    "foo"), 3.0f);
}

TEST(VfxAttributeStore, MissingReturnsDefault) {
    VfxAttributeStore store;
    EXPECT_FLOAT_EQ(store.GetFloat(VfxAttributeScope::System, "missing", 42.0f), 42.0f);
}

// ─── Typed round-trips ─────────────────────────────────────────────────────

TEST(VfxAttributeStore, Vec3RoundTripPerScope) {
    VfxAttributeStore store;
    store.SetVec3(VfxAttributeScope::Emitter, "windDir", {1.0f, 0.0f, 0.0f});
    const Vec3 v = store.GetVec3(VfxAttributeScope::Emitter, "windDir");
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

TEST(VfxAttributeStore, Vec4RoundTripPerScope) {
    VfxAttributeStore store;
    store.SetVec4(VfxAttributeScope::System, "teamColor", {0.2f, 0.4f, 0.6f, 0.8f});
    const Vec4 v = store.GetVec4(VfxAttributeScope::System, "teamColor");
    EXPECT_FLOAT_EQ(v.x, 0.2f);
    EXPECT_FLOAT_EQ(v.y, 0.4f);
    EXPECT_FLOAT_EQ(v.z, 0.6f);
    EXPECT_FLOAT_EQ(v.w, 0.8f);
}

// ─── Presence + remove + clear ─────────────────────────────────────────────

TEST(VfxAttributeStore, HasAndRemove) {
    VfxAttributeStore store;
    store.SetFloat(VfxAttributeScope::Emitter, "loopCount", 3.0f);
    EXPECT_TRUE (store.Has(VfxAttributeScope::Emitter, "loopCount"));
    EXPECT_FALSE(store.Has(VfxAttributeScope::System,  "loopCount"));
    EXPECT_TRUE (store.Remove(VfxAttributeScope::Emitter, "loopCount"));
    EXPECT_FALSE(store.Has(VfxAttributeScope::Emitter, "loopCount"));
    EXPECT_FALSE(store.Remove(VfxAttributeScope::Emitter, "loopCount"));
}

TEST(VfxAttributeStore, ClearSingleScopeLeavesOthers) {
    VfxAttributeStore store;
    store.SetFloat(VfxAttributeScope::Particles, "a", 1.0f);
    store.SetFloat(VfxAttributeScope::Emitter,   "b", 2.0f);
    store.SetFloat(VfxAttributeScope::System,    "c", 3.0f);

    store.Clear(VfxAttributeScope::Emitter);
    EXPECT_TRUE (store.Has(VfxAttributeScope::Particles, "a"));
    EXPECT_FALSE(store.Has(VfxAttributeScope::Emitter,   "b"));
    EXPECT_TRUE (store.Has(VfxAttributeScope::System,    "c"));
}

TEST(VfxAttributeStore, ClearAllEmptiesEveryScope) {
    VfxAttributeStore store;
    store.SetFloat(VfxAttributeScope::Particles, "a", 1.0f);
    store.SetFloat(VfxAttributeScope::Emitter,   "b", 2.0f);
    store.SetFloat(VfxAttributeScope::System,    "c", 3.0f);

    store.ClearAll();
    EXPECT_EQ(store.GetCount(VfxAttributeScope::Particles), 0u);
    EXPECT_EQ(store.GetCount(VfxAttributeScope::Emitter),   0u);
    EXPECT_EQ(store.GetCount(VfxAttributeScope::System),    0u);
}

// ─── Enumeration ───────────────────────────────────────────────────────────

TEST(VfxAttributeStore, GetNamesPreservesInsertionOrderPerScope) {
    VfxAttributeStore store;
    store.SetFloat(VfxAttributeScope::System, "first",  1.0f);
    store.SetFloat(VfxAttributeScope::System, "second", 2.0f);
    store.SetFloat(VfxAttributeScope::System, "third",  3.0f);

    const auto names = store.GetNames(VfxAttributeScope::System);
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "first");
    EXPECT_EQ(names[1], "second");
    EXPECT_EQ(names[2], "third");
}

// ─── CB layout / PackScopeToBuffer ─────────────────────────────────────────

TEST(VfxAttributeStore, GetUploadSizeIsSixteenBytesPerAttribute) {
    VfxAttributeStore store;
    EXPECT_EQ(store.GetUploadSize(VfxAttributeScope::System), 0u);

    store.SetFloat(VfxAttributeScope::System, "a", 1.0f);
    store.SetVec3 (VfxAttributeScope::System, "b", {2, 3, 4});

    EXPECT_EQ(store.GetUploadSize(VfxAttributeScope::System), 32u);
    // Other scopes remain empty.
    EXPECT_EQ(store.GetUploadSize(VfxAttributeScope::Particles), 0u);
    EXPECT_EQ(store.GetUploadSize(VfxAttributeScope::Emitter),   0u);
}

TEST(VfxAttributeStore, PackScopePreservesInsertionOrder) {
    VfxAttributeStore store;
    store.SetVec4(VfxAttributeScope::System, "k0", {0.0f, 1.0f, 2.0f, 3.0f});
    store.SetVec4(VfxAttributeScope::System, "k1", {4.0f, 5.0f, 6.0f, 7.0f});

    std::array<float, 8> buf{};
    const uint32_t written = store.PackScopeToBuffer(
        VfxAttributeScope::System, buf.data(),
        static_cast<uint32_t>(buf.size() * sizeof(float)));
    ASSERT_EQ(written, 32u);

    EXPECT_FLOAT_EQ(buf[0], 0.0f); EXPECT_FLOAT_EQ(buf[1], 1.0f);
    EXPECT_FLOAT_EQ(buf[2], 2.0f); EXPECT_FLOAT_EQ(buf[3], 3.0f);
    EXPECT_FLOAT_EQ(buf[4], 4.0f); EXPECT_FLOAT_EQ(buf[5], 5.0f);
    EXPECT_FLOAT_EQ(buf[6], 6.0f); EXPECT_FLOAT_EQ(buf[7], 7.0f);
}

TEST(VfxAttributeStore, PackRefusesUndersizedBuffer) {
    VfxAttributeStore store;
    store.SetVec4(VfxAttributeScope::System, "k0", {1, 2, 3, 4});

    std::array<float, 2> tooSmall{};
    const uint32_t written = store.PackScopeToBuffer(
        VfxAttributeScope::System, tooSmall.data(),
        static_cast<uint32_t>(tooSmall.size() * sizeof(float)));
    EXPECT_EQ(written, 0u);
}

// ─── Key-qualified convenience API ─────────────────────────────────────────

TEST(VfxAttributeStore, KeyOverloadsAgreeWithScopeOverloads) {
    VfxAttributeStore store;
    const VfxAttributeKey sysIntensity{VfxAttributeScope::System, "intensity"};

    store.SetFloat(sysIntensity, 5.0f);
    EXPECT_FLOAT_EQ(store.GetFloat(sysIntensity, 0.0f), 5.0f);
    // Same value reachable via the legacy (scope, name) overload.
    EXPECT_FLOAT_EQ(store.GetFloat(VfxAttributeScope::System, "intensity"), 5.0f);
}
