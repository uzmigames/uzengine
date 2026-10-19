#include "pathtracing/light_bvh.h"
#include "../../../shaders/runtime/include/light_bvh_math.hlsli"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {
using uz::LightBVH;
using uz::LightBVHLight;
using uz::Vec3;

LightBVHLight Triangle(float z, float emission, float x = 0.f) {
    LightBVHLight light{};
    light.v0 = {x - .5f, -.5f, z};
    light.v1 = {x + .5f, -.5f, z};
    light.v2 = {x, .5f, z};
    light.area = .5f;
    light.normal = {0, 0, 1};
    light.emission = {emission, emission, emission};
    return light;
}

double Power(const LightBVHLight& light) {
    return (double(light.emission.x) * .2126 + double(light.emission.y) * .7152
            + double(light.emission.z) * .0722) * light.area;
}

double NodeImportance(const uz::LightBVHNode& node, Vec3 position, Vec3 normal) {
    const Vec3 delta = (node.aabbMin + node.aabbMax) * .5f - position;
    const double distanceSquared = std::max(double(delta.LengthSq()), 1.0);
    const double cosine = std::max(double(normal.Dot(delta)) / std::sqrt(distanceSquared), .01);
    double orientation=1;
    if (node.coneCosHalfAngle > 0) {
        double distance=std::sqrt(std::max(double(delta.LengthSq()),1e-20));
        double radius=.5*(node.aabbMax-node.aabbMin).Length();
        double theta=std::acos(std::clamp(std::abs(double(node.coneAxis.Dot(delta)))/distance,0.0,1.0));
        double subtended=std::asin(std::min(radius/distance,1.0));
        orientation=std::max(std::cos(std::max(0.0,theta-std::acos(double(node.coneCosHalfAngle))-subtended)),.01);
    }
    return node.totalEmissionPower * cosine * orientation / distanceSquared;
}

double PathPDF(const LightBVH& bvh, uint32_t light, Vec3 position, Vec3 normal) {
    double pdf = 1;
    for (uint32_t id = bvh.lights[light].leaf; bvh.nodes[id].parent != UINT32_MAX;) {
        const auto& parent = bvh.nodes[bvh.nodes[id].parent];
        const double left = NodeImportance(bvh.nodes[parent.leftChild], position, normal);
        const double right = NodeImportance(bvh.nodes[parent.rightChild], position, normal);
        const double q = std::clamp(left / (left + right), 1e-6, 1.0 - 1e-6);
        pdf *= id == parent.leftChild ? q : 1 - q;
        id = bvh.nodes[id].parent;
    }
    return pdf;
}

void ExpectVectorNear(Vec3 actual, Vec3 expected) {
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.z, expected.z);
}
} // namespace

TEST(LightBVH, StructuredBufferLayouts) {
    EXPECT_EQ(sizeof(uz::LightBVHNode), 64u);
    EXPECT_EQ(offsetof(uz::LightBVHNode, totalEmissionPower), 12u);
    EXPECT_EQ(offsetof(uz::LightBVHNode, leftChild), 28u);
    EXPECT_EQ(offsetof(uz::LightBVHNode, rightChild), 48u);
    EXPECT_EQ(offsetof(uz::LightBVHNode, parent), 60u);
    EXPECT_EQ(sizeof(LightBVHLight), 112u);
    EXPECT_EQ(offsetof(LightBVHLight, normal), 48u);
    EXPECT_EQ(offsetof(LightBVHLight, emission), 64u);
    EXPECT_EQ(offsetof(LightBVHLight, uv0), 80u);
    EXPECT_EQ(offsetof(LightBVHLight, leaf), 104u);
    EXPECT_EQ(sizeof(uz::LightBVHConfig), 16u);
}

