#include <gtest/gtest.h>
#include "../../../shaders/runtime/pathtracing/layered_stack.hlsli"
#include "../../../shaders/runtime/pathtracing/layered_bsdf.hlsli"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>

namespace {
constexpr double pi = 3.141592653589793;
struct V3 { double x, y, z; };
V3 operator+(V3 a, V3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
V3 operator-(V3 a) { return {-a.x,-a.y,-a.z}; }
V3 operator*(V3 a, double s) { return {a.x*s,a.y*s,a.z*s}; }
double dot(V3 a, V3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
V3 norm(V3 a) { return a*(1/std::sqrt(dot(a,a))); }
uint32_t hash(uint32_t input) {
    uint32_t state = input*747796405u+2891336453u;
    uint32_t word = ((state >> ((state >> 28u)+4u))^state)*277803737u;
    return (word >> 22u)^word;
}
double random(uint32_t& state) { state = hash(state); return double(state >> 8u)/16777216; }
// Same spherical-cap visible GGX normal construction as SampleGGX_VNDF.
V3 facet(V3 v, double alpha, uint32_t& rng) {
    V3 h = norm({alpha*v.x,alpha*v.y,v.z});
    double phi = 2*pi*random(rng);
    double z = (1-random(rng))*(1+h.z)-h.z;
    double r = std::sqrt(std::max(0.0,1-z*z));
    h = h+V3{r*std::cos(phi),r*std::sin(phi),z};
    return norm({alpha*h.x,alpha*h.y,std::max(0.0,h.z)});
}
double boundary(V3& direction, double roughness, float etaI, float etaT,
                uint32_t& rng, unsigned& tir) {
    if (etaI == etaT) return 1;
    double sign = direction.z < 0 ? 1 : -1;
    V3 v = {-direction.x,-direction.y,-direction.z*sign};
    double alpha = std::max(.0064,roughness*roughness);
    V3 h = facet(v,alpha,rng);
    float vh = float(dot(v,h));
    float ct = uz::PathStackCosT(vh,etaI,etaT);
    float f = uz::PathStackFresnel(vh,etaI,etaT);
    tir += ct < 0;
    bool transmit = random(rng) >= f;
    V3 l = transmit ? -v*(etaI/etaT)+h*((etaI/etaT)*vh-ct)
                    : -v+h*(2*vh);
    double cosine = l.z*(transmit ? -1 : 1);
    direction = {l.x,l.y,l.z*sign};
    if (cosine <= 0) return 0;
    return uz::PathStackEventWeight(float(alpha),float(cosine),etaI,etaT,transmit);
}

// Numerical path-space integration of the SAME VNDF/Fresnel/Jacobian kernel.
// Lambert base reflectance is known exactly. It isolates the layer contract
// from the unchanged D7 base, whose own furnace has separate regressions.
double walk(double nv, const std::array<float,5>& ior, int depth, double roughness,
            uint32_t& rng, bool& hitBase, unsigned& tir, double baseAlbedo = .8) {
    V3 direction = {-std::sqrt(1-nv*nv),0,-nv};
    double weight = 1;
    int vertex = 0;
    while (true) {
        if (vertex == depth) {
            hitBase = true;
            weight *= baseAlbedo;
            double radius = std::sqrt(random(rng)), phi = 2*pi*random(rng);
            direction = {radius*std::cos(phi),radius*std::sin(phi),
                         std::sqrt(1-radius*radius)};
        } else {
            bool down = direction.z < 0;
            weight *= boundary(direction,roughness,ior[vertex+(down ? 0 : 1)],
                               ior[vertex+(down ? 1 : 0)],rng,tir);
        }
        if (weight <= 0) return 0;
        if (vertex == 0 && direction.z > 0) return weight;
        vertex += direction.z < 0 ? 1 : -1;
    }
}

double boundaryValue(V3 v, V3 l, double alpha, float etaI, float etaT) {
    const bool transmit = l.z < 0;
    V3 h = norm(transmit ? v*etaI+l*etaT : v+l);
    if (h.z < 0) h = -h;
    double vh = dot(v,h), lh = dot(l,h);
    if (vh <= 0 || (transmit ? lh >= 0 : lh <= 0)) return 0;
    double f = uz::PathStackFresnel(float(vh),etaI,etaT);
    double ph = uz::PathStackD(float(alpha),float(h.z))
              * uz::PathStackG1(float(alpha),float(v.z))*vh/v.z;
    double pdf = transmit ? ph*(1-f)*uz::PathStackHalfJacobian(float(vh),float(lh),etaI,etaT)
                          : ph*f/(4*vh);
    return pdf*uz::PathStackEventWeight(float(alpha),float(std::abs(l.z)),etaI,etaT,transmit);
}
}

TEST(LayeredBSDFStage3, SnellJacobianPreservesEtendueAndRoundTrips) {
    for (float ni : {1.f,1.33f,1.5f,2.4f})
        for (float nt : {1.f,1.33f,1.5f,2.4f})
            for (float ci : {.01f,.1f,.5f,.9f,1.f}) {
                float ct = uz::PathStackCosT(ci,ni,nt);
                if (ct < 0) {
                    EXPECT_FLOAT_EQ(uz::PathStackFresnel(ci,ni,nt),1.f);
                    continue;
                }
                float j = uz::PathStackDirectionJacobian(ci,ct,ni,nt);
                EXPECT_NEAR(nt*nt*ct*j,ni*ni*ci,2e-6);
                EXPECT_NEAR(j*uz::PathStackDirectionJacobian(ct,ci,nt,ni),1,3e-7);
                EXPECT_NEAR(ni*std::sqrt(1-ci*ci),nt*std::sqrt(std::max(0.f,1-ct*ct)),3e-5);
                EXPECT_NEAR(uz::PathStackFresnel(ci,ni,nt),uz::PathStackFresnel(ct,nt,ni),5e-4);
            }
}

TEST(LayeredBSDFStage3, RoughRefractionHalfVectorJacobianFiniteDifference) {
    const double thetaV = .4;
    const V3 v = {std::sin(thetaV),0,std::cos(thetaV)};
    for (double thetaH : {.05,.15,.35})
        for (float eta : {1.2f,1.5f,2.4f}) {
            auto refract = [&](double hAngle) {
                V3 h = {std::sin(hAngle),0,std::cos(hAngle)};
                double vh = dot(v,h), e = 1/double(eta);
                double ct = std::sqrt(1-e*e*(1-vh*vh));
                return -v*e+h*(e*vh-ct);
            };
            V3 h = {std::sin(thetaH),0,std::cos(thetaH)}, l = refract(thetaH);
            // A second independent tangent perturbation gives the full solid-angle determinant.
            constexpr double eps = 1e-5;
            V3 lp = refract(thetaH+eps), lm = refract(thetaH-eps);
            V3 dx = (lp+-lm)*(1/(2*eps));
            auto refY = [&](double y) {
                V3 hh = norm({h.x,y,h.z});
                double vh = dot(v,hh), e = 1/double(eta);
                return -v*e+hh*(e*vh-std::sqrt(1-e*e*(1-vh*vh)));
            };
            V3 dy = (refY(eps)+-refY(-eps))*(1/(2*eps));
            V3 cross = {dx.y*dy.z-dx.z*dy.y,dx.z*dy.x-dx.x*dy.z,dx.x*dy.y-dx.y*dy.x};
            double numerical = 1/std::sqrt(dot(cross,cross));
            EXPECT_NEAR(uz::PathStackHalfJacobian(float(dot(v,h)),float(dot(l,h)),1,eta),
                        numerical,numerical*3e-6);
        }
}

TEST(LayeredBSDFStage3, ReciprocalDielectricBoundaryWithRadianceEtaScaling) {
    for (double alpha : {.04,.25,1.})
        for (double nv : {.2,.5,1.})
            for (double nl : {.3,.7,.95}) {
                V3 v = {std::sqrt(1-nv*nv),0,nv};
                V3 reflected = {0,std::sqrt(1-nl*nl),nl};
                double f = boundaryValue(v,reflected,alpha,1,1.5f)/nl;
                double reverse = boundaryValue(reflected,v,alpha,1,1.5f)/nv;
                EXPECT_NEAR(f,reverse,1e-6*std::max(1.,f));
                V3 transmitted = {0,std::sqrt(1-nl*nl),-nl};
                f = boundaryValue(v,transmitted,alpha,1,1.5f)/nl;
                V3 reverseV = {-transmitted.x,-transmitted.y,-transmitted.z};
                V3 reverseL = {-v.x,-v.y,-v.z};
                reverse = boundaryValue(reverseV,reverseL,alpha,1.5f,1)/nv;
                EXPECT_NEAR(f*2.25,reverse,2e-5*std::max(1.,reverse));
            }
}

TEST(LayeredBSDFStage3, DepthOneIsBitExactLegacyBranch) {
    for (unsigned depth : {0u,1u})
        for (float sigma : {0.f,.7f})
            for (float v : {.01f,.5f,1.f}) {
                ASSERT_FALSE(uz::PathStackEnabled(depth,1.f));
                float t = uz::PathCoatTransmission(1,v,.5f,1.5f);
                float coat = uz::PathCoatReflection(1,.3f,1.5f,v,.5f,.7f,.8f);
                float legacy = uz::PathCoatCompose(.37f,coat,t*uz::PathCoatAbsorption(sigma,
                    uz::PathCoatPathLength(1,v,.5f,1.5f)));
                float dispatched = uz::PathStackEnabled(depth,1.f) ? -1.f : legacy;
                EXPECT_EQ(std::bit_cast<uint32_t>(legacy),std::bit_cast<uint32_t>(dispatched));
            }
    EXPECT_TRUE(uz::PathStackEnabled(2,1));
    EXPECT_TRUE(uz::PathStackEnabled(4,1));
    EXPECT_FALSE(uz::PathStackEnabled(4,0));
}

TEST(LayeredBSDFStage3, InternalTIRDoesNotCreateEnergy) {
    EXPECT_LT(uz::PathStackCosT(.5f,1.5f,1.f),0);
    EXPECT_FLOAT_EQ(uz::PathStackFresnel(.5f,1.5f,1.f),1);
    for (float c : {.01f,.1f,.5f,1.f}) {
        float ct = uz::PathStackCosT(c,1.f,1.5f);
        float entry = uz::PathStackEventWeight(.25f,ct,1,1.5f,true);
        float exit = uz::PathStackEventWeight(.25f,c,1.5f,1,true);
        EXPECT_LE(entry*exit,1.f+1e-7f);
        EXPECT_LE(uz::PathStackEventWeight(.25f,c,1.5f,1,false),1.f);
    }
}

TEST(LayeredBSDFStage3, WhiteFurnaceStackRelativeToPassiveBase) {
    constexpr int samples = 131072;
    const std::array<float,5> ior = {1,1.5f,1.1f,1.8f,1.33f};
    double maximum = 0, maxRelative = -1, maxBoundExcess = -1;
    unsigned totalTIR = 0;
    int cases = 0;
    for (int depth : {2,3,4})
        for (double nv : {.05,.5,1.})
            for (double r : {.15,.5,1.}) {
                double energy = 0, black = 0;
                for (int sample = 0; sample < samples; ++sample) {
                    uint32_t rng = hash(uint32_t(sample+1));
                    bool base = false;
                    double w = walk(nv,ior,depth,r,rng,base,totalTIR);
                    ASSERT_TRUE(std::isfinite(w));
                    ASSERT_GE(w,0);
                    ASSERT_LE(w,base ? .800001 : 1.000001);
                    energy += w;
                    if (!base) black += w;
                }
                energy /= samples; black /= samples;
                EXPECT_LE(energy,.8+black+1e-6);
                EXPECT_LE(energy,1.000001);
                maximum = std::max(maximum,energy);
                maxRelative = std::max(maxRelative,energy-.8);
                maxBoundExcess = std::max(maxBoundExcess,energy-.8-black);
                ++cases;
            }
    EXPECT_GT(totalTIR,0u);
    std::printf("STACK_FURNACE cases=%d samples=%d max_albedo=%.9f max_stack_minus_base=%.9f max_stack_minus_base_minus_interfaces=%.9f TIR_events=%u\n",
        cases,samples,maximum,maxRelative,maxBoundExcess,totalTIR);
}

TEST(LayeredBSDFStage3, UnitWhiteBaseAndInterfaceQuadratureRemainPassive) {
    // Integrate both rough-boundary hemispheres in solid angle. Scale the
    // transmitted radiance back to invariant radiance L/n^2 before adding it
    // to reflection: a change of medium is not absorption or energy creation.
    constexpr int cosines = 512, azimuths = 512;
    double maxBoundaryEnergy = 0, maxStackEnergy = 0;
    for (float ni : {1.f,1.5f})
        for (double alpha : {.25,1.})
            for (double nv : {.2,.5,1.}) {
                const float nt = ni == 1.f ? 1.5f : 1.f;
                V3 v = {std::sqrt(1-nv*nv),0,nv};
                double energy = 0;
                for (int c = 0; c < cosines; ++c) {
                    double nl = (c+.5)/cosines;
                    double radius = std::sqrt(1-nl*nl);
                    for (int a = 0; a < azimuths; ++a) {
                        double phi = 2*pi*(a+.5)/azimuths;
                        V3 l = {radius*std::cos(phi),radius*std::sin(phi),nl};
                        energy += boundaryValue(v,l,alpha,ni,nt);
                        l.z = -nl;
                        energy += boundaryValue(v,l,alpha,ni,nt)*(nt*nt)/(ni*ni);
                    }
                }
                energy *= 2*pi/(cosines*azimuths);
                EXPECT_LE(energy,1.002);
                maxBoundaryEnergy = std::max(maxBoundaryEnergy,energy);
            }
    const std::array<float,5> ior = {1,1.5f,1.1f,1.8f,1.33f};
    constexpr int samples = 131072;
    unsigned tir = 0;
    for (int depth : {2,3,4})
        for (double nv : {.05,.5,1.}) {
            double energy = 0;
            for (int sample = 0; sample < samples; ++sample) {
                uint32_t rng = hash(uint32_t(sample+1));
                bool base = false;
                double w = walk(nv,ior,depth,.5,rng,base,tir,1.);
                ASSERT_TRUE(std::isfinite(w));
                ASSERT_LE(w,1.000002);
                energy += w;
            }
            energy /= samples;
            EXPECT_LE(energy,1.000002);
            maxStackEnergy = std::max(maxStackEnergy,energy);
        }
    std::printf("STACK_UNIT_FURNACE max_boundary_quadrature=%.9f max_stack_albedo=%.9f TIR_events=%u\n",
        maxBoundaryEnergy,maxStackEnergy,tir);
}
