/// LTC lookup tables for rectangular area lights (phase3a 3.33.0 stage 1).
///
/// Two 64x64 textures of the Linearly Transformed Cosine fit of GGX (Heitz, Dupuy, Hill, Neubelt
/// 2016, "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines"), baked offline by
/// `scripts/rendering/bake_ltc_lut.py` (a numpy re-implementation of the authors' fitter, cross-checked
/// against the published tables) and embedded as `ltc_lut_data.inc`:
///
/// - matrix table, RGBA32F: M^-1 normalised by its middle entry, packed as the reference ltc_1
///   (invM[0][0], invM[0][2], invM[2][0], invM[2][2] in column-major indexing); rebuilt by
///   `LtcInverseMatrix` in ltc.hlsli. 32-bit because the near-mirror columns hold entries up to ~1e5.
/// - magnitude table, RG32F: x = directional albedo of GGX without Fresnel, y = its (1 - VoH)^5
///   weighted albedo (the Schlick split: F0 * x + (F90 - F0) * y).
///
/// Texel (x = perceptual roughness, y = sqrt(1 - NoV)); the shader samples texel centres at both ends.
/// Created and uploaded on the first frame that carries a rect light (frame_setup.cpp) and reached
/// through the bindless SRV indices in LightingCB (ltcMatrixSrv / ltcMagnitudeSrv). Nothing is
/// created for scenes without a rect light: their resource layout, LightingCB and lighting DXIL stay
/// byte-identical to the engine before this feature (verified on reflection-reference frame 95).
///
/// The same file holds `RectLightBuffer`, the per-frame StructuredBuffer<RectLightData> the
/// UZ_RECT_LIGHTS lighting permutation reads (LightingCB::rectLightsSrv), created lazily too.

#pragma once

#include "rhi_types.h"
#include "constant_buffers.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class UploadContext;

/// Side of the square LTC tables.
static constexpr uint32_t kLtcLutSize = 64;

/// (invM[0][0], invM[0][2], invM[2][0], invM[2][2]) per texel, row t (sqrt(1 - NoV)), column a (roughness).
extern const float kLtcMatrixTable[kLtcLutSize * kLtcLutSize * 4];

/// (magnitude, fresnel) per texel, same layout.
extern const float kLtcMagnitudeTable[kLtcLutSize * kLtcLutSize * 2];

/// The two LUT textures and their bindless SRV indices.
struct LtcLut {
    RHITextureHandle matrix;                 ///< 64x64 RGBA32F inverse-matrix table.
    RHITextureHandle magnitude;              ///< 64x64 RG32F (magnitude, fresnel) table.
    uint32_t         matrixSrv    = UINT32_MAX;
    uint32_t         magnitudeSrv = UINT32_MAX;
    bool             uploaded     = false;   ///< True once the tables were copied to the GPU.

    /// Both textures exist, were uploaded, and have SRVs.
    [[nodiscard]] bool IsReady() const noexcept {
        return uploaded && matrixSrv != UINT32_MAX && magnitudeSrv != UINT32_MAX;
    }
};

/// Creates the two textures (ShaderResource | CopyDst) and resolves their SRV indices.
/// Returns false (and leaves `lut` empty) when either allocation fails; the area-light branch then
/// stays disabled (the lighting CB keeps UINT32_MAX SRVs).
bool CreateLtcLut(IRHIDevice* device, LtcLut& lut);

/// Records the upload of both embedded tables on `cmdList` through `upload` (staging buffers stay
/// owned by the context until `ReleaseStaging`). The caller owns the resource-state transitions
/// (CopyDst before, ShaderResource after). Sets `lut.uploaded` on success.
bool UploadLtcLut(IRHICommandList* cmdList, UploadContext& upload, LtcLut& lut);

/// Destroys the textures and resets `lut`.
void DestroyLtcLut(IRHIDevice* device, LtcLut& lut);

/// Frames in flight the rect light buffer is ringed over (the frame fence covers N-3).
static constexpr uint32_t kRectLightBufferFrames = 3;

/// Upload-heap StructuredBuffer<RectLightData> per frame in flight (kMaxRectLights entries each),
/// written by PopulatePerFrameConstants and read through its bindless SRV.
struct RectLightBuffer {
    RHIBufferHandle buffers[kRectLightBufferFrames];
    uint32_t        srvs[kRectLightBufferFrames] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};

    [[nodiscard]] bool IsValid() const noexcept { return buffers[0].IsValid(); }
};

/// Creates the ring (returns false and leaves it empty on failure).
bool CreateRectLightBuffer(IRHIDevice* device, RectLightBuffer& ring);

/// Copies `count` lights into the slot of `frameIndex` and returns that slot's SRV (UINT32_MAX on failure).
uint32_t WriteRectLightBuffer(IRHIDevice* device, RectLightBuffer& ring, uint64_t frameIndex,
                              const RectLightData* lights, uint32_t count);

/// Destroys the buffers and resets `ring`.
void DestroyRectLightBuffer(IRHIDevice* device, RectLightBuffer& ring);

} // namespace uz
