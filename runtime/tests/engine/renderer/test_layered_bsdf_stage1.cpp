#include <gtest/gtest.h>
#include "../../../shaders/runtime/pathtracing/pathtracer_math.hlsli"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "pathtracer_furnace_test.h"

TEST(PathTracerEnergy, WhiteFurnaceConservesTotalDirectionalAlbedo) {
    double maxAlbedo = 0, maxDielectric = 0;
    int cases = 0;
    for (int step = 1; step <= 9; ++step)
        for (double nv : {.0001,.0005,.0009,.001,.01,.025,.05,.1,.2,.3,.5,.7,.9,1.0})
            for (double metallic : {0.0,.5,1.0}) {
                const double roughness = step/10.0;
                const double f0 = .04*(1-metallic)+metallic;
                const double diffuse = (1-metallic)
                    * path_furnace::DiffuseEnergy(nv,roughness,f0);
                const double specular = path_furnace::SpecularIntegral(nv,roughness,f0);
                const double total = diffuse+specular;
                EXPECT_LE(total,1.0) << "roughness=" << roughness << " NoV=" << nv
                    << " metallic=" << metallic;
                maxAlbedo = std::max(maxAlbedo,total);
                if (metallic == 0) {
                    maxDielectric = std::max(maxDielectric,total);
                    if (nv == 1) {
                        EXPECT_GT(total,.99) << "roughness=" << roughness;
                        std::printf("BASE_FURNACE normal roughness=%.1f total=%.9f\n",
                            roughness,total);
                    }
                    if (step == 3 && nv == .1)
                        std::printf("BASE_FURNACE D7 total=%.9f diffuse=%.9f specular=%.9f\n",
                            total,diffuse,specular);
                }
                ++cases;
            }
    std::printf("BASE_FURNACE cases=%d max_total=%.9f max_dielectric=%.9f\n",
        cases,maxAlbedo,maxDielectric);
}

TEST(LayeredBSDFStage1, CoatFresnelNormalGrazingAndMatchedIOR) {
    EXPECT_NEAR(uz::PathCoatFresnel(1.f,1.5f),.04f,1e-7f);
    EXPECT_FLOAT_EQ(uz::PathCoatFresnel(0.f,1.5f),1.f);
    EXPECT_FLOAT_EQ(uz::PathCoatFresnel(0.f,1.f),0.f);
    EXPECT_FLOAT_EQ(uz::PathCoatFresnel(.5f,1.f),0.f);
    EXPECT_NEAR(uz::PathCoatFresnel(1.f,1.33f),.0200593f,1e-6f);
    float previous = 1.f;
    for (int i = 0; i <= 100; ++i) {
        float f = uz::PathCoatFresnel(float(i)/100.f,1.5f);
        EXPECT_LE(f,previous);
        EXPECT_GE(f,.04f);
        previous = f;
    }
}

TEST(LayeredBSDFStage1, ZeroCoatIsExactBaseAndPDFPassThrough) {
    for (float base : {0.f,.04f,.783493f,1.2837f,12345.25f}) {
        float t = uz::PathCoatTransmission(0.f,.1f,.7f,1.5f);
        float c = uz::PathCoatReflection(0.f,.1f,1.5f,.1f,.7f,.4f,.3f);
        EXPECT_FLOAT_EQ(t,1.f);
        EXPECT_FLOAT_EQ(c,0.f);
        EXPECT_FLOAT_EQ(uz::PathCoatCompose(base,c,t),base);
        EXPECT_FLOAT_EQ(uz::PathCoatMixturePDF(0.f,17.f,base),base);
    }
}

TEST(LayeredBSDFStage1, TwoInterfaceCompositionNeverAmplifiesBase) {
    for (float w : {.1f,.5f,1.f})
        for (float v : {0.f,.1f,.5f,1.f})
            for (float l : {0.f,.1f,.5f,1.f}) {
                const float fv = w*uz::PathCoatFresnel(v,1.5f);
                const float fl = w*uz::PathCoatFresnel(l,1.5f);
                const float t = uz::PathCoatTransmission(w,v,l,1.5f);
                EXPECT_FLOAT_EQ(t,(1-fv)*(1-fl));
                EXPECT_GE(t,0.f);
                EXPECT_LE(t,1-fv);
                for (float base : {0.f,.5f,1.f,1.284f}) {
                    float result = uz::PathCoatCompose(base,fv,t);
                    EXPECT_LE(result-base,fv+2e-7f);
                    if (base <= 1.f) EXPECT_LE(result,1.f+1e-7f);
                }
            }
}

TEST(LayeredBSDFStage1, LobeSelectionUsesBothDensities) {
    for (float p : {0.f,.04f,.25f,.5f,1.f}) {
        EXPECT_FLOAT_EQ(uz::PathCoatMixturePDF(p,.7f,.3f),p*.7f+(1-p)*.3f);
        int selected = 0;
        for (int i = 0; i < 10000; ++i)
            selected += (float(i)+.5f)/10000.f < p;
        EXPECT_NEAR(float(selected)/10000.f,p,1e-6f);
        // Disjoint two-bin normalized lobes remain a normalized mixture.
        EXPECT_NEAR(uz::PathCoatMixturePDF(p,1.f,0.f)
                  + uz::PathCoatMixturePDF(p,0.f,1.f),1.f,1e-7f);
    }
}