TEST(LightBVH, NeeAndEmitterHitMISConserveEnergyWithMatchingSelectionPDF) {
    EXPECT_FLOAT_EQ(uz::LightBVHSolidAnglePDF(.2f,2.f,9.f,.5f),1.8f);
    EXPECT_FLOAT_EQ(uz::LightBVHBalanceWeight(.75f,.25f),.75f);
    LightBVH bvh;
    bvh.Build({Triangle(2, 2), Triangle(5, 15), Triangle(7, 1)});
    const Vec3 position{0,0,0}, normal{0,0,1};
    for (unsigned i = 0; i < 1024; ++i) {
        float selectedPdf;
        const uint32_t id = bvh.Sample(position,normal,(i+.5f)/1024.f,selectedPdf);
        const auto& light = bvh.lights[id];
        const float sampled = uz::LightBVHSolidAnglePDF(selectedPdf,light.area,
            light.v0.z*light.v0.z,.8f);
        const float evaluated = uz::LightBVHSolidAnglePDF(bvh.EvaluatePDF(position,normal,id),
            light.area,light.v0.z*light.v0.z,.8f);
        for (float bsdfPdf : {.001f,.1f,10.f})
            for (float samples : {1.f,4.f,32.f}) {
                const float nee = uz::LightBVHBalanceWeight(samples*sampled,bsdfPdf);
                const float hit = uz::LightBVHBalanceWeight(bsdfPdf,samples*evaluated);
                EXPECT_NEAR(nee+hit,1.f,2e-6f);
            }
    }
}

TEST(LightBVH, HundredTrianglesAggregateRootPower) {
    std::vector<LightBVHLight> input;
    double expected = 0;
    for (unsigned i = 0; i < 100; ++i) {
        auto light = Triangle(2.f + i, 1.f + i, float(i % 7));
        light.emission = {1.f + i, 2.f + i, 3.f + i};
        expected += Power(light);
        input.push_back(light);
    }
    LightBVH bvh;
    bvh.Build(input);
    ASSERT_EQ(bvh.lights.size(), 100u);
    ASSERT_EQ(bvh.nodes.size(), 199u);
    EXPECT_EQ(bvh.nodes[0].parent, UINT32_MAX);
    EXPECT_NEAR(bvh.nodes[0].totalEmissionPower, expected, expected * 1e-6);
}

TEST(LightBVH, EveryParentUnionsChildBoundsAndPower) {
    std::vector<LightBVHLight> input;
    for (unsigned i = 0; i < 100; ++i)
        input.push_back(Triangle(2.f + i * .5f, 1.f + i, float(i % 11) - 5.f));
    LightBVH bvh;
    bvh.Build(input);
    ASSERT_EQ(bvh.nodes.size(), 199u);
    unsigned leaves = 0;
    for (uint32_t id = 0; id < bvh.nodes.size(); ++id) {
        SCOPED_TRACE(id);
        const auto& node = bvh.nodes[id];
        if (node.lightCount) {
            ++leaves;
            ASSERT_EQ(node.lightCount, 1u);
            ASSERT_LT(node.lightOffset, bvh.lights.size());
            const auto& light = bvh.lights[node.lightOffset];
            EXPECT_EQ(light.leaf, id);
            ExpectVectorNear(node.aabbMin, {light.v0.x, -.5f, light.v0.z});
            ExpectVectorNear(node.aabbMax, {light.v1.x, .5f, light.v0.z});
            EXPECT_NEAR(node.totalEmissionPower, Power(light), Power(light) * 1e-6);
            continue;
        }
        ASSERT_LT(node.leftChild, bvh.nodes.size());
        ASSERT_LT(node.rightChild, bvh.nodes.size());
        EXPECT_NE(node.leftChild, node.rightChild);
        const auto& left = bvh.nodes[node.leftChild];
        const auto& right = bvh.nodes[node.rightChild];
        EXPECT_EQ(left.parent, id);
        EXPECT_EQ(right.parent, id);
        ExpectVectorNear(node.aabbMin, {std::min(left.aabbMin.x, right.aabbMin.x),
            std::min(left.aabbMin.y, right.aabbMin.y), std::min(left.aabbMin.z, right.aabbMin.z)});
        ExpectVectorNear(node.aabbMax, {std::max(left.aabbMax.x, right.aabbMax.x),
            std::max(left.aabbMax.y, right.aabbMax.y), std::max(left.aabbMax.z, right.aabbMax.z)});
        EXPECT_FLOAT_EQ(node.totalEmissionPower, left.totalEmissionPower + right.totalEmissionPower);
    }
    EXPECT_EQ(leaves, 100u);
}

