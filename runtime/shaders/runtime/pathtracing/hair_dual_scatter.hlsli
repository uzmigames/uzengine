#ifndef UZ_PATH_HAIR_DUAL_SCATTER
#define UZ_PATH_HAIR_DUAL_SCATTER
#include "hair_bcsdf.hlsli"

// @ref Zinke et al. 2008 "Dual Scattering Approximation for Fast Multiple
// Scattering in Hair", equations 6, 8, 11-16.
// This is a BIASED, passive tail closure, not an additional scattering lobe
// on an explicitly continued path. At K the entire remaining radiance is
// replaced, including NEE at that vertex. Its error is E[T_K (A-L_tail)].
// Assumptions: locally aligned, homogeneous fibers and distant illumination.
// Unlike the paper's raster sum, the kernel below spends at most the current
// fiber's exact albedo. Thus a unit furnace cannot acquire extra energy.
#ifdef __cplusplus
namespace uz {
#endif

struct HairDualMoments {
    float af, ab, vf, vb, shiftF, shiftB;
};

inline float HairDualForwardMass(int p, float scale, float gammaO, float gammaT) {
    // Both BCSDF directions point away from the vertex: the forward half-cone
    // is pi/2 < phi < 3pi/2. Integrate the *wrapped*, trimmed logistic exactly.
    // @constants pi and 2pi delimit the normalized azimuthal distribution.
    float center = HairPhi(p,gammaO,gammaT);
    float mass = 0.0f;
    float norm = HairLogisticCDF(3.14159265359f,scale)
               - HairLogisticCDF(-3.14159265359f,scale);
    for (int turn = -2; turn <= 2; ++turn) {
        float lo = max(-3.14159265359f,1.57079632679f-center+6.28318530718f*float(turn));
        float hi = min(3.14159265359f,4.71238898038f-center+6.28318530718f*float(turn));
        if (hi > lo) mass += (HairLogisticCDF(hi,scale)-HairLogisticCDF(lo,scale))/norm;
    }
    return max(0.0f,min(1.0f,mass));
}

inline HairDualMoments HairDualIntegrals(float sinO, float sigmaA,
    float betaM, float betaN, float eta, float alpha) {
    HairDualMoments m;
    m.af = m.ab = m.vf = m.vb = m.shiftF = m.shiftB = 0.0f;
    float cosO = sqrt(max(0.0f,1.0f-sinO*sinO));
    float etaPrime = sqrt(eta*eta-sinO*sinO)/max(cosO,1e-6f);
    float cosT = sqrt(1.0f-sinO*sinO/(eta*eta));
    float scale = HairLogisticScale(betaN);
    // @constants 16 = midpoint quadrature of uniform incident offsets h.
    // Mp integrates to one in d(sin theta); no surface cosine or second
    // longitudinal quadrature is needed for these lobe integrals.
    for (int i = 0; i < 16; ++i) {
        float h = -1.0f+(float(i)+0.5f)/8.0f;
        float go = asin(h), gt = asin(h/etaPrime);
        float f = HairFresnel(cosO*cos(go),eta);
        float t = exp(-sigmaA*2.0f*cos(gt)/cosT);
        for (int p = 0; p < 3; ++p) {
            float a = HairAp(p,f,t)/16.0f;
            float front = HairDualForwardMass(p,scale,go,gt);
            float af = a*front, ab = a*(1.0f-front);
            m.af += af; m.ab += ab;
            m.vf += af*HairVariance(betaM,p);
            m.vb += ab*HairVariance(betaM,p);
            m.shiftF += af*HairShift(p,alpha);
            m.shiftB += ab*HairShift(p,alpha);
        }
    }
    m.vf /= max(m.af,1e-20f); m.vb /= max(m.ab,1e-20f);
    m.shiftF /= max(m.af,1e-20f); m.shiftB /= max(m.ab,1e-20f);
    return m;
}

inline float HairDualBackEnergy(float af, float ab) {
    float r = af*af, d = max(1.0f-r,1e-6f);
    return ab*r/d + ab*ab*ab*r/(d*d*d);
}

inline float HairDualBackVariance(HairDualMoments m) {
    float r = m.af*m.af, d = max(1.0f-r,1e-6f);
    float a1 = m.ab*r/d, a3 = m.ab*m.ab*m.ab*r/(d*d*d);
    // Moments of the same series, rather than a Gaussian fitted independently:
    // E[2i|A1]=2/(1-r); E[m|A3]=2+6r/(1-r).
    return (a1*(2.0f*m.vf/d+m.vb)
          + a3*((2.0f+6.0f*r/d)*m.vf+3.0f*m.vb))/max(a1+a3,1e-20f);
}

inline float HairDualBackShift(HairDualMoments m) {
    float r = m.af*m.af, d = max(1.0f-r,1e-6f);
    float a1 = m.ab*r/d, a3 = m.ab*m.ab*m.ab*r/(d*d*d);
    return (a1*(2.0f*m.shiftF/d+m.shiftB)
          + a3*((2.0f+6.0f*r/d)*m.shiftF+3.0f*m.shiftB))/max(a1+a3,1e-20f);
}

inline float HairDualBudgetScale(float albedo, float backEnergy) {
    return albedo/max(albedo+backEnergy,1e-20f);
}

inline bool HairDualReplace(bool hair, bool enabled, int explicitVertices, int k) {
    return hair && enabled && explicitVertices >= k;
}

#ifdef __cplusplus
} // namespace uz
#else
struct HairDualKernel {
    float3 a[4];
    float variance[4];
    float shift[4];
    float3 af;
    float gammaO, gammaT;
};

