#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include "../../../shaders/runtime/pathtracing/hair_bcsdf.hlsli"

namespace {
constexpr double pi = 3.141592653589793;
constexpr float eta = 1.55f;
constexpr float alpha = 0.03490658504f;

// All material inputs are explicit: betaM, betaN, eta, alpha, h and RGB sigmaA.
// The HLSL vector wrappers are excluded in C++; only their scalar assembly is
// reproduced here. All distributions, shifts, absorption and Ap use the shader.
struct Attenuation {
    float gammaO, gammaT;
    float ap[3][3];
    float probability[3];
};

Attenuation Attenuate(float thetaO, float h, float ior,
                     const std::array<float,3>& sigmaA) {
    Attenuation result{};
    const float sinO = std::sin(thetaO), cosO = std::cos(thetaO);
    result.gammaO = std::asin(h);
    const float etaPrime = std::sqrt(ior*ior-sinO*sinO)/std::max(cosO,1e-6f);
    result.gammaT = std::asin(h/etaPrime);
    const float cosThetaT = std::sqrt(1.f-sinO*sinO/(ior*ior));
    const float f = uz::HairFresnel(cosO*std::cos(result.gammaO),ior);
    constexpr float luminance[3] = {.2126f,.7152f,.0722f};
    float total = 0;
    for (int p = 0; p < 3; ++p) {
        for (int c = 0; c < 3; ++c) {
            const float t = std::exp(-sigmaA[c]*2.f*std::cos(result.gammaT)/cosThetaT);
            result.ap[p][c] = uz::HairAp(p,f,t);
            result.probability[p] += luminance[c]*result.ap[p][c];
        }
        total += result.probability[p];
    }
    for (float& probability : result.probability)
        probability /= std::max(total,1e-20f);
    return result;
}

void RotateOutgoing(float thetaO, int p, float tilt, float& so, float& co) {
    const float shift = uz::HairShift(p,tilt);
    so = std::sin(thetaO)*std::cos(shift)-std::cos(thetaO)*std::sin(shift);
    co = std::abs(std::cos(thetaO)*std::cos(shift)+std::sin(thetaO)*std::sin(shift));
}

double IntegrateM(float so, float co, float variance, double lo, double hi, int n) {
    double sum = 0;
    for (int i = 0; i < n; ++i) {
        const double theta = lo+(hi-lo)*(i+.5)/n;
        sum += uz::HairMp(float(std::sin(theta)),float(std::cos(theta)),so,co,variance)
             * std::cos(theta);
    }
    return sum*(hi-lo)/n;
}

double IntegrateN(int p, float scale, float gammaO, float gammaT,
                  double lo, double hi, int n) {
    double sum = 0;
    for (int i = 0; i < n; ++i)
        sum += uz::HairNp(float(lo+(hi-lo)*(i+.5)/n),p,scale,gammaO,gammaT);
    return sum*(hi-lo)/n;
}

float WrapPhi(float phi) {
    return float(phi-2*pi*std::floor((phi+pi)/(2*pi)));
}
}

TEST(HairBCSDFStage1, LongitudinalMpNormalizesInSolidAngle) {
    // d omega = d(sin theta) d phi = cos(theta) d theta d phi.
    // Theta-space quadrature resolves narrow lobes even at a fiber-axis pole.
    for (float betaM : {.05f,.15f,.5f,1.f})
        for (float thetaO : {-1.55f,-.8f,0.f,.8f,1.55f})
            for (int p = 0; p < 3; ++p) {
                SCOPED_TRACE(::testing::Message() << betaM << " " << thetaO << " " << p);
                float so,co;
                RotateOutgoing(thetaO,p,alpha,so,co);
                const double mass = IntegrateM(so,co,uz::HairVariance(betaM,p),-pi/2,pi/2,4096);
                EXPECT_NEAR(mass,1.,2e-3);
            }
}

