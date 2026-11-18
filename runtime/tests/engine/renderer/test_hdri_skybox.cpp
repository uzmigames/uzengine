// phase2_hdri-skybox-completion: the HDRI skybox contracts the GPU validation
// (E:/UzmiEngine/_evidence/lighting/hdri-skybox, 2026-11-17) relied on, pinned on
// the CPU against the shipped shader sources and the C++ layout.
#include "constant_buffers.h"
#include "cvar.h"
#include "engine_shaders.h"
#include "hdri_skybox_pass.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <regex>
#include <string>

namespace {

std::string StripComments(const char* source) {
    return std::regex_replace(std::string(source), std::regex(R"(//[^\r\n]*|/\*[\s\S]*?\*/)"), " ");
}

constexpr double kPi = 3.14159265358979323846;

struct V3 { double x, y, z; };

/// equirect_to_cubemap_cs.hlsl `CubeFaceDirection`: face texel (u, v in [0,1]) -> direction.
V3 CubeFaceDirection(int face, double u, double v) {
    u = u * 2.0 - 1.0;
    v = v * 2.0 - 1.0;
    switch (face) {
        case 0:  return { 1.0, -v, -u};
        case 1:  return {-1.0, -v,  u};
        case 2:  return { u,  1.0,  v};
        case 3:  return { u, -1.0, -v};
        case 4:  return { u, -v,  1.0};
        default: return {-u, -v, -1.0};
    }
}

/// equirect_to_cubemap_cs.hlsl `DirectionToEquirect` (cube frame, +Y up), inlined by
/// path_tracer_ps.hlsl for the raw panorama.
void DirectionToEquirect(V3 d, double& u, double& v) {
    const double n = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    d = {d.x / n, d.y / n, d.z / n};
    const double phi   = std::atan2(d.z, d.x);
    const double theta = std::asin(d.y < -1.0 ? -1.0 : (d.y > 1.0 ? 1.0 : d.y));
    u = phi / kPi * 0.5 + 0.5;
    v = 1.0 - (theta / kPi + 0.5);
}

/// skybox_cubemap_ps.hlsl / deferred_lighting_ps.hlsl `WorldToCubemap`: Z-up world -> Y-up cube.
V3 WorldToCubemap(V3 w) { return {w.x, w.z, w.y}; }

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// r.HDRISkybox
// ─────────────────────────────────────────────────────────────────────────────

TEST(HdriSkyboxCVar, RegisteredWithTheCubemapModeAsDefaultAndTheThreeModes) {
    const uz::CVar* cv = uz::CVarRegistry::Get().Find("r.HDRISkybox");
    ASSERT_NE(cv, nullptr);
    // 1 is the behaviour the engine had before the CVar existed: the only default that
    // leaves a cubemap_path scene byte-identical.
    EXPECT_EQ(static_cast<int>(cv->defaultValue), static_cast<int>(uz::HdriSkyboxMode::Cubemap));
    EXPECT_EQ(static_cast<int>(cv->minValue), static_cast<int>(uz::HdriSkyboxMode::Off));
    EXPECT_EQ(static_cast<int>(cv->maxValue), static_cast<int>(uz::HdriSkyboxMode::SourceResolution));
    EXPECT_EQ(static_cast<int>(uz::HdriSkyboxMode::Off), 0);
    EXPECT_EQ(static_cast<int>(uz::HdriSkyboxMode::Cubemap), 1);
    EXPECT_EQ(static_cast<int>(uz::HdriSkyboxMode::SourceResolution), 2);
}

TEST(HdriSkyboxCubeResolution, WidthOverFourPowerOfTwoCappedAndNeverBelowTheAuthoredValue) {
    using uz::HdriSkyboxCubeResolution;
    EXPECT_EQ(HdriSkyboxCubeResolution(2048, 128), 512u);   // the HDR loader's 2048 cap -> 512
    EXPECT_EQ(HdriSkyboxCubeResolution(4096, 128), 1024u);
    EXPECT_EQ(HdriSkyboxCubeResolution(8192, 128), uz::kHdriSkyboxMaxCubeResolution);
    EXPECT_EQ(HdriSkyboxCubeResolution(1536, 128), 256u);   // 384 rounds down to a power of two
    EXPECT_EQ(HdriSkyboxCubeResolution(1000, 128), 128u);   // 250 -> 128
    EXPECT_EQ(HdriSkyboxCubeResolution(512, 256), 256u);    // the authored value is the floor
    EXPECT_EQ(HdriSkyboxCubeResolution(0, 128), 128u);
    static_assert(uz::HdriSkyboxCubeResolution(2048, 128) == 512u, "constexpr");
    // IBLBaker::EquirectToCubemap dispatches 8x8 groups: a multiple of 8 for every authored multiple of 8.
    for (const uint32_t w : {64u, 100u, 512u, 2048u, 4096u}) {
        EXPECT_EQ(HdriSkyboxCubeResolution(w, 128) % 8u, 0u) << w;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// skybox_cubemap_ps.hlsl contracts
// ─────────────────────────────────────────────────────────────────────────────

TEST(HdriSkyboxShader, PerFrameCBPrefixMatchesTheCppLayoutUpToInvViewProj) {
    // The shader declares only the leading members of PerFrameCB at b1; invViewProj must sit at
    // byte 144, after the 64-byte viewProj and five float4s, exactly as sky_atmosphere_ps.hlsl.
    const std::regex prefix(
        R"(cbuffer\s+PerFrameCB\s*:\s*register\s*\(\s*b1\s*\)\s*\{\s*)"
        R"(row_major\s+float4x4\s+viewProj\s*;\s*float4\s+cameraPos\s*;\s*float4\s+timeParams\s*;\s*)"
        R"(float4\s+lightDir\s*;\s*float4\s+lightColor\s*;\s*float4\s+ambientColor\s*;\s*)"
        R"(row_major\s+float4x4\s+invViewProj\s*;\s*\})");
    EXPECT_TRUE(std::regex_search(StripComments(uz::EngineShaders::GetSource(uz::EngineShader::SkyboxCubemapPS)), prefix));
    EXPECT_TRUE(std::regex_search(StripComments(uz::EngineShaders::GetSource(uz::EngineShader::SkyAtmospherePS)), prefix));
    EXPECT_EQ(offsetof(uz::PerFrameCB, viewProj), 0u);
    EXPECT_EQ(offsetof(uz::PerFrameCB, cameraPos), 64u);
    EXPECT_EQ(offsetof(uz::PerFrameCB, timeParams), 80u);
    EXPECT_EQ(offsetof(uz::PerFrameCB, lightDir), 96u);
    EXPECT_EQ(offsetof(uz::PerFrameCB, lightColor), 112u);
    EXPECT_EQ(offsetof(uz::PerFrameCB, ambientColor), 128u);
    EXPECT_EQ(offsetof(uz::PerFrameCB, invViewProj), 144u);
}

TEST(HdriSkyboxShader, SkyPixelsOnlyTwoPointUnprojectAndNoSecondExposure) {
    const std::string sky = StripComments(uz::EngineShaders::GetSource(uz::EngineShader::SkyboxCubemapPS));
    const std::string atmo = StripComments(uz::EngineShaders::GetSource(uz::EngineShader::SkyAtmospherePS));
    // Reversed-Z: the far plane is depth 0; every geometry pixel is discarded, as in the procedural sky.
    const std::regex discard(R"(if\s*\(\s*depth\s*>\s*0\.0001\s*\)\s*discard\s*;)");
    EXPECT_TRUE(std::regex_search(sky, discard));
    EXPECT_TRUE(std::regex_search(atmo, discard));
    // The same two-point unproject through invViewProj as sky_atmosphere_ps.hlsl.
    const std::regex unproject(R"(normalize\s*\(\s*pt0\.xyz\s*/\s*pt0\.w\s*-\s*pt1\.xyz\s*/\s*pt1\.w\s*\))");
    EXPECT_TRUE(std::regex_search(sky, unproject));
    EXPECT_TRUE(std::regex_search(atmo, unproject));
    // The cubemap is sampled with the Z-up -> Y-up swizzle the IBL lookups use, so a mirror shows the sky behind it.
    const std::regex swizzle(R"(WorldToCubemap\s*\(\s*float3\s+dir\s*\)\s*\{\s*return\s+float3\s*\(\s*dir\.x\s*,\s*dir\.z\s*,\s*dir\.y\s*\)\s*;\s*\})");
    EXPECT_TRUE(std::regex_search(sky, swizzle));
    EXPECT_TRUE(std::regex_search(StripComments(uz::EngineShaders::GetSource(uz::EngineShader::DeferredLightingPS)), swizzle));
    EXPECT_TRUE(std::regex_search(sky, std::regex(R"(SampleLevel\s*\(\s*linearSampler\s*,\s*cubemapDir\s*,\s*0\s*\))")));
    // Exposure is applied once, at the tonemap, to the sky and the lit scene alike: the skybox must not scale.
    EXPECT_EQ(sky.find("exposure"), std::string::npos);
    EXPECT_EQ(sky.find("Exposure"), std::string::npos);
    EXPECT_TRUE(std::regex_search(StripComments(uz::EngineShaders::GetSource(uz::EngineShader::TonemapPS)),
                                  std::regex(R"(color\s*\*=\s*exposure\s*;)")));
}

// ─────────────────────────────────────────────────────────────────────────────
// equirect_to_cubemap_cs.hlsl orientation (what the labeled-panorama captures showed)
// ─────────────────────────────────────────────────────────────────────────────

TEST(HdriSkyboxOrientation, ShippedConverterUsesTheModelledMapping) {
    const std::string cs = StripComments(uz::EngineShaders::GetSource(uz::EngineShader::EquirectToCubemapCS));
    EXPECT_TRUE(std::regex_search(cs, std::regex(R"(float\s+phi\s*=\s*atan2\s*\(\s*dir\.z\s*,\s*dir\.x\s*\)\s*;)")));
    EXPECT_TRUE(std::regex_search(cs, std::regex(R"(float\s+theta\s*=\s*asin\s*\(\s*clamp\s*\(\s*dir\.y\s*,\s*-1\s*,\s*1\s*\)\s*\)\s*;)")));
    EXPECT_TRUE(std::regex_search(cs, std::regex(R"(float\s+u\s*=\s*phi\s*\*\s*M_1_PI\s*\*\s*0\.5\s*\+\s*0\.5\s*;)")));
    EXPECT_TRUE(std::regex_search(cs, std::regex(R"(float\s+v\s*=\s*theta\s*\*\s*M_1_PI\s*\+\s*0\.5\s*;)")));
    EXPECT_TRUE(std::regex_search(cs, std::regex(R"(return\s+float2\s*\(\s*u\s*,\s*1\.0\s*-\s*v\s*\)\s*;)")));
    EXPECT_TRUE(std::regex_search(cs, std::regex(R"(case\s+0\s*:\s*return\s+normalize\s*\(\s*float3\s*\(\s*1\s*,\s*-v\s*,\s*-u\s*\)\s*\)\s*;)")));
    EXPECT_TRUE(std::regex_search(cs, std::regex(R"(case\s+1\s*:\s*return\s+normalize\s*\(\s*float3\s*\(\s*-1\s*,\s*-v\s*,\s*u\s*\)\s*\)\s*;)")));
    EXPECT_TRUE(std::regex_search(cs, std::regex(R"(case\s+2\s*:\s*return\s+normalize\s*\(\s*float3\s*\(\s*u\s*,\s*1\s*,\s*v\s*\)\s*\)\s*;)")));
    EXPECT_TRUE(std::regex_search(cs, std::regex(R"(case\s+3\s*:\s*return\s+normalize\s*\(\s*float3\s*\(\s*u\s*,\s*-1\s*,\s*-v\s*\)\s*\)\s*;)")));
    EXPECT_TRUE(std::regex_search(cs, std::regex(R"(case\s+4\s*:\s*return\s+normalize\s*\(\s*float3\s*\(\s*u\s*,\s*-v\s*,\s*1\s*\)\s*\)\s*;)")));
    EXPECT_TRUE(std::regex_search(cs, std::regex(R"(case\s+5\s*:\s*return\s+normalize\s*\(\s*float3\s*\(\s*-u\s*,\s*-v\s*,\s*-1\s*\)\s*\)\s*;)")));
}

TEST(HdriSkyboxOrientation, WorldAxesLandWhereTheLabeledPanoramaShowedThem) {
    double u = 0.0, v = 0.0;
    // World +X is the panorama's centre column, on the horizon row.
    DirectionToEquirect(WorldToCubemap({1.0, 0.0, 0.0}), u, v);
    EXPECT_NEAR(u, 0.5, 1e-9); EXPECT_NEAR(v, 0.5, 1e-9);
    // World +Y (the camera's right when it faces +X in the left-handed Z-up world) is u = 0.75:
    // u increases to the right, so the panorama reads unmirrored from the inside.
    DirectionToEquirect(WorldToCubemap({0.0, 1.0, 0.0}), u, v);
    EXPECT_NEAR(u, 0.75, 1e-9);
    DirectionToEquirect(WorldToCubemap({0.0, -1.0, 0.0}), u, v);
    EXPECT_NEAR(u, 0.25, 1e-9);
    // World +Z (up) is the top row, -Z the bottom row.
    DirectionToEquirect(WorldToCubemap({0.0, 0.0, 1.0}), u, v);
    EXPECT_NEAR(v, 0.0, 1e-9);
    DirectionToEquirect(WorldToCubemap({0.0, 0.0, -1.0}), u, v);
    EXPECT_NEAR(v, 1.0, 1e-9);
    // World -X is the u = 0 / 1 seam of the panorama.
    DirectionToEquirect(WorldToCubemap({-1.0, 0.0, 0.0}), u, v);
    EXPECT_TRUE(u < 1e-9 || u > 1.0 - 1e-9) << u;
    // The swizzle is its own inverse.
    const V3 w = WorldToCubemap(WorldToCubemap({0.2, 0.3, 0.9}));
    EXPECT_DOUBLE_EQ(w.x, 0.2); EXPECT_DOUBLE_EQ(w.y, 0.3); EXPECT_DOUBLE_EQ(w.z, 0.9);
}

TEST(HdriSkyboxOrientation, CubeFaceTableIsTheD3DConvention) {
    // A hardware TextureCube lookup in direction d returns the texel of the face whose axis
    // dominates d, at the D3D (u, v) of that face. The converter must write each texel from
    // that same direction, so the face axis must dominate every texel direction of the face
    // and the D3D orientation of the +X face (u toward -Z, v toward -Y) must hold.
    const double axes[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        const V3 c = CubeFaceDirection(f, 0.5, 0.5);
        EXPECT_NEAR(c.x, axes[f][0], 1e-12) << f;
        EXPECT_NEAR(c.y, axes[f][1], 1e-12) << f;
        EXPECT_NEAR(c.z, axes[f][2], 1e-12) << f;
        for (int iu = 0; iu < 9; ++iu) {
            for (int iv = 0; iv < 9; ++iv) {
                const V3 d = CubeFaceDirection(f, (iu + 0.5) / 9.0, (iv + 0.5) / 9.0);
                const double comp[3] = {d.x, d.y, d.z};
                int axis = 0;
                for (int k = 1; k < 3; ++k) {
                    if (std::fabs(comp[k]) > std::fabs(comp[axis])) axis = k;
                }
                EXPECT_EQ(axis, f / 2) << "face " << f;
                EXPECT_GT(comp[axis] * axes[f][axis], 0.0) << "face " << f;
            }
        }
    }
    const V3 px = CubeFaceDirection(0, 1.0, 0.0);
    EXPECT_NEAR(px.y, 1.0, 1e-12);
    EXPECT_NEAR(px.z, -1.0, 1e-12);
    const V3 py = CubeFaceDirection(2, 1.0, 1.0);
    EXPECT_NEAR(py.x, 1.0, 1e-12);
    EXPECT_NEAR(py.z, 1.0, 1e-12);
}
