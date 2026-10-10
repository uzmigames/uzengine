#include "vfx_camera_di.h"

#include <type_traits>

namespace uz {

// ── Layout assertions — keep CPU mirror byte-for-byte compatible with HLSL ──

static_assert(sizeof(VfxCameraConstants) == 192u,
              "VfxCameraConstants must stay 192 bytes (HLSL VfxCameraCB size).");

static_assert(std::is_trivially_copyable_v<VfxCameraConstants>,
              "VfxCameraConstants must be trivially copyable for direct CB upload.");

// Offsets matching the HLSL cbuffer layout. Any drift will surface here
// immediately rather than manifesting as garbled shader reads at runtime.
static_assert(offsetof(VfxCameraConstants, viewMatrix) == 0u);
static_assert(offsetof(VfxCameraConstants, projMatrix) == 64u);
static_assert(offsetof(VfxCameraConstants, cameraPos)  == 128u);
static_assert(offsetof(VfxCameraConstants, cameraFwd)  == 144u);
static_assert(offsetof(VfxCameraConstants, fovY)       == 156u);

float ComputeVfxCameraAspect(const Vec2& screenSize) noexcept {
    // Defensive: zero-height screens would divide by zero during aspect
    // correction. Falling back to 1.0 keeps shaders producing sensible
    // (if ugly) output instead of NaN.
    if (!(screenSize.y > 0.0f)) {
        return 1.0f;
    }
    return screenSize.x / screenSize.y;
}

VfxCameraConstants BuildVfxCameraConstants(const VfxCameraSource& src) noexcept {
    VfxCameraConstants cb;
    cb.viewMatrix  = src.viewMatrix;
    cb.projMatrix  = src.projMatrix;
    cb.cameraPos   = src.position;
    cb.cameraFwd   = src.forward;
    cb.fovY        = src.fovYRadians;
    cb.nearZ       = src.nearZ;
    cb.farZ        = src.farZ;
    cb.aspect      = ComputeVfxCameraAspect(src.screenSize);
    cb.screenSize  = src.screenSize;
    return cb;
}

} // namespace uz