// Count actual hair intersections, not opaque shadow hits. Non-hair geometry
// occludes the distant boundary. Capped traversal conservatively absorbs paths
// that have not escaped; it never invents an unobstructed boundary.
uint HairDualDepth(float3 P, float3 N, float3 L, out bool escaped) {
    RayDesc ray;
    ray.Origin = P+N*(dot(N,L)<0.0 ? -0.05 : 0.05);
    ray.Direction = L; ray.TMin = 0.01; ray.TMax = 1e7;
    StructuredBuffer<MaterialData> materials = ResourceDescriptorHeap[materialTableSrv];
    escaped = false;
    // @constants 64 = bounded ray-shooting work; offsets match stage 1.
    for (uint n = 0u; n < 64u; ++n) {
        RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
        query.TraceRayInline(tScene,RAY_FLAG_NONE,0xff,ray);
        while (query.Proceed()) {}
        if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
            escaped = true;
            return n;
        }
        if (!PathIsHair(materials[query.CommittedInstanceIndex()])) return n;
        ray.TMin = query.CommittedRayT()+0.06;
    }
    return 64u;
}

HairDualKernel HairDualMake(float3 P, float3 N, float3 V, float3 tangent,
                            HairMaterial hair, float density) {
    float sinO = clamp(dot(V,tangent),-1.0,1.0);
    float cosO = sqrt(max(0.0,1.0-sinO*sinO));
    HairDualMoments mr = HairDualIntegrals(sinO,hair.sigmaA.r,hair.betaM,hair.betaN,hair.eta,hair.alpha);
    HairDualMoments mg = HairDualIntegrals(sinO,hair.sigmaA.g,hair.betaM,hair.betaN,hair.eta,hair.alpha);
    HairDualMoments mb = HairDualIntegrals(sinO,hair.sigmaA.b,hair.betaM,hair.betaN,hair.eta,hair.alpha);
    HairDualKernel kernel;
    kernel.af = float3(mr.af,mg.af,mb.af);
    bool escapeF,escapeB;
    uint nf = HairDualDepth(P,N,-N,escapeF);
    uint nb = HairDualDepth(P,N,N,escapeB);
    // A single vertex-wide spread preserves normalization. Making variance a
    // function of the integration direction would invalidate the furnace proof.
    float n = 0.5*float(nf+nb);
    float db = nf+nb == 0u ? 0.0 : density;
    float3 a[3], probabilities;
    HairAttenuation(sinO,cosO,hair,a,probabilities,kernel.gammaO,kernel.gammaT);
    float3 albedo = a[0]+a[1]+a[2];
    float3 back = db*float3(HairDualBackEnergy(mr.af,mr.ab),
        HairDualBackEnergy(mg.af,mg.ab),HairDualBackEnergy(mb.af,mb.ab));
    float3 scale = float3(HairDualBudgetScale(albedo.r,back.r),
        HairDualBudgetScale(albedo.g,back.g),HairDualBudgetScale(albedo.b,back.b));
    [unroll] for (int p = 0; p < 3; ++p) {
        kernel.a[p] = a[p]*scale;
        kernel.variance[p] = HairVariance(hair.betaM,p)+n*mg.vf;
        kernel.shift[p] = HairShift(p,hair.alpha)+n*mg.shiftF;
    }
    kernel.a[3] = back*scale;
    kernel.variance[3] = max(HairDualBackVariance(mg)+n*mg.vf,1e-4);
    kernel.shift[3] = HairDualBackShift(mg)+n*mg.shiftF;
    return kernel;
}

