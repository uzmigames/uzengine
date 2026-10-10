/// Unit tests for VfxCameraConstants — VFX Camera Data Interface CB builder.
///
/// Covers: round-trip of VfxCameraSource -> VfxCameraConstants,
/// aspect-ratio derivation (including zero-height fallback), struct layout
/// determinism via the static_asserts in the .cpp file, and the register-
/// slot convention contract (b8).
///
/// @ref phase7_7.13.0-vfx-camera-data-interface

#include <gtest/gtest.h>

#include "engine/vfx/data_interfaces/vfx_camera_di.h"

using namespace uz;

TEST(VfxCameraDI, RegisterSlotConventionIsEight) {
    EXPECT_EQ(kVfxCameraCBRegisterSlot, 8u);
}

TEST(VfxCameraDI, CBSizeMatchesExpectedLayout) {
    EXPECT_EQ(kVfxCameraCBSize, 192u);
}

TEST(VfxCameraDI, BuildRoundTripsAllFields) {
    VfxCameraSource src;
    src.position     = {10.0f, 20.0f, 30.0f};
    src.forward      = {0.0f, 0.0f, -1.0f};
    src.fovYRadians  = 1.2f;
    src.nearZ        = 0.5f;
    src.farZ         = 2000.0f;
    src.screenSize   = {1920.0f, 1080.0f};
    // view/proj stay identity (default) — we're checking scalars here.

    const auto cb = BuildVfxCameraConstants(src);
    EXPECT_FLOAT_EQ(cb.cameraPos.x, 10.0f);
    EXPECT_FLOAT_EQ(cb.cameraPos.y, 20.0f);
    EXPECT_FLOAT_EQ(cb.cameraPos.z, 30.0f);
    EXPECT_FLOAT_EQ(cb.cameraFwd.z, -1.0f);
    EXPECT_FLOAT_EQ(cb.fovY, 1.2f);
    EXPECT_FLOAT_EQ(cb.nearZ, 0.5f);
    EXPECT_FLOAT_EQ(cb.farZ, 2000.0f);
    EXPECT_FLOAT_EQ(cb.screenSize.x, 1920.0f);
    EXPECT_FLOAT_EQ(cb.screenSize.y, 1080.0f);
}

TEST(VfxCameraDI, AspectDerivedFromScreenSize) {
    VfxCameraSource src;
    src.screenSize = {1920.0f, 1080.0f};
    const auto cb = BuildVfxCameraConstants(src);
    EXPECT_NEAR(cb.aspect, 1920.0f / 1080.0f, 1e-4f);
}

TEST(VfxCameraDI, AspectFallbackForZeroHeight) {
    // Zero-height should not divide by zero; fallback to 1.0 keeps shaders
    // from producing NaN.
    EXPECT_FLOAT_EQ(ComputeVfxCameraAspect({1920.0f, 0.0f}), 1.0f);
    EXPECT_FLOAT_EQ(ComputeVfxCameraAspect({1920.0f, -5.0f}), 1.0f);
}

TEST(VfxCameraDI, ConstantsAreZeroInitialised) {
    const VfxCameraConstants cb{};
    EXPECT_FLOAT_EQ(cb.cameraPos.x, 0.0f);
    EXPECT_FLOAT_EQ(cb.cameraPos.y, 0.0f);
    EXPECT_FLOAT_EQ(cb.cameraPos.z, 0.0f);
    EXPECT_FLOAT_EQ(cb.cameraFwd.z, -1.0f); // Default forward is -Z.
    EXPECT_FLOAT_EQ(cb.aspect, 16.0f / 9.0f);
}

TEST(VfxCameraDI, AlignmentIsSixteenBytes) {
    // Required for a proper ConstantBuffer upload; a misaligned struct would
    // break `CopyBufferRegion` on some drivers.
    EXPECT_EQ(alignof(VfxCameraConstants), 16u);
}