TEST(HairBCSDFStage1, ConeAndCuticleTiltPeakConvention) {
    // R=-alpha, TT=alpha/2, TRT=-3alpha/2. Outgoing theta rotates
    // by MINUS this shift, so the narrow incoming peak is -thetaO+shift.
    const float expectedShift[3] = {-alpha,.5f*alpha,-1.5f*alpha};
    for (float tilt : {0.f,alpha})
        for (float thetaO : {-.8f,0.f,.8f})
            for (int p = 0; p < 3; ++p) {
                EXPECT_FLOAT_EQ(uz::HairShift(p,tilt),
                    tilt == 0.f ? 0.f : expectedShift[p]);
                float so,co;
                RotateOutgoing(thetaO,p,tilt,so,co);
                const float expected = -thetaO+(tilt == 0.f ? 0.f : expectedShift[p]);
                float peak = 0, maximum = -1;
                for (int i = 0; i <= 2000; ++i) {
                    const float theta = expected-.02f+float(i)*.00002f;
                    const float value = uz::HairMp(std::sin(theta),std::cos(theta),
                        so,co,uz::HairVariance(.05f,p));
                    if (value > maximum) {
                        maximum = value;
                        peak = theta;
                    }
                }
                // Finite-width Mp has a small Jacobian-dependent mode offset.
                EXPECT_NEAR(peak,expected,.004f) << thetaO << " p=" << p;
                EXPECT_GT(maximum,0.f);
            }
}

TEST(HairBCSDFStage1, TrimmedLogisticNormalizesAndCentersR_TT_TRT) {
    for (float betaN : {.05f,.2f,.5f,1.f})
        for (float h : {-.95f,0.f,.8f})
            for (float thetaO : {0.f,1.2f}) {
                const auto a = Attenuate(thetaO,h,eta,{{0.f,0.f,0.f}});
                const float s = uz::HairLogisticScale(betaN);
                const float centers[3] = {-2.f*a.gammaO,
                    float(pi)+2.f*a.gammaT-2.f*a.gammaO,
                    float(2*pi)+4.f*a.gammaT-2.f*a.gammaO};
                for (int p = 0; p < 3; ++p) {
                    EXPECT_NEAR(uz::HairPhi(p,a.gammaO,a.gammaT),centers[p],1e-6f);
                    EXPECT_NEAR(IntegrateN(p,s,a.gammaO,a.gammaT,-pi,pi,4096),1.,5e-4);
                    const float center = WrapPhi(centers[p]);
                    const float peak = uz::HairNp(center,p,s,a.gammaO,a.gammaT);
                    EXPECT_GT(peak,uz::HairNp(center-.1f,p,s,a.gammaO,a.gammaT));
                    EXPECT_GT(peak,uz::HairNp(center+.1f,p,s,a.gammaO,a.gammaT));
                    EXPECT_NEAR(uz::HairNp(center-.1f,p,s,a.gammaO,a.gammaT),
                        uz::HairNp(center+.1f,p,s,a.gammaO,a.gammaT),peak*2e-4f);
                    EXPECT_NEAR(uz::HairNp(center+float(2*pi),p,s,a.gammaO,a.gammaT),
                        peak,peak*2e-4f);
                }
            }
}

TEST(HairBCSDFStage1, ColorToAbsorptionIsFinitePositiveAndMonotonic) {
    for (float betaN : {.05f,.2f,.5f,1.f}) {
        EXPECT_FLOAT_EQ(uz::HairSigmaA(1.f,betaN),0.f);
        float previous = uz::HairSigmaA(0.f,betaN);
        EXPECT_GT(previous,0.f);
        EXPECT_FLOAT_EQ(previous,uz::HairSigmaA(1e-4f,betaN));
        for (float color : {1e-4f,.001f,.01f,.1f,.3f,.7f,1.f}) {
            const float value = uz::HairSigmaA(color,betaN);
            EXPECT_TRUE(std::isfinite(value));
            EXPECT_GE(value,0.f);
            EXPECT_LE(value,previous);
            if (color < 1.f) EXPECT_GT(value,0.f);
            previous = value;
        }
    }
}

