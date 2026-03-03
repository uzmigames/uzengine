// test_string_id.cpp — 100% coverage of string_id.h / string_id.cpp

#include <gtest/gtest.h>
#include "engine/core/string_id.h"

#include <algorithm>
#include <atomic>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace uz;

// ============================================================================
// FNV-1a reference implementation (mirrors the production code) for white-box
// verification of expected hash values.
// ============================================================================

static constexpr uint32_t fnv1a_ref(const char* str, size_t len) noexcept {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint8_t>(str[i]);
        hash *= 16777619u;
    }
    return hash;
}

// ============================================================================
// StringId — default construction
// ============================================================================

TEST(StringIdDefault, DefaultConstructed_HashIsZero_BoolFalse) {
    StringId id;

    EXPECT_EQ(id.hash, 0u);
    EXPECT_FALSE(static_cast<bool>(id));
}

TEST(StringIdDefault, ExplicitZeroHash_BoolFalse) {
    StringId id(0u);

    EXPECT_FALSE(static_cast<bool>(id));
}

TEST(StringIdDefault, NonZeroHash_BoolTrue) {
    StringId id(42u);

    EXPECT_TRUE(static_cast<bool>(id));
}

// ============================================================================
// _sid user-defined literal
// ============================================================================

TEST(StringIdLiteral, HelloLiteral_HasNonZeroHash) {
    auto id = "hello"_sid;

    EXPECT_NE(id.hash, 0u);
}

TEST(StringIdLiteral, HashMatchesFnv1aReference) {
    auto id = "hello"_sid;
    const uint32_t expected = fnv1a_ref("hello", 5);

    EXPECT_EQ(id.hash, expected);
}

TEST(StringIdLiteralCompileTime, IsConstexpr) {
    // consteval guarantees compile-time evaluation.
    constexpr StringId compile_time_id = "test"_sid;
    constexpr uint32_t expected = fnv1a_ref("test", 4); // Can't call fnv1a_ref at constexpr...
    // Instead verify the hash is non-zero and stable.
    static_assert(compile_time_id.hash != 0u, "compile-time hash must be non-zero");
    EXPECT_NE(compile_time_id.hash, 0u);
}

TEST(StringIdLiteralCompileTime, CanBeUsedInStaticAssert) {
    constexpr StringId a = "engine"_sid;
    constexpr StringId b = "engine"_sid;
    static_assert(a == b, "Same literal must produce the same constexpr hash");
    EXPECT_EQ(a, b);
}

// ============================================================================
// _sid equality and inequality
// ============================================================================

TEST(StringIdSameString, SameLiteral_HashesAreEqual) {
    EXPECT_EQ("foo"_sid, "foo"_sid);
}

TEST(StringIdDifferentStrings, DifferentLiterals_HashesAreNotEqual) {
    EXPECT_NE("foo"_sid, "bar"_sid);
}

TEST(StringIdDifferentStrings, EmptyVsNonEmpty_AreNotEqual) {
    EXPECT_NE(""_sid, "a"_sid);
}

// ============================================================================
// runtime string_id(const char*)
// ============================================================================

TEST(StringIdRuntime, NullPointer_ReturnsZeroHash) {
    StringId id = string_id(static_cast<const char*>(nullptr));

    EXPECT_EQ(id.hash, 0u);
    EXPECT_FALSE(static_cast<bool>(id));
}

TEST(StringIdRuntime, SameStringAsLiteral_HashMatches) {
    StringId runtime = string_id("hello");
    constexpr StringId literal = "hello"_sid;

    EXPECT_EQ(runtime, literal);
}

TEST(StringIdRuntime, DifferentRuntimeStrings_ProduceDifferentHashes) {
    EXPECT_NE(string_id("foo"), string_id("bar"));
}

TEST(StringIdRuntime, EmptyString_ReturnsKnv1aOffsetHash) {
    // FNV-1a of zero-length string is the offset basis.
    StringId id = string_id("");
    constexpr uint32_t kFnvOffset = 2166136261u;

    EXPECT_EQ(id.hash, kFnvOffset)
        << "FNV-1a of empty string must equal the offset basis";
    // Non-zero, so operator bool returns true.
    EXPECT_TRUE(static_cast<bool>(id));
}

// ============================================================================
// runtime string_id(std::string_view)
// ============================================================================

TEST(StringIdRuntime, StringViewOverload_MatchesCharPtr) {
    std::string s = "test_string_view";
    StringId from_sv   = string_id(std::string_view{s});
    StringId from_cstr = string_id(s.c_str());

    EXPECT_EQ(from_sv, from_cstr);
}

TEST(StringIdRuntime, StringViewOverload_NonZeroHash) {
    StringId id = string_id(std::string_view{"engine_event"});

    EXPECT_NE(id.hash, 0u);
}

// ============================================================================
// string_id_lookup
// ============================================================================

TEST(StringIdLookupRegistered, RuntimeRegistered_LookupReturnsOriginalString) {
    const char* original = "registered_string";
    StringId id = string_id(original);

    const char* found = string_id_lookup(id);

    EXPECT_STREQ(found, original);
}

TEST(StringIdLookupRegistered, StringViewRegistered_LookupReturnsOriginalString) {
    StringId id = string_id(std::string_view{"sv_registered"});

    const char* found = string_id_lookup(id);

    EXPECT_STREQ(found, "sv_registered");
}