TEST(LightBVH, SampleAndEvaluatePDFMatchBranchProductsAndNormalize) {
    std::vector<LightBVHLight> input;
    for (unsigned i = 0; i < 17; ++i)
        input.push_back(Triangle(2.f + i * .5f, 1.f + i * i, float(i % 3) - 1.f));
    LightBVH bvh;
    bvh.Build(input);
    const Vec3 position{.25f, -.75f, 0};
    const Vec3 normal{0, 0, 1};
    double sum = 0;
    for (uint32_t light = 0; light < bvh.lights.size(); ++light) {
        const double expected = PathPDF(bvh, light, position, normal);
        const float pdf = bvh.EvaluatePDF(position, normal, light);
        EXPECT_NEAR(pdf, expected, expected * 2e-6);
        EXPECT_GT(pdf, 0.f);
        sum += pdf;
    }
    EXPECT_NEAR(sum, 1.0, 1e-6);
    for (unsigned i = 0; i < 4096; ++i) {
        float pdf = 0;
        const uint32_t light = bvh.Sample(position, normal, (i + .5f) / 4096.f, pdf);
        ASSERT_LT(light, bvh.lights.size());
        const float evaluated = bvh.EvaluatePDF(position, normal, light);
        EXPECT_NEAR(pdf, evaluated, evaluated * 2e-6);
        EXPECT_NEAR(pdf, PathPDF(bvh, light, position, normal), pdf * 2e-6);
    }
}

TEST(LightBVH, EmptyAndDegenerateEmittersAreRemovedAndBuildResets) {
    LightBVH bvh;
    bvh.Build({});
    EXPECT_TRUE(bvh.nodes.empty());
    EXPECT_TRUE(bvh.lights.empty());
    auto zeroArea = Triangle(2, 1);
    zeroArea.area = 0;
    auto negativeArea = Triangle(2, 1);
    negativeArea.area = -1;
    bvh.Build({Triangle(2, 0), Triangle(2, -1), zeroArea, negativeArea});
    EXPECT_TRUE(bvh.nodes.empty());
    EXPECT_TRUE(bvh.lights.empty());
    bvh.Build({zeroArea, Triangle(3, 4), Triangle(2, 0)});
    ASSERT_EQ(bvh.lights.size(), 1u);
    ASSERT_EQ(bvh.nodes.size(), 1u);
    float pdf = 0;
    EXPECT_EQ(bvh.Sample({0, 0, 0}, {0, 0, 1}, .5f, pdf), 0u);
    EXPECT_FLOAT_EQ(pdf, 1.f);
    EXPECT_FLOAT_EQ(bvh.EvaluatePDF({0, 0, 0}, {0, 0, 1}, 0), 1.f);
    bvh.Build({});
    EXPECT_TRUE(bvh.nodes.empty());
    EXPECT_TRUE(bvh.lights.empty());
}

TEST(LightBVH, ReceiverCosineAndBranchProbabilityAreClamped) {
    LightBVH bvh;
    bvh.Build({Triangle(2, 1e-12f), Triangle(4, 1e12f)});
    const Vec3 position{0, 0, 0}, normal{0, 0, -1};
    for (const auto& light : bvh.lights) {
        const auto& node = bvh.nodes[light.leaf];
        EXPECT_NEAR(bvh.Importance(light.leaf, position, normal),
                    node.totalEmissionPower * .01 / (light.v0.z * light.v0.z),
                    NodeImportance(node, position, normal) * 1e-6);
    }
    EXPECT_NEAR(bvh.EvaluatePDF(position, normal, 0), 1e-6, 1e-12);
    EXPECT_FLOAT_EQ(bvh.EvaluatePDF(position, normal, 1), 1.f - 1e-6f);
    float pdf = 0;
    EXPECT_EQ(bvh.Sample(position, normal, 0.f, pdf), 0u);
    EXPECT_FLOAT_EQ(pdf, 1e-6f);
    bvh.Build({Triangle(2, 1e12f), Triangle(4, 1e-12f)});
    EXPECT_FLOAT_EQ(bvh.EvaluatePDF(position, normal, 0), 1.f - 1e-6f);
    EXPECT_FLOAT_EQ(bvh.EvaluatePDF(position, normal, 1), 1.f - (1.f - 1e-6f));
}

