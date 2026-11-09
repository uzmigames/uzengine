#pragma once

#include "../../../shaders/runtime/pathtracing/pathtracer_math.hlsli"
#include <algorithm>
#include <cmath>

namespace path_furnace {
constexpr double pi = 3.141592653589793;

// CPU reproduction of ibl/brdf_lut_cs.hlsl, including its RG16F storage.
// This is deliberately separate from the independent furnace quadrature.
inline double Half(double value) {
    int exponent;
    std::frexp(value, &exponent);
    exponent = std::max(exponent, -13);
    return std::ldexp(std::nearbyint(std::ldexp(value, 11-exponent)), exponent-11);
}

inline double Texel(double nv, double roughness, double f0) {
    const double alpha = std::max(roughness*roughness, .0064);
    const double a2 = alpha*alpha, vx = std::sqrt(1-nv*nv);
    const double length = std::hypot(alpha*vx, nv);
    const double sx = alpha*vx/length, sz = nv/length;
    const double g1 = 2*nv/(nv+std::sqrt(nv*nv*(1-a2)+a2));
    double scale = 0, bias = 0;
    for (unsigned i = 0; i < 1024; ++i) {
        unsigned bits = i, reversed = 0;
        for (int k = 0; k < 10; ++k) {
            reversed = 2*reversed+(bits&1u);
            bits >>= 1u;
        }
        const double phi = 2*pi*i/1024;
        const double z = (1-double(reversed)/1024)*(1+sz)-sz;
        const double tangent = std::sqrt(std::max(0.0, 1-z*z));
        double hx = alpha*(tangent*std::cos(phi)+sx);
        const double hy = alpha*tangent*std::sin(phi);
        double hz = std::max(z+sz, 0.0);
        const double hLength = std::sqrt(hx*hx+hy*hy+hz*hz);
        hx /= hLength;
        hz /= hLength;
        const double vh = vx*hx+nv*hz, nl = 2*vh*hz-nv;
        if (nl <= 0) continue;
        const double g2 = 2*nv*nl/(nl*std::sqrt(nv*nv*(1-a2)+a2)
            + nv*std::sqrt(nl*nl*(1-a2)+a2));
        const double fc = std::pow(1-vh, 5);
        scale += (1-fc)*g2/g1;
        bias += fc*g2/g1;
    }
    return f0*Half(scale/1024)+Half(bias/1024);
}

inline float DiffuseEnergy(double nv, double roughness, double f0) {
    // The shipped IBL LUT is 512x512; duplicate the PT texel-center clamp.
    constexpr int size = 512;
    const double x = std::clamp(nv*size-.5, 0.0, double(size-1));
    const double y = std::clamp(roughness*size-.5, 0.0, double(size-1));
    const int ix = int(x), iy = int(y);
    const double fx = x-ix, fy = y-iy;
    double albedo = 0;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i)
            albedo += Texel(std::max(.001, (std::min(ix+i,size-1)+.5)/size),
                (std::min(iy+j,size-1)+.5)/size, f0)
                * (i ? fx : 1-fx) * (j ? fy : 1-fy);
    return uz::PathDiffuseEnergy(float(albedo),float(nv));
}

// Independent Heitz visible-normal disk quadrature, NOT the spherical-cap
// Hammersley quadrature used to bake the LUT. VNDF resolves the narrow .1 lobe
// that uniform/cosine hemisphere quadrature misses at grazing angles.
inline double SpecularIntegral(double nv, double roughness, double f0) {
    constexpr int radial = 256, angular = 512;
    const double alpha = std::max(roughness*roughness, .0064), a2 = alpha*alpha;
    const double vx = std::sqrt(1-nv*nv), length = std::hypot(alpha*vx,nv);
    const double sx = alpha*vx/length, sz = nv/length, s = (1+sz)/2;
    const double g1 = 2*nv/(nv+std::sqrt(nv*nv*(1-a2)+a2));
    double sum = 0;
    for (int i = 0; i < radial; ++i) {
        const double radius = std::sqrt((i+.5)/radial);
        for (int j = 0; j < angular; ++j) {
            const double phi = 2*pi*(j+.5)/angular;
            const double x = radius*std::cos(phi);
            const double y = (1-s)*std::sqrt(1-x*x)+s*radius*std::sin(phi);
            const double z = std::sqrt(std::max(0.0,1-x*x-y*y));
            double hx = alpha*(-sz*y+sx*z);
            const double hy = alpha*x;
            double hz = std::max(0.0,sx*y+sz*z);
            const double hLength = std::sqrt(hx*hx+hy*hy+hz*hz);
            hx /= hLength;
            hz /= hLength;
            const double vh = vx*hx+nv*hz, nl = 2*vh*hz-nv;
            if (nl <= 0) continue;
            const double g2 = 2*nv*nl/(nl*std::sqrt(nv*nv*(1-a2)+a2)
                + nv*std::sqrt(nl*nl*(1-a2)+a2));
            sum += (f0+(1-f0)*std::pow(1-vh,5))*g2/g1;
        }
    }
    return sum/(radial*angular);
}
} // namespace path_furnace
