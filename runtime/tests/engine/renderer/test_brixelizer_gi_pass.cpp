/// Public API safety tests for the optional Brixelizer pass.
///
/// These exercise the linked engine implementation, not a mirror of its
/// geometry conversion. They do NOT establish successful FFX context creation,
/// registration, bake/dispatch, or leak-free shutdown. The current build has
/// no wrapper DLL and links the no-op implementation.

#include "engine/renderer/passes/gi/brixelizer_gi_pass.h"
#include "engine/renderer/core/gbuffer/draw_list.h"
#include "engine/renderer/mesh/gpu_mesh.h"

#include <gtest/gtest.h>

#include <utility>

namespace uz::test {
namespace {

void ExpectUnavailableOutputs(const BrixelizerGIPass& pass) {
    EXPECT_FALSE(pass.IsInitialized());
    EXPECT_EQ(pass.GetDiffuseGISrv(), UINT32_MAX);
    EXPECT_EQ(pass.GetSpecularGISrv(), UINT32_MAX);
    EXPECT_EQ(pass.GetDiffuseGIUav(), UINT32_MAX);
    EXPECT_EQ(pass.GetSpecularGIUav(), UINT32_MAX);
    EXPECT_FALSE(pass.GetDiffuseGITexture().IsValid());
    EXPECT_FALSE(pass.GetSpecularGITexture().IsValid());
}

} // namespace

TEST(BrixelizerGIPass, DefaultOutputsAreUnavailable) {
    BrixelizerGIPass pass;
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, NullDeviceInitIsRejected) {
    BrixelizerGIPass pass;
    EXPECT_FALSE(pass.Init(nullptr, 64, 64));
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, ZeroExtentInitIsRejectedBeforeDeviceAccess) {
    BrixelizerGIPass pass;
    auto* dummy = reinterpret_cast<IRHIDevice*>(uintptr_t{1});
    EXPECT_FALSE(pass.Init(dummy, 0, 64));
    EXPECT_FALSE(pass.Init(dummy, 64, 0));
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, ShutdownIsIdempotentBeforeInit) {
    BrixelizerGIPass pass;
    pass.Shutdown();
    pass.Shutdown();
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, RejectedInitCanBeShutdownRepeatedly) {
    BrixelizerGIPass pass;
    EXPECT_FALSE(pass.Init(nullptr, 64, 64));
    pass.Shutdown();
    EXPECT_FALSE(pass.Init(nullptr, 64, 64));
    pass.Shutdown();
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, RegistrationBeforeInitDoesNotExposeOutputs) {
    BrixelizerGIPass pass;
    DrawCommand draw{};
    draw.meshId = 0;
    GPUMesh mesh{};
    mesh.vertexBuffer = {1, 1};
    mesh.indexBuffer = {2, 1};
    mesh.vertexCount = 3;
    mesh.indexCount = 3;
    mesh.vertexStride = 48;
    mesh.boundsMin = Vec3(-1.0f, -2.0f, -3.0f);
    mesh.boundsMax = Vec3(1.0f, 2.0f, 3.0f);
    const std::vector<DrawCommand> draws{draw};
    pass.RegisterGeometry(draws, &mesh, 1);
    pass.RegisterGeometry(draws, nullptr, 1);
    pass.RegisterGeometry(draws, &mesh, 0);
    pass.RegisterGeometry({}, &mesh, 1);
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, ExecuteBeforeInitDoesNotDereferenceInputs) {
    BrixelizerGIPass pass;
    pass.Execute(nullptr, nullptr, nullptr, nullptr, nullptr,
                 0.0f, 0.0f, 0.0f, 0, {}, {}, {}, {}, {});
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, ResizeBeforeInitDoesNotExposeOutputs) {
    BrixelizerGIPass pass;
    pass.OnResize(0, 64);
    pass.OnResize(64, 0);
    pass.OnResize(64, 64);
    pass.OnResize(1920, 1080);
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, UninitializedMoveConstructionAndAssignmentAreSafe) {
    BrixelizerGIPass source;
    BrixelizerGIPass moved(std::move(source));
    ExpectUnavailableOutputs(source);
    ExpectUnavailableOutputs(moved);
    BrixelizerGIPass assigned;
    assigned = std::move(moved);
    ExpectUnavailableOutputs(moved);
    ExpectUnavailableOutputs(assigned);
    source.Shutdown();
    moved.Shutdown();
    assigned.Shutdown();
}

} // namespace uz::test