TEST(LayeredBSDFStage1, GGXCoatMatchesEngineNormalIncidence) {
    const float roughness = .5f;
    const float alpha = roughness*roughness;
    const float expected = .04f/(4.f*3.14159265359f*alpha*alpha);
    EXPECT_NEAR(uz::PathCoatReflection(1.f,roughness,1.5f,1.f,1.f,1.f,1.f),
                expected,1e-6f);
    EXPECT_FLOAT_EQ(uz::PathCoatReflection(1.f,roughness,1.5f,1.f,0.f,1.f,1.f),0.f);
}

TEST(LayeredBSDFStage1, WhiteFurnaceRelativeEnergyBound) {
    // Deterministic cosine-hemisphere quadrature: no RNG or timing dependence.
    // Same directional-energy coupling as EvaluatePBR, including its baked LUT.
    constexpr double pi = 3.141592653589793;
    constexpr int radial = 256, angular = 512;
    double maxDelta = -1e9, maxExcess = -1e9, maxAlbedo = 0;
    int cases = 0;
    for (float nv : {1.f,.5f,.1f,.01f})
        for (float baseRoughness : {.3f,.8f,1.f})
            for (float coatRoughness : {.25f,.5f,1.f})
                for (float metallic : {0.f,1.f})
                    for (float weight : {.25f,1.f}) {
                        double baseIntegral = 0, layeredIntegral = 0, coatIntegral = 0;
                        const double vx = std::sqrt(1-double(nv)*nv);
                        const double alpha = std::max(double(baseRoughness)*baseRoughness,.0064);
                        const double a2 = alpha*alpha;
                        const double diffuseEnergy = path_furnace::DiffuseEnergy(
                            nv,baseRoughness,.04*(1-metallic)+metallic);
                        for (int i = 0; i < radial; ++i) {
                            const double nl = std::sqrt((i+.5)/radial);
                            const double tangent = std::sqrt(1-nl*nl);
                            for (int j = 0; j < angular; ++j) {
                                const double phi = 2*pi*(j+.5)/angular;
                                const double hx = vx+tangent*std::cos(phi);
                                const double hy = tangent*std::sin(phi);
                                const double hz = nv+nl;
                                const double length = std::sqrt(hx*hx+hy*hy+hz*hz);
                                const double nh = hz/length, vh = (vx*hx+nv*hz)/length;
                                const double f0 = .04*(1-metallic)+metallic;
                                const double f = f0+(1-f0)*std::pow(1-vh,5);
                                const double d = (nh*a2-nh)*nh+1;
                                const double visibility = .5/std::max(
                                    nl*std::sqrt(nv*nv*(1-a2)+a2)
                                  + nv*std::sqrt(nl*nl*(1-a2)+a2),1e-7);
                                const float base = float((diffuseEnergy*(1-metallic)/pi
                                    + a2/(pi*d*d)*visibility*f)*nl);
                                const float coat = uz::PathCoatReflection(weight,coatRoughness,1.5f,
                                    nv,float(nl),float(nh),float(vh));
                                const float t = uz::PathCoatTransmission(weight,nv,float(nl),1.5f);
                                const double factor = pi/nl/(radial*angular);
                                baseIntegral += base*factor;
                                coatIntegral += coat*factor;
                                layeredIntegral += uz::PathCoatCompose(base,coat,t)*factor;
                            }
                        }
                        const double fresnel = weight*uz::PathCoatFresnel(nv,1.5f);
                        const double delta = layeredIntegral-baseIntegral;
                        EXPECT_LE(delta,coatIntegral+2e-6);
                        EXPECT_LE(delta,fresnel+2e-3)
                            << "nv=" << nv << " baseR=" << baseRoughness
                            << " coatR=" << coatRoughness << " metal=" << metallic;
                        maxDelta = std::max(maxDelta,delta);
                        maxExcess = std::max(maxExcess,delta-fresnel);
                        maxAlbedo = std::max(maxAlbedo,layeredIntegral);
                        if (nv == .1f && baseRoughness == .3f && coatRoughness == .5f
                            && metallic == 0.f && weight == 1.f)
                            std::printf("LAYERED_FURNACE grazing base=%.9f layered=%.9f delta=%.9f F=%.9f coat=%.9f\n",
                                baseIntegral,layeredIntegral,delta,fresnel,coatIntegral);
                        ++cases;
                    }
    std::printf("LAYERED_FURNACE cases=%d max_delta=%.9f max_delta_minus_F=%.9f max_layered_albedo=%.9f\n",
        cases,maxDelta,maxExcess,maxAlbedo);
}