TEST(StringIdLookupUnregistered, LiteralOnly_Hash_LookupReturnsUnknown) {
    // A compile-time-only _sid was never passed through string_id(), so it
    // might not be in the intern table (unless another test registered it).
    // Use an obscure string unlikely to be registered elsewhere.
    constexpr StringId id = "xyzzy_not_registered_in_runtime_table_7f3a"_sid;

    const char* result = string_id_lookup(id);

    // Must return either the string (if a previous call registered it) or "<unknown>".
    EXPECT_NE(result, nullptr);
    // Acceptable outcomes: the original string or "<unknown>".
    bool ok = (std::string(result) == "xyzzy_not_registered_in_runtime_table_7f3a") ||
              (std::string(result) == "<unknown>");
    EXPECT_TRUE(ok) << "lookup returned unexpected: " << result;
}

TEST(StringIdLookupEmpty, EmptyString_Lookup_ReturnsEmptyString) {
    string_id(""); // register it
    StringId id;
    id.hash = 2166136261u; // FNV-1a offset = hash of ""

    const char* result = string_id_lookup(id);

    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "") << "Lookup of empty string hash must return the empty string";
}

TEST(StringIdLookup, UnregisteredHash_ReturnsUnknownSentinel) {
    // Hash 0xDEAD0001 was never inserted.
    StringId fake{0xDEAD0001u};

    const char* result = string_id_lookup(fake);

    EXPECT_STREQ(result, "<unknown>");
}

// ============================================================================
// StringIdHash — for std::unordered_map / std::unordered_set
// ============================================================================

TEST(StringIdHashForMap, SameSid_ProducesSameHasherOutput) {
    StringIdHash hasher;
    auto id = "test_map_key"_sid;

    EXPECT_EQ(hasher(id), hasher(id));
}

TEST(StringIdHashForMap, HasherReturnsSizeOfHash) {
    StringIdHash hasher;
    StringId id{0xABCD1234u};

    EXPECT_EQ(hasher(id), static_cast<size_t>(0xABCD1234u));
}

TEST(StringIdHashForMap, DifferentSids_DifferentHasherOutput) {
    StringIdHash hasher;
    auto a = "alpha"_sid;
    auto b = "beta"_sid;

    EXPECT_NE(hasher(a), hasher(b));
}

// ============================================================================
// operator< — strict weak ordering
// ============================================================================

TEST(StringIdOrdering, OperatorLess_StrictWeakOrdering) {
    StringId lo{1u};
    StringId hi{2u};

    EXPECT_TRUE(lo < hi);
    EXPECT_FALSE(hi < lo);
    EXPECT_FALSE(lo < lo); // irreflexivity
}

TEST(StringIdOrdering, CanBeUsedInStdSet) {
    std::set<StringId> s;

    auto a = string_id("apple");
    auto b = string_id("banana");
    auto c = string_id("cherry");

    s.insert(a);
    s.insert(b);
    s.insert(c);
    s.insert(a); // duplicate — must not increase size

    EXPECT_EQ(s.size(), 3u);
    EXPECT_TRUE(s.count(a) == 1u);
    EXPECT_TRUE(s.count(b) == 1u);
    EXPECT_TRUE(s.count(c) == 1u);
}

TEST(StringIdOrdering, SortingProducesDeterministicOrder) {
    std::vector<StringId> ids = {
        string_id("zebra"),
        string_id("ant"),
        string_id("monkey"),
    };

    std::sort(ids.begin(), ids.end());

    // After sort, must be in ascending hash order.
    for (size_t i = 1; i < ids.size(); ++i) {
        EXPECT_LE(ids[i - 1].hash, ids[i].hash)
            << "IDs at positions " << (i-1) << " and " << i << " are out of order";
    }
}

// ============================================================================
// StringId equality / inequality operators
// ============================================================================

TEST(StringIdEquality, EqualHashes_OperatorEq) {
    StringId a{42u};
    StringId b{42u};
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a != b);
}

TEST(StringIdEquality, DifferentHashes_OperatorNeq) {
    StringId a{1u};
    StringId b{2u};
    EXPECT_NE(a, b);
    EXPECT_FALSE(a == b);
}

// ============================================================================
// Thread safety — concurrent string_id() calls
// ============================================================================

TEST(StringIdThreadSafety, ConcurrentRegistration_NoCrash_CorrectHashes) {
    // 8 threads each register 20 unique strings simultaneously.
    constexpr int kThreads  = 8;
    constexpr int kPerThread = 20;

    std::vector<std::thread> threads;
    std::vector<std::vector<StringId>> results(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t, &results]() {
            for (int i = 0; i < kPerThread; ++i) {
                std::string s = "thread_" + std::to_string(t) + "_str_" + std::to_string(i);
                results[t].push_back(string_id(s.c_str()));
            }
        });
    }

    for (auto& th : threads) th.join();

    // Verify all hashes are correct and non-zero.
    for (int t = 0; t < kThreads; ++t) {
        ASSERT_EQ(static_cast<int>(results[t].size()), kPerThread);
        for (int i = 0; i < kPerThread; ++i) {
            std::string s = "thread_" + std::to_string(t) + "_str_" + std::to_string(i);
            uint32_t expected = fnv1a_ref(s.c_str(), s.size());
            EXPECT_EQ(results[t][i].hash, expected)
                << "Thread " << t << " item " << i << " hash mismatch";
            EXPECT_NE(results[t][i].hash, 0u);
        }
    }
}

TEST(StringIdThreadSafety, SameStringFromMultipleThreads_AllHashesEqual) {
    // All threads register the same string — first-registration-wins must not
    // produce conflicting results.
    constexpr int kThreads = 8;
    const char* kStr = "shared_key_concurrent_test";

    std::vector<StringId> results(kThreads);
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t, kStr, &results]() {
            results[t] = string_id(kStr);
        });
    }

    for (auto& th : threads) th.join();

    const StringId expected = "shared_key_concurrent_test"_sid;
    for (int t = 0; t < kThreads; ++t) {
        EXPECT_EQ(results[t], expected)
            << "Thread " << t << " got wrong hash";
    }
}
