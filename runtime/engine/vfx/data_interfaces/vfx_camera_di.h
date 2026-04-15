#pragma once

/// VFX Camera Data Interface — exposes active-camera state to every VFX
/// compute shader via a shared constant buffer (`b8` by convention).
///
/// Simulation shaders need camera state (position, forward vector, FOV,
/// near/far, view/projection matrices, screen size) for:
///   - Screen-space fade and soft-particle depth tests.
///   - LOD hysteresis driven by per-particle screen size.
///   - Billboard orientation math (e.g. camera-facing spawn direction).
///   - View-frustum early-out inside per-particle reduction passes.
///
/// Currently the mesh billboard VS is the only place where camera data
/// reaches the GPU VFX path; the spawn and update compute shaders have
/// no access at all. This CB fills that gap.
///
/// # Layout
///
/// Matches UE Niagara's `NiagaraCameraDataInterface` binding shape:
///
/// ```hlsl
/// cbuffer VfxCameraCB : register(b8) {
///     float4x4 viewMatrix;       // row-major world-space -> view-space
///     float4x4 projMatrix;       // row-major view-space -> clip-space
///     float3   cameraPos;   float _padPos;
///     float3   cameraFwd;   float fovY;        // in radians
///     float    nearZ;       float farZ;        float aspect; float _padTail;
///     float2   screenSize;  float2 _padScreen;
/// };
/// ```
///
/// Total size = 192 bytes. Caller binds the matching
/// `VfxCameraConstants` struct each frame.
///
/// @ref UE5 `UNiagaraDataInterfaceCamera` + `FNiagaraCameraShaderParameters`

#include "mat4.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"

#include <cstdint>

namespace uz {

/// CPU mirror of the HLSL `VfxCameraCB`. Must stay byte-for-byte compatible
/// with the shader struct — see the assertions in `vfx_camera_di.cpp`.
struct alignas(16) VfxCameraConstants {
    Mat4  viewMatrix {};
    Mat4  projMatrix {};
    Vec3  cameraPos   {};
    float _padPos    = 0.0f;
    Vec3  cameraFwd   {0.0f, 0.0f, -1.0f};
    float fovY       = 0.0f;     // in radians
    float nearZ      = 0.1f;
    float farZ       = 10000.0f;
    float aspect     = 16.0f / 9.0f;
    float _padTail   = 0.0f;
    Vec2  screenSize  {1920.0f, 1080.0f};
    Vec2  _padScreen  {};
};

/// Register slot convention. Kept in lock-step with the HLSL declaration so
/// the render graph can bind the CB without a per-shader mapping table.
inline constexpr uint32_t kVfxCameraCBRegisterSlot = 8u;

/// CB byte size. Compile-time constant so callers can reserve upload ring
/// allocations without instantiating the struct.
inline constexpr uint32_t kVfxCameraCBSize = sizeof(VfxCameraConstants);

/// Input data — what the renderer knows at frame-start.
struct VfxCameraSource {
    Vec3  position  {};                         ///< World-space camera position.
    Vec3  forward   {0.0f, 0.0f, -1.0f};        ///< World-space forward vector (normalised).
    Mat4  viewMatrix{};                         ///< World-space -> view-space.
    Mat4  projMatrix{};                         ///< View-space -> clip-space.
    float fovYRadians = 0.0f;                   ///< Vertical FOV in radians.
    float nearZ       = 0.1f;
    float farZ        = 10000.0f;
    Vec2  screenSize  {1920.0f, 1080.0f};       ///< Active swapchain resolution.
};

/// Populates a `VfxCameraConstants` from the active camera source. The
/// helper exists to keep the struct layout (ordering, padding) in a single
/// place; renderers just call this per-frame with a live `VfxCameraSource`.
///
/// # Returns
/// A fully populated constants struct ready for upload to the CB ring.
[[nodiscard]] VfxCameraConstants BuildVfxCameraConstants(const VfxCameraSource& src) noexcept;

/// Derived aspect ratio from the screen size. Exposed so callers can skip
/// recomputing it when they already have the value.
[[nodiscard]] float ComputeVfxCameraAspect(const Vec2& screenSize) noexcept;

} // namespace uz
