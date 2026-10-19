#include <gtest/gtest.h>
#include "../../../shaders/runtime/pathtracing/layered_bsdf.hlsli"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include "pathtracer_furnace_test.h"

TEST(LayeredBSDFStage2, BeerLambertCoatPathViaSnell) {
    for (float ior : {1.f,1.33f,1.5f,3.f})
        for (float v : {.01f,.1f,.5f,1.f})
            for (float l : {.01f,.1f,.5f,1.f}) {
                const double cv = std::cos(std::asin(std::sqrt(1-double(v)*v)/ior));
                const double cl = std::cos(std::asin(std::sqrt(1-double(l)*l)/ior));
                const double expected = .5*(1/cv+1/cl);
                const float path = uz::PathCoatPathLength(1.f,v,l,ior);
                EXPECT_NEAR(path,expected,expected*1e-4);
                EXPECT_FLOAT_EQ(path,uz::PathCoatPathLength(1.f,l,v,ior));
                EXPECT_FLOAT_EQ(uz::PathCoatPathLength(2.f,v,l,ior),2*path);
                for (float sigma : {0.f,.1f,.7f,1.4f})
                    EXPECT_NEAR(uz::PathCoatAbsorption(sigma,path),
                                std::exp(-sigma*expected),2e-6);
            }
    const float path = uz::PathCoatPathLength(2.f,1.f,.5f,1.5f);
    EXPECT_NEAR(path,2.224744871,3e-7);
    std::printf("COAT_ABSORPTION Snell path=%.9f tint=(%.9f,%.9f,%.9f)\n",path,
        uz::PathCoatAbsorption(.1f,path),uz::PathCoatAbsorption(.7f,path),
        uz::PathCoatAbsorption(1.4f,path));
}

TEST(LayeredBSDFStage2, ZeroAbsorptionIsBitExactStage1) {
    for (float thickness : {0.f,1.f,1e6f})
        for (float v : {0.f,.1f,.5f,1.f})
            for (float ior : {1.f,1.5f,3.f}) {
                const float path = uz::PathCoatPathLength(thickness,v,.5f,ior);
                EXPECT_FLOAT_EQ(uz::PathCoatAbsorption(0.f,path),1.f);
                for (float base : {0.f,.283493f,1.2837f,12345.25f}) {
                    const float t = uz::PathCoatTransmission(1.f,v,.5f,ior);
                    const float coat = uz::PathCoatReflection(1.f,.5f,ior,v,.5f,.7f,.8f);
                    const float stage1 = uz::PathCoatCompose(base,coat,t);
                    const float stage2 = uz::PathCoatCompose(base,coat,
                        t*uz::PathCoatAbsorption(0.f,path));
                    EXPECT_EQ(std::bit_cast<uint32_t>(stage1),std::bit_cast<uint32_t>(stage2));
                }
            }
}

TEST(LayeredBSDFStage2, AbsorptionOnlyRemovesTransmittedEnergy) {
    for (float ior : {1.f,1.33f,1.5f,3.f})
        for (float thickness : {0.f,.1f,1.f,10.f,1e6f})
            for (float sigma : {0.f,.1f,1.f,100.f,1e6f}) {
                const float path = uz::PathCoatPathLength(thickness,0.f,.5f,ior);
                const float a = uz::PathCoatAbsorption(sigma,path);
                EXPECT_TRUE(std::isfinite(a));
                EXPECT_GE(a,0.f);
                EXPECT_LE(a,1.f);
                EXPECT_LE(uz::PathCoatAbsorption(sigma,2*path),a);
                const float transmission = uz::PathCoatTransmission(1.f,.1f,.5f,ior);
                EXPECT_LE(uz::PathCoatCompose(1.2837f,.04f,transmission*a),
                          uz::PathCoatCompose(1.2837f,.04f,transmission));
            }
}

TEST(LayeredBSDFStage2, AbsorbingWhiteFurnaceRelativeToEnergyCorrectBase) {
    constexpr double pi = 3.141592653589793;
    constexpr int radial = 256, angular = 512;
    double maxDelta = -1e9, maxExcess = -1e9, maxAbsorbedMinusClear = -1e9;
    int cases = 0;
    for (float nv : {1.f,.5f,.1f,.01f})
        for (float baseR : {.3f,.8f,1.f})
            for (float coatR : {.25f,.5f,1.f})
                for (float metal : {0.f,1.f}) {
                    double baseIntegral = 0, clearIntegral = 0, absorbedIntegral = 0;
                    const double vx = std::sqrt(1-double(nv)*nv);
                    const double alpha = std::max(double(baseR)*baseR,.0064);
                    const double a2 = alpha*alpha;
                    const double diffuseEnergy = path_furnace::DiffuseEnergy(
                        nv,baseR,.04*(1-metal)+metal);
                    for (int i = 0; i < radial; ++i) {
                        const double nl = std::sqrt((i+.5)/radial);
                        const double tangent = std::sqrt(1-nl*nl);
                        const float attenuation = uz::PathCoatAbsorption(.7f,
                            uz::PathCoatPathLength(2.f,nv,float(nl),1.5f));
                        const float t = uz::PathCoatTransmission(1.f,nv,float(nl),1.5f);
                        for (int j = 0; j < angular; ++j) {
                            const double phi = 2*pi*(j+.5)/angular;
                            const double hx = vx+tangent*std::cos(phi);
                            const double hy = tangent*std::sin(phi), hz = nv+nl;
                            const double length = std::sqrt(hx*hx+hy*hy+hz*hz);
                            const double nh = hz/length, vh = (vx*hx+nv*hz)/length;
                            const double f0 = .04*(1-metal)+metal;
                            const double f = f0+(1-f0)*std::pow(1-vh,5);
                            const double d = (nh*a2-nh)*nh+1;
                            const double visibility = .5/std::max(
                                nl*std::sqrt(nv*nv*(1-a2)+a2)
                                + nv*std::sqrt(nl*nl*(1-a2)+a2),1e-7);
                            // Same energy-correct base equations as stage 1.
                            const float base = float((diffuseEnergy*(1-metal)/pi
                                + a2/(pi*d*d)*visibility*f)*nl);
                            const float coat = uz::PathCoatReflection(1.f,coatR,1.5f,
                                nv,float(nl),float(nh),float(vh));
                            const float clear = uz::PathCoatCompose(base,coat,t);
                            const float absorbed = uz::PathCoatCompose(base,coat,t*attenuation);
                            ASSERT_LE(absorbed,clear);
                            const double factor = pi/nl/(radial*angular);
                            baseIntegral += base*factor;
                            clearIntegral += clear*factor;
                            absorbedIntegral += absorbed*factor;
                        }
                    }
                    const double fresnel = uz::PathCoatFresnel(nv,1.5f);
                    const double delta = absorbedIntegral-baseIntegral;
                    EXPECT_LE(absorbedIntegral,clearIntegral);
                    EXPECT_LE(delta,fresnel+2e-3);
                    maxDelta = std::max(maxDelta,delta);
                    maxExcess = std::max(maxExcess,delta-fresnel);
                    maxAbsorbedMinusClear = std::max(maxAbsorbedMinusClear,
                        absorbedIntegral-clearIntegral);
                    ++cases;
                }
    std::printf("COAT_ABSORPTION_FURNACE cases=%d max_delta=%.9f max_delta_minus_F=%.9f max_absorbed_minus_clear=%.9f\n",
        cases,maxDelta,maxExcess,maxAbsorbedMinusClear);
}