float3 HairDualTransmission(float3 P, float3 N, float3 L, float3 af) {
    bool escaped;
    uint n = HairDualDepth(P,N,L,escaped);
    if (!escaped) return 0.0;
    // @ref Zinke 2008, equation 5. No attenuation on a direct boundary path.
    return n == 0u ? float3(1,1,1) : pow(af,float(n));
}

float3 HairDualEvaluate(float3 V, float3 L, float3 tangent, float3 radial,
    HairMaterial hair, HairDualKernel kernel) {
    float3 B = cross(tangent,radial);
    float thetaO = asin(clamp(dot(V,tangent),-1.0,1.0));
    float sinI = clamp(dot(L,tangent),-1.0,1.0);
    float cosI = sqrt(max(0.0,1.0-sinI*sinI));
    float phi = atan2(dot(L,B),dot(L,radial))-atan2(dot(V,B),dot(V,radial));
    float3 value = 0.0;
    [unroll] for (int p = 0; p < 4; ++p) {
        float theta = thetaO-kernel.shift[p];
        float longitudinal = HairMp(sinI,cosI,sin(theta),abs(cos(theta)),kernel.variance[p]);
        float azimuth = p < 3
            ? HairNp(phi,p,HairLogisticScale(hair.betaN),kernel.gammaO,kernel.gammaT)
            : (cos(phi)>=0.0 ? 1.0/PI : 0.0);
        value += kernel.a[p]*longitudinal*azimuth;
    }
    return value;
}

float3 HairDualTail(float3 P, float3 N, float3 V, float3 tangent,
    HairMaterial hair, float density, uint dimension) {
    HairDualKernel kernel = HairDualMake(P,N,V,tangent,hair,density);
    float3 value = 0.0;
    float3 B = cross(tangent,N);
    float thetaO = asin(clamp(dot(V,tangent),-1.0,1.0));
    float phiO = atan2(dot(V,B),dot(V,N));
    // Stratify the four lobes instead of selecting one; all remaining scattering
    // events are analytic. No explicit continuation is launched after this call.
    [unroll] for (int p = 0; p < 4; ++p) {
        if (!any(kernel.a[p]>0.0)) continue;
        float2 u = PathSample(dimension+uint(p)*2u);
        float w = PathSample(dimension+uint(p)*2u+1u).y;
        float theta = thetaO-kernel.shift[p];
        float sinI = HairSampleSinTheta(sin(theta),abs(cos(theta)),kernel.variance[p],u.x,u.y);
        float phi = phiO+(p < 3
            ? HairPhi(p,kernel.gammaO,kernel.gammaT)+HairLogisticSample(w,HairLogisticScale(hair.betaN))
            : (w-0.5)*PI);
        float3 L = sinI*tangent+sqrt(max(0.0,1.0-sinI*sinI))*(cos(phi)*N+sin(phi)*B);
        value += kernel.a[p]*PathEnvironment(L)*HairDualTransmission(P,N,L,kernel.af);
    }
    StructuredBuffer<PathDirectionalLight> lights = ResourceDescriptorHeap[directionalLightsSrv];
    for (uint id = 0u; id < directionalLightCount; ++id) {
        PathDirectionalLight light = lights[id];
        float3 L = normalize(light.direction), T, bitangent;
        PathBasis(L,T,bitangent);
        for (uint sample = 0u; sample < neeSamples; ++sample) {
            float2 u = PathSample(dimension+16u+id*32u+sample);
            float radius = sqrt(u.x)*tan(light.angularRadius);
            float phi = 2.0*PI*u.y;
            float3 direction = normalize(L+radius*(cos(phi)*T+sin(phi)*bitangent));
            value += light.radiance*HairDualEvaluate(V,direction,tangent,N,hair,kernel)
                *HairDualTransmission(P,N,direction,kernel.af)/float(neeSamples);
        }
    }
    return value;
}
#endif
#endif