TEST(LightBVH, TwoTriangleStratifiedHistogramMatchesPowerOverDistanceSquared) {
    LightBVH bvh;
    bvh.Build({Triangle(2, 2), Triangle(5, 15)});
    constexpr unsigned sampleCount = 100000;
    unsigned histogram[2]{};
    const Vec3 position{0, 0, 0}, normal{0, 0, 1};
    double weights[2]{}, sum = 0;
    for (unsigned i = 0; i < 2; ++i) {
        weights[i] = Power(bvh.lights[i]) / std::pow(bvh.lights[i].v0.z, 2);
        sum += weights[i];
    }
    for (unsigned i = 0; i < sampleCount; ++i) {
        float pdf = 0;
        const uint32_t light = bvh.Sample(position, normal, (i + .5f) / sampleCount, pdf);
        ASSERT_LT(light, 2u);
        ++histogram[light];
    }
    double chiSquared = 0;
    for (unsigned i = 0; i < 2; ++i) {
        const double probability = weights[i] / sum;
        const double expected = sampleCount * probability;
        const double error = histogram[i] - expected;
        EXPECT_NEAR(bvh.EvaluatePDF(position, normal, i), probability, 1e-6);
        EXPECT_LE(std::abs(error), 2.0);
        chiSquared += error * error / expected;
        std::printf("LightBVH two-light bin %u: observed=%u expected=%.6f pdf=%.9f\n",
                    i, histogram[i], expected, probability);
    }
    std::printf("LightBVH two-light chi-square=%.9f samples=%u\n", chiSquared, sampleCount);
    EXPECT_LT(chiSquared, .01);
}

TEST(LightBVH, HierarchicalHistogramAndInverseProbabilityEstimatorBeatUniformVariance) {
    std::vector<LightBVHLight> input;
    for (unsigned i = 0; i < 8; ++i)
        input.push_back(Triangle(2.f + i * .5f, float(1u << i)));
    LightBVH bvh;
    bvh.Build(input);
    constexpr unsigned sampleCount = 100000;
    const Vec3 position{0, 0, 0}, normal{0, 0, 1};
    std::vector<unsigned> histogram(bvh.lights.size());
    std::vector<double> pdfs, contributions;
    double target = 0, theoreticalSecond = 0, uniformSecond = 0;
    for (uint32_t i = 0; i < bvh.lights.size(); ++i) {
        const double contribution = Power(bvh.lights[i]) / std::pow(bvh.lights[i].v0.z, 2);
        const double pdf = bvh.EvaluatePDF(position, normal, i);
        ASSERT_GT(pdf, 0);
        pdfs.push_back(pdf);
        contributions.push_back(contribution);
        target += contribution;
        theoreticalSecond += contribution * contribution / pdf;
        uniformSecond += contribution * contribution * bvh.lights.size();
    }
    double sum = 0, second = 0, uniformSum = 0, uniformSampleSecond = 0;
    for (unsigned i = 0; i < sampleCount; ++i) {
        const float u = (i + .5f) / sampleCount;
        float pdf = 0;
        const uint32_t light = bvh.Sample(position, normal, u, pdf);
        ASSERT_LT(light, bvh.lights.size());
        ++histogram[light];
        const double value = contributions[light] / pdf;
        sum += value;
        second += value * value;
        const auto uniformLight = std::min(size_t(u * bvh.lights.size()), bvh.lights.size() - 1);
        const double uniformValue = contributions[uniformLight] * bvh.lights.size();
        uniformSum += uniformValue;
        uniformSampleSecond += uniformValue * uniformValue;
    }
    double chiSquared = 0;
    for (unsigned i = 0; i < histogram.size(); ++i) {
        const double expected = sampleCount * pdfs[i];
        const double error = histogram[i] - expected;
        EXPECT_LE(std::abs(error), 2.0);
        chiSquared += error * error / expected;
        std::printf("LightBVH hierarchical bin %u: observed=%u expected=%.6f pdf=%.9f\n",
                    i, histogram[i], expected, pdfs[i]);
    }
    const double mean = sum / sampleCount, uniformMean = uniformSum / sampleCount;
    const double variance = second / sampleCount - mean * mean;
    const double uniformVariance = uniformSampleSecond / sampleCount - uniformMean * uniformMean;
    const double expectedVariance = theoreticalSecond - target * target;
    const double expectedUniformVariance = uniformSecond - target * target;
    std::printf("LightBVH hierarchical chi-square=%.9f samples=%u target=%.9f "
                "mean=%.9f variance=%.9f expected-variance=%.9f "
                "uniform-mean=%.9f uniform-variance=%.9f expected-uniform-variance=%.9f\n",
                chiSquared, sampleCount, target, mean, variance, expectedVariance,
                uniformMean, uniformVariance, expectedUniformVariance);
    EXPECT_LT(chiSquared, .01);
    EXPECT_NEAR(mean, target, target * .001);
    EXPECT_NEAR(uniformMean, target, target * 1e-6);
    EXPECT_NEAR(variance, expectedVariance, expectedUniformVariance * .001);
    EXPECT_NEAR(uniformVariance, expectedUniformVariance, expectedUniformVariance * 1e-6);
    EXPECT_LT(variance, uniformVariance * .5);
}