TEST(HairBCSDFStage1, AttenuationR_TT_TRTNeverCreatesEnergy) {
    for (float ior : {1.33f,eta,1.8f})
        for (float h : {-.999f,-.5f,0.f,.5f,.999f})
            for (float thetaO : {-1.56f,-.8f,0.f,.8f,1.56f})
                for (float betaN : {.05f,.3f,1.f})
                    for (float color : {1.f,.5f,.01f,0.f}) {
                        const float sigma = uz::HairSigmaA(color,betaN);
                        const auto a = Attenuate(thetaO,h,ior,{{sigma,sigma,sigma}});
                        float energy = 0, probability = 0;
                        for (int p = 0; p < 3; ++p) {
                            EXPECT_TRUE(std::isfinite(a.ap[p][0]));
                            EXPECT_GE(a.ap[p][0],0.f);
                            EXPECT_LE(a.ap[p][0],1.f);
                            energy += a.ap[p][0];
                            probability += a.probability[p];
                        }
                        EXPECT_LE(energy,1.f+2e-7f);
                        EXPECT_NEAR(probability,1.f,2e-7f);
                    }
    // Independent normal-incidence reference with no absorption.
    const float f = (eta-1.f)*(eta-1.f)/((eta+1.f)*(eta+1.f));
    const auto a = Attenuate(0.f,0.f,eta,{{0.f,0.f,0.f}});
    EXPECT_NEAR(a.ap[0][0],f,1e-7f);
    EXPECT_NEAR(a.ap[1][0],(1-f)*(1-f),1e-7f);
    EXPECT_NEAR(a.ap[2][0],(1-f)*(1-f)*f,1e-7f);
}

TEST(HairBCSDFStage1, FullSphereFurnaceAndActualApMixturePDF) {
    const std::array<std::array<float,3>,3> colors = {{
        {{1.f,1.f,1.f}}, {{.7f,.2f,.05f}}, {{.02f,.005f,.0001f}}
    }};
    double maximumEnergy = 0, minimumPDF = 1e9, maximumPDF = 0;
    int cases = 0;
    for (float thetaO : {-1.3f,0.f,.9f})
        for (float betaM : {.05f,.3f,1.f}) {
            double m[3];
            for (int p = 0; p < 3; ++p) {
                float so,co;
                RotateOutgoing(thetaO,p,alpha,so,co);
                m[p] = IntegrateM(so,co,uz::HairVariance(betaM,p),-pi/2,pi/2,4096);
            }
            for (float h : {-.95f,0.f,.8f})
                for (float betaN : {.05f,.5f,1.f}) {
                    const auto geometry = Attenuate(thetaO,h,eta,{{0.f,0.f,0.f}});
                    double n[3];
                    for (int p = 0; p < 3; ++p)
                        n[p] = IntegrateN(p,uz::HairLogisticScale(betaN),
                            geometry.gammaO,geometry.gammaT,-pi,pi,4096);
                    for (const auto& color : colors) {
                        SCOPED_TRACE(::testing::Message() << thetaO << " " << betaM
                            << " " << h << " " << betaN << " color=" << color[0]);
                        std::array<float,3> sigmaA;
                        for (int c = 0; c < 3; ++c)
                            sigmaA[c] = uz::HairSigmaA(color[c],betaN);
                        const auto a = Attenuate(thetaO,h,eta,sigmaA);
                        double energy[3] = {}, pdf = 0;
                        // Exact factoring of the full rectangular sphere quadrature:
                        // sum_ij Mp(theta_i) Np(phi_j) cos(theta_i) dtheta dphi.
                        // No hemisphere restriction or surface-normal cosine.
                        for (int p = 0; p < 3; ++p) {
                            const double lobeIntegral = m[p]*n[p];
                            pdf += a.probability[p]*lobeIntegral;
                            for (int c = 0; c < 3; ++c)
                                energy[c] += a.ap[p][c]*lobeIntegral;
                        }
                        for (int c = 0; c < 3; ++c) {
                            const double expected = a.ap[0][c]+a.ap[1][c]+a.ap[2][c];
                            EXPECT_NEAR(energy[c],expected,2e-3);
                            EXPECT_GE(energy[c],0.);
                            EXPECT_LE(energy[c],1.002);
                            maximumEnergy = std::max(maximumEnergy,energy[c]);
                        }
                        EXPECT_NEAR(pdf,1.,2e-3);
                        minimumPDF = std::min(minimumPDF,pdf);
                        maximumPDF = std::max(maximumPDF,pdf);
                        // Same stratified selector as the shader: weights are
                        // luminance(Ap)/sum(luminance(Ap)), not uniform thirds.
                        int counts[3] = {};
                        constexpr int selections = 4096;
                        for (int i = 0; i < selections; ++i) {
                            const float u = (float(i)+.5f)/selections;
                            ++counts[u < a.probability[0] ? 0 :
                                (u < a.probability[0]+a.probability[1] ? 1 : 2)];
                        }
                        for (int p = 0; p < 3; ++p)
                            EXPECT_NEAR(double(counts[p])/selections,a.probability[p],
                                1./selections+2e-7);
                        ++cases;
                    }
                }
        }
    std::printf("HAIR_FURNACE cases=%d max_energy=%.12f\n",cases,maximumEnergy);
    std::printf("HAIR_PDF cases=%d integral_min=%.12f integral_max=%.12f\n",
        cases,minimumPDF,maximumPDF);
}

