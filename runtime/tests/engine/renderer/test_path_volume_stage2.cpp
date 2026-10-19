#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include "../../../shaders/runtime/pathtracing/pathtracer_volume.hlsli"

TEST(PathVolumeStage2, MajorantBoundsDensityAndAcceptance) {
    for (float variation : {0.f, .25f, .8f, 1.f}) {
        for (int i = 0; i <= 1000; ++i) {
            float q = -1.f+2.f*i/1000.f;
            float probability = uz::PathVolumeDensity(q,variation)/(1.f+variation);
            EXPECT_GE(probability,0.f);
            EXPECT_LE(probability,1.f);
            EXPECT_NEAR(probability,(1.f+variation*q)/(1.f+variation),1e-7f);
        }
    }
    // Direct acceptance frequency at a known local sigmaT/majorant.
    uint32_t state = 17;
    int accepted = 0;
    constexpr int n = 262144;
    for (int i = 0; i < n; ++i)
        accepted += uz::PathVolumeRandom(state) < .25f;
    EXPECT_NEAR(double(accepted)/n,.25,.003);
    std::printf("Stage2 acceptance: measured=%.10f analytic=0.25 n=%d\n",
                double(accepted)/n,n);
}

TEST(PathVolumeStage2, DeltaAndRatioTrackingMatchAnalyticLinearProfile) {
    constexpr int n = 524288;
    constexpr float sigma = .005f, variation = .8f, distance = 200.f;
    // q runs from -.8 to +.4; the mean density is .84, tau=.84.
    constexpr float q0 = -.8f, dq = .006f;
    double survival = 0, ratio = 0, albedo = 0;
    for (int i = 0; i < n; ++i) {
        uint32_t state = uz::PathVolumeHash(uint32_t(i));
        float u = uz::PathVolumeRandom(state);
        bool escaped = uz::PathVolumeDelta(sigma,variation,q0,dq,distance,u,state) >= distance;
        survival += escaped;
        albedo += escaped ? 0 : .6;
        ratio += uz::PathVolumeRatio(sigma,variation,q0,dq,distance,
                                    uz::PathVolumeHash(uint32_t(i)^0xa511e9b3u));
    }
    double expected = std::exp(-double(sigma)*distance
        *(1.0+variation*(q0+.5*dq*distance)));
    EXPECT_NEAR(survival/n,expected,.0025);
    EXPECT_NEAR(ratio/n,expected,.0025);
    EXPECT_NEAR(albedo/n,.6*(1-expected),.0015);
    std::printf("Stage2 linear slab: delta=%.10f ratio=%.10f analytic=%.10f n=%d\n",
                survival/n,ratio/n,expected,n);
}

TEST(PathVolumeStage2, HomogeneousLimitIsExactlyStageOne) {
    for (float sigma : {0.f,.001f,.005f,.05f}) {
        for (float u : {0.f,.01f,.2f,.5f,.9f,.999f}) {
            EXPECT_FLOAT_EQ(uz::PathVolumeDelta(sigma,0.f,-.8f,.006f,200.f,u,17),
                            uz::PathVolumeDistance(sigma,u));
            EXPECT_FLOAT_EQ(uz::PathVolumeRatio(sigma,0.f,-.8f,.006f,200.f,17),
                            uz::PathVolumeTransmittance(sigma,200.f));
        }
    }
}

TEST(PathVolumeStage2, ZeroDensityAndEmptyIntervalsAreNoOps) {
    EXPECT_FLOAT_EQ(uz::PathVolumeRatio(0.f,1.f,-1.f,.01f,200.f,1),1.f);
    EXPECT_FLOAT_EQ(uz::PathVolumeRatio(.005f,1.f,0.f,.01f,0.f,1),1.f);
    EXPECT_GT(uz::PathVolumeDelta(0.f,1.f,-1.f,.01f,200.f,.5f,1),1e20f);
}

TEST(PathVolumeStage2, RatioTrackingConvergesWithoutEnergyDrift) {
    double sum = 0;
    double errors[3]{};
    int checkpoint = 0;
    double expected = std::exp(-.84);
    for (int i = 0; i < 1048576; ++i) {
        sum += uz::PathVolumeRatio(.005f,.8f,-.8f,.006f,200.f,
                                  uz::PathVolumeHash(uint32_t(i)^0xabc98388u));
        if (i+1 == 1024 || i+1 == 32768 || i+1 == 1048576) {
            errors[checkpoint++] = std::abs(sum/(i+1)-expected);
            std::printf("Stage2 convergence: n=%d mean=%.10f analytic=%.10f error=%.10f\n",
                        i+1,sum/(i+1),expected,errors[checkpoint-1]);
        }
    }
    EXPECT_LT(errors[2],.0015);
    EXPECT_LT(errors[2],errors[0]);
}
