#include <gtest/gtest.h>
#include "uz_random.h"
#include <cmath>
#include <set>

using namespace uz;

TEST(PCGRandomTest, DeterministicWithSameSeed) {
    PCGRandom a(12345);
    PCGRandom b(12345);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(a.Next(), b.Next());
    }
}

TEST(PCGRandomTest, DifferentSeedsDifferentSequence) {
    PCGRandom a(1);
    PCGRandom b(2);
    bool allSame = true;
    for (int i = 0; i < 10; ++i) {
        if (a.Next() != b.Next()) { allSame = false; break; }
    }
    EXPECT_FALSE(allSame);
}

TEST(PCGRandomTest, FloatInRange) {
    PCGRandom rng(42);
    for (int i = 0; i < 10000; ++i) {
        float f = rng.Float();
        EXPECT_GE(f, 0.0f);
        EXPECT_LT(f, 1.0f);
    }
}

TEST(PCGRandomTest, RangeFloat) {
    PCGRandom rng(42);
    for (int i = 0; i < 10000; ++i) {
        float f = rng.Range(5.0f, 10.0f);
        EXPECT_GE(f, 5.0f);
        EXPECT_LT(f, 10.0f);
    }
}

TEST(PCGRandomTest, IntRange) {
    PCGRandom rng(42);
    for (int i = 0; i < 10000; ++i) {
        int val = rng.Int(0, 9);
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 9);
    }
}

TEST(PCGRandomTest, IntRangeCoversAllValues) {
    PCGRandom rng(42);
    std::set<int> seen;
    for (int i = 0; i < 10000; ++i) {
        seen.insert(rng.Int(0, 4));
    }
    EXPECT_EQ(seen.size(), 5u); // 0,1,2,3,4
}

TEST(PCGRandomTest, Bool) {
    PCGRandom rng(42);
    int trueCount = 0;
    for (int i = 0; i < 10000; ++i) {
        if (rng.Bool()) ++trueCount;
    }
    // Should be roughly 50/50
    EXPECT_GT(trueCount, 4000);
    EXPECT_LT(trueCount, 6000);
}

TEST(PCGRandomTest, UnitSphere) {
    PCGRandom rng(42);
    for (int i = 0; i < 1000; ++i) {
        float x, y, z;
        rng.UnitSphere(x, y, z);
        float len = std::sqrt(x * x + y * y + z * z);
        EXPECT_NEAR(len, 1.0f, 1e-4f);
    }
}

TEST(PCGRandomTest, UnitDisk) {
    PCGRandom rng(42);
    for (int i = 0; i < 1000; ++i) {
        float x, y;
        rng.UnitDisk(x, y);
        EXPECT_LT(x * x + y * y, 1.0f);
    }
}

TEST(PCGRandomTest, Reseed) {
    PCGRandom rng(1);
    uint32_t first = rng.Next();
    rng.Seed(1);
    uint32_t second = rng.Next();
    EXPECT_EQ(first, second);
}

TEST(PCGRandomTest, UniformDistribution) {
    PCGRandom rng(42);
    int buckets[10] = {};
    for (int i = 0; i < 100000; ++i) {
        int val = rng.Int(0, 9);
        buckets[val]++;
    }
    // Each bucket should have ~10000 ± 500
    for (int i = 0; i < 10; ++i) {
        EXPECT_GT(buckets[i], 9000);
        EXPECT_LT(buckets[i], 11000);
    }
}