TEST(HairBCSDFStage1, SampledLongitudinalDirectionsMatchMpBinMasses) {
    constexpr int bins = 10, side = 128;
    for (float betaM : {.05f,.3f,1.f})
        for (float thetaO : {-.8f,0.f,.8f})
            for (int p = 0; p < 3; ++p) {
                SCOPED_TRACE(::testing::Message() << betaM << " " << thetaO << " " << p);
                float so,co;
                RotateOutgoing(thetaO,p,alpha,so,co);
                const float v = uz::HairVariance(betaM,p);
                int counts[bins] = {};
                bool finiteAndBounded = true;
                for (int i = 0; i < side; ++i)
                    for (int j = 0; j < side; ++j) {
                        const float sinI = uz::HairSampleSinTheta(so,co,v,
                            (float(i)+.5f)/side,(float(j)+.5f)/side);
                        finiteAndBounded &= std::isfinite(sinI) && sinI >= -1.f && sinI <= 1.f;
                        if (!std::isfinite(sinI)) continue;
                        const int bin = std::max(0,std::min(bins-1,int((sinI+1.f)*.5f*bins)));
                        ++counts[bin];
                    }
                EXPECT_TRUE(finiteAndBounded);
                for (int b = 0; b < bins; ++b) {
                    const double lo = std::asin(-1.+2.*b/bins);
                    const double hi = std::asin(-1.+2.*(b+1)/bins);
                    const double mass = IntegrateM(so,co,v,lo,hi,1024);
                    EXPECT_NEAR(double(counts[b])/(side*side),mass,.008) << "bin=" << b;
                }
            }
}

TEST(HairBCSDFStage1, InverseCDFSampledAzimuthDirectionsMatchNpBinMasses) {
    constexpr int bins = 12, samples = 8192;
    for (float betaN : {.05f,.3f,1.f})
        for (float h : {-.8f,0.f,.9f})
            for (int p = 0; p < 3; ++p) {
                SCOPED_TRACE(::testing::Message() << betaN << " " << h << " " << p);
                const auto a = Attenuate(.7f,h,eta,{{0.f,0.f,0.f}});
                const float s = uz::HairLogisticScale(betaN);
                int counts[bins] = {};
                bool finiteAndBounded = true;
                for (int i = 0; i < samples; ++i) {
                    const float residual = uz::HairLogisticSample((float(i)+.5f)/samples,s);
                    finiteAndBounded &= std::isfinite(residual)
                        && residual >= -pi && residual <= pi;
                    if (!std::isfinite(residual)) continue;
                    const float phi = WrapPhi(uz::HairPhi(p,a.gammaO,a.gammaT)+residual);
                    const int bin = std::max(0,std::min(bins-1,int((phi+pi)/(2*pi)*bins)));
                    ++counts[bin];
                }
                EXPECT_TRUE(finiteAndBounded);
                for (int b = 0; b < bins; ++b) {
                    const double lo = -pi+2*pi*b/bins, hi = -pi+2*pi*(b+1)/bins;
                    const double mass = IntegrateN(p,s,a.gammaO,a.gammaT,lo,hi,512);
                    EXPECT_NEAR(double(counts[b])/samples,mass,6e-4) << "bin=" << b;
                }
            }
}
