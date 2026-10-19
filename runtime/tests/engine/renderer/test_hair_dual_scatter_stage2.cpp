#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include "../../../shaders/runtime/pathtracing/hair_dual_scatter.hlsli"

namespace {
constexpr float pi = 3.14159265359f;
constexpr float eta = 1.55f, alpha = 0.03490658504f;
}

TEST(HairDualScatterStage2, ForwardLobeMassMatchesIndependentQuadrature) {
    for (float beta : {.05f,.2f,.5f,1.f})
        for (float h : {-.99f,-.6f,0.f,.6f,.99f})
            for (int p = 0; p < 3; ++p) {
                float go = std::asin(h), gt = std::asin(h/eta);
                float scale = uz::HairLogisticScale(beta);
                double forward = 0, backward = 0;
                for (int i = 0; i < 8192; ++i) {
                    float phi = -pi+2*pi*(float(i)+.5f)/8192;
                    double mass = uz::HairNp(phi,p,scale,go,gt)*(2*pi/8192);
                    (std::cos(phi)<0 ? forward : backward) += mass;
                }
                EXPECT_NEAR(uz::HairDualForwardMass(p,scale,go,gt),forward,3e-4);
                EXPECT_NEAR(forward+backward,1.,5e-4);
            }
}

TEST(HairDualScatterStage2, AverageForwardBackwardSumToBCSDFLobeIntegrals) {
    double maxEnergy = 0;
    for (float sinO : {-.95f,0.f,.7f})
        for (float color : {.005f,.05f,.7f,1.f})
            for (float beta : {.05f,.3f,1.f}) {
                float sigma = uz::HairSigmaA(color,beta);
                auto m = uz::HairDualIntegrals(sinO,sigma,beta,beta,eta,alpha);
                double expected = 0;
                float cosO = std::sqrt(1-sinO*sinO);
                float ep = std::sqrt(eta*eta-sinO*sinO)/cosO;
                for (int i = 0; i < 16; ++i) {
                    float h = -1+(float(i)+.5f)/8;
                    float gt = std::asin(h/ep);
                    float f = uz::HairFresnel(cosO*std::sqrt(1-h*h),eta);
                    float t = std::exp(-2*sigma*std::cos(gt)/std::sqrt(1-sinO*sinO/(eta*eta)));
                    for (int p = 0; p < 3; ++p) expected += uz::HairAp(p,f,t)/16.;
                }
                EXPECT_NEAR(m.af+m.ab,expected,3e-7);
                EXPECT_GE(m.af,0); EXPECT_GE(m.ab,0);
                EXPECT_LE(m.af+m.ab,1.f);
                EXPECT_GT(m.vf,0); EXPECT_GT(m.vb,0);
                EXPECT_TRUE(std::isfinite(m.shiftF+m.shiftB));
                maxEnergy = std::max(maxEnergy,double(m.af+m.ab));
            }
    std::printf("HAIR_DUAL_INTEGRALS max_albedo=%.12f\n",maxEnergy);
}

TEST(HairDualScatterStage2, BackscatterSeriesAndSpreadUseSamePathWeights) {
    for (float af : {.1f,.5f,.9f,.98f}) {
        float ab = (1-af)*.6f;
        double r = double(af)*af, one = 0, three = 0;
        // r/(1-r)^3 has coefficient n(n+1)/2 at r^n.
        for (int n = 1; n <= 2048; ++n) {
            one += ab*std::pow(r,n);
            three += double(ab)*ab*ab*.5*n*(n+1)*std::pow(r,n);
        }
        EXPECT_NEAR(uz::HairDualBackEnergy(af,ab),one+three,2e-6);
        uz::HairDualMoments m{af,ab,.01f,.03f,.002f,-.004f};
        double v = (one*(2*.01/(1-r)+.03)
                 + three*((2+6*r/(1-r))*.01+3*.03))/(one+three);
        EXPECT_NEAR(uz::HairDualBackVariance(m),v,2e-6);
        EXPECT_TRUE(std::isfinite(uz::HairDualBackShift(m)));
    }
}

TEST(HairDualScatterStage2, ReplacementFurnaceNeverSpendsMoreThanSingleFiber) {
    double maximum = 0;
    for (float color : {.001f,.05f,.7f,1.f})
        for (float beta : {.05f,.3f,1.f})
            for (float sinO : {-.99f,0.f,.9f})
                for (float density : {0.f,.7f,1.f}) {
                    float sigma = uz::HairSigmaA(color,beta);
                    auto m = uz::HairDualIntegrals(sinO,sigma,beta,beta,eta,alpha);
                    float back = density*uz::HairDualBackEnergy(m.af,m.ab);
                    // The shader budgets the *current near-field fiber*, not
                    // the h-averaged albedo used to compute af and ab.
                    for (float h : {-.99f,0.f,.7f,.99f}) {
                        float cosO = std::sqrt(1-sinO*sinO);
                        float ep = std::sqrt(eta*eta-sinO*sinO)/cosO;
                        float gt = std::asin(h/ep);
                        float f = uz::HairFresnel(cosO*std::sqrt(1-h*h),eta);
                        float t = std::exp(-2*sigma*std::cos(gt)
                            /std::sqrt(1-sinO*sinO/(eta*eta)));
                        float a = 0;
                        for (int p = 0; p < 3; ++p) a += uz::HairAp(p,f,t);
                        float scale = uz::HairDualBudgetScale(a,back);
                        for (int depth : {0,1,8,64}) {
                            double energy = (double(a)+back)*scale*std::pow(double(m.af),depth);
                            EXPECT_LE(energy,double(a)+1e-7);
                            EXPECT_LE(energy,1.);
                            maximum = std::max(maximum,energy);
                        }
                    }
                }
    std::printf("HAIR_DUAL_FURNACE max_energy=%.12f\n",maximum);
}

TEST(HairDualScatterStage2, OffAndNonHairNeverReplaceStageOneTransport) {
    for (int k : {1,2,4,8,16,32,64,128})
        for (int bounce = 0; bounce <= 128; ++bounce) {
            EXPECT_FALSE(uz::HairDualReplace(true,false,bounce,k));
            EXPECT_FALSE(uz::HairDualReplace(false,true,bounce,k));
            EXPECT_EQ(uz::HairDualReplace(true,true,bounce,k),bounce>=k);
        }
}

TEST(HairDualScatterStage2, PrefixTailPartitionHasNoOverlappingPathLengths) {
    // Exact geometric transport supplies an independent solvable tail.
    // Replacing, rather than adding to, a continued tail recovers its mean.
    for (double a : {.2,.8,.98})
        for (int k : {1,2,4,8,16}) {
            double prefix = 0, throughput = 1;
            for (int i = 0; i < k; ++i) {
                prefix += throughput*(1-a);
                throughput *= a;
            }
            EXPECT_NEAR(prefix+throughput,1.,1e-13);
            double closure = .9;
            EXPECT_NEAR((prefix+throughput*closure)-1,
                        std::pow(a,k)*(closure-1),1e-13);
        }
}
