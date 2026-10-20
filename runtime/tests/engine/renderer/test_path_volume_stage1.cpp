#include <gtest/gtest.h>
#include <cmath>
#include "../../../shaders/runtime/pathtracing/pathtracer_math.hlsli"

TEST(PathVolumeStage1, BeerLambertKnownThicknessAndZeroDensity) {
    EXPECT_FLOAT_EQ(uz::PathVolumeTransmittance(0.f, 200.f), 1.f);
    EXPECT_FLOAT_EQ(uz::PathVolumeTransmittance(.005f, 0.f), 1.f);
    EXPECT_NEAR(uz::PathVolumeTransmittance(.005f, 200.f), std::exp(-1.f), 1e-7f);
    EXPECT_NEAR(uz::PathVolumeTransmittance(.002f, 75.f)
              * uz::PathVolumeTransmittance(.002f, 125.f),
                uz::PathVolumeTransmittance(.002f, 200.f), 1e-7f);
    EXPECT_GT(uz::PathVolumeDistance(0.f, .5f), 1e20f);
}

TEST(PathVolumeStage1, ExponentialPDFIntegratesToCollisionProbability) {
    constexpr int n = 10000;
    constexpr float sigma = .005f, distance = 200.f;
    double integral = 0;
    for (int i = 0; i < n; ++i)
        integral += uz::PathVolumeDistancePDF(sigma, (float(i)+.5f)*distance/n)*distance/n;
    EXPECT_NEAR(integral, 1.0-std::exp(-double(sigma)*distance), 1e-7);
    for (float u : {.01f, .2f, .5f, .9f, .999f}) {
        float t = uz::PathVolumeDistance(sigma,u);
        EXPECT_NEAR(1.f-uz::PathVolumeTransmittance(sigma,t), u, 1e-6f);
    }
}

TEST(PathVolumeStage1, AnalogSurvivalAndAlbedoEstimatorConverge) {
    constexpr float sigmaA = .002f, sigmaS = .003f, distance = 200.f;
    double lastError = 1;
    for (int n : {64, 1024, 65536}) {
        double survival = 0, scattering = 0;
        for (int i = 0; i < n; ++i) {
            float t = uz::PathVolumeDistance(sigmaA+sigmaS,(float(i)+.5f)/n);
            if (t >= distance) survival += 1.0/n;
            else scattering += double(sigmaS/(sigmaA+sigmaS))/n;
        }
        double tr = std::exp(-double(sigmaA+sigmaS)*distance);
        double error = std::abs(survival-tr);
        EXPECT_LE(error, 1.0/n);
        EXPECT_LT(error,lastError);
        EXPECT_NEAR(scattering,(1.0-tr)*sigmaS/(sigmaA+sigmaS),1.0/n);
        lastError = error;
    }
}

TEST(PathVolumeStage1, HenyeyGreensteinNormalizesAndIsotropicIsUniform) {
    constexpr double pi = 3.141592653589793;
    constexpr int n = 100000;
    for (float g : {-.9f,-.5f,0.f,.00001f,.5f,.9f}) {
        double integral = 0, meanCosine = 0;
        for (int i = 0; i < n; ++i) {
            float cosine = -1.f+2.f*(float(i)+.5f)/n;
            integral += uz::PathVolumePhase(cosine,g)*4*pi/n;
            meanCosine += uz::PathVolumePhaseCosine((float(i)+.5f)/n,g)/n;
        }
        EXPECT_NEAR(integral,1.0,2e-5);
        EXPECT_NEAR(meanCosine,g,2e-6);
    }
    for (float cosine : {-1.f,0.f,1.f})
        EXPECT_NEAR(uz::PathVolumePhase(cosine,0.f),1.0/(4*pi),1e-8);
}

TEST(PathVolumeStage1, SingleScatteringMatchesAnalyticBacklitChord) {
    constexpr float sigmaT = .005f, sigmaS = .003f, distance = 200.f;
    constexpr int n = 65536;
    double estimate = 0;
    for (int i = 0; i < n; ++i) {
        float t = uz::PathVolumeDistance(sigmaT,(float(i)+.5f)/n);
        if (t < distance)
            estimate += (sigmaS/sigmaT)*uz::PathVolumeTransmittance(sigmaT,t)
                * uz::PathVolumePhase(-1.f,0.f)*10.0/n;
    }
    double expected = sigmaS*10.0/(4*3.141592653589793)
        * (1-std::exp(-2.0*sigmaT*distance))/(2.0*sigmaT);
    EXPECT_NEAR(estimate,expected,1e-5);
}
