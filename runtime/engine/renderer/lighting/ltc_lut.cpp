/// LTC lookup tables for rectangular area lights — creation, upload and the embedded data.

#include "ltc_lut.h"

#include "rhi_device.h"
#include "rhi_command_list.h"
#include "upload_context.h"
#include "log.h"

#include <algorithm>
#include <cstring>

namespace uz {

// The baked tables (scripts/rendering/bake_ltc_lut.py). 64 * 64 * (4 + 2) floats, 96 KB.
#include "ltc_lut_data.inc"

static_assert(sizeof(kLtcMatrixTable) == kLtcLutSize * kLtcLutSize * 4 * sizeof(float), "LTC matrix table size");
static_assert(sizeof(kLtcMagnitudeTable) == kLtcLutSize * kLtcLutSize * 2 * sizeof(float), "LTC magnitude table size");

bool CreateLtcLut(IRHIDevice* device, LtcLut& lut)
{
    if (!device) return false;

    RHITextureDesc desc{};
    desc.width  = kLtcLutSize;
    desc.height = kLtcLutSize;
    desc.usage  = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;

    desc.format    = RHIFormat::RGBA32Float;
    desc.debugName = "LtcMatrixLut";
    lut.matrix = device->CreateTexture(desc);

    desc.format    = RHIFormat::RG32Float;
    desc.debugName = "LtcMagnitudeLut";
    lut.magnitude = device->CreateTexture(desc);

    lut.matrixSrv    = lut.matrix.IsValid()    ? device->GetTextureSRVIndex(lut.matrix)    : UINT32_MAX;
    lut.magnitudeSrv = lut.magnitude.IsValid() ? device->GetTextureSRVIndex(lut.magnitude) : UINT32_MAX;
    lut.uploaded     = false;

    if (lut.matrixSrv == UINT32_MAX || lut.magnitudeSrv == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "LtcLut: texture creation failed (matrix SRV={}, magnitude SRV={}); "
               "rect area lights disabled", lut.matrixSrv, lut.magnitudeSrv);
        DestroyLtcLut(device, lut);
        return false;
    }
    return true;
}

bool UploadLtcLut(IRHICommandList* cmdList, UploadContext& upload, LtcLut& lut)
{
    if (!cmdList || !lut.matrix.IsValid() || !lut.magnitude.IsValid()) return false;
    const bool ok = upload.UploadTexture(cmdList, lut.matrix, kLtcMatrixTable, sizeof(kLtcMatrixTable))
                 && upload.UploadTexture(cmdList, lut.magnitude, kLtcMagnitudeTable, sizeof(kLtcMagnitudeTable));
    lut.uploaded = ok;
    if (!ok) {
        UZ_LOG(Renderer, Error, "LtcLut: table upload failed; rect area lights disabled");
    }
    return ok;
}

void DestroyLtcLut(IRHIDevice* device, LtcLut& lut)
{
    if (device) {
        if (lut.matrix.IsValid())    device->DestroyTexture(lut.matrix);
        if (lut.magnitude.IsValid()) device->DestroyTexture(lut.magnitude);
    }
    lut = LtcLut{};
}

bool CreateRectLightBuffer(IRHIDevice* device, RectLightBuffer& ring)
{
    if (!device) return false;
    // Upload heap, CPU-written each frame and read by the lighting pass (ReflectionProbeRenderer's pattern).
    RHIBufferDesc desc{};
    desc.sizeBytes = kMaxRectLights * sizeof(RectLightData);
    desc.stride    = sizeof(RectLightData);
    desc.usage     = RHIBufferUsage::StructuredBuffer;
    desc.memory    = RHIMemoryType::Upload;
    desc.debugName = "RectLightBuffer";
    for (uint32_t i = 0; i < kRectLightBufferFrames; ++i) {
        ring.buffers[i] = device->CreateBuffer(desc);
        ring.srvs[i]    = ring.buffers[i].IsValid() ? device->GetBufferSRVIndex(ring.buffers[i]) : UINT32_MAX;
        if (ring.srvs[i] == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "RectLightBuffer: buffer {} creation failed; rect area lights disabled", i);
            DestroyRectLightBuffer(device, ring);
            return false;
        }
    }
    return true;
}

uint32_t WriteRectLightBuffer(IRHIDevice* device, RectLightBuffer& ring, uint64_t frameIndex,
                              const RectLightData* lights, uint32_t count)
{
    if (!device || !ring.IsValid() || !lights || count == 0) return UINT32_MAX;
    const uint32_t slot = static_cast<uint32_t>(frameIndex % kRectLightBufferFrames);
    void* mapped = device->MapBuffer(ring.buffers[slot]);
    if (!mapped) return UINT32_MAX;
    std::memcpy(mapped, lights, std::min(count, kMaxRectLights) * sizeof(RectLightData));
    device->UnmapBuffer(ring.buffers[slot]);
    return ring.srvs[slot];
}

void DestroyRectLightBuffer(IRHIDevice* device, RectLightBuffer& ring)
{
    if (device) {
        for (auto& buffer : ring.buffers) {
            if (buffer.IsValid()) device->DestroyBuffer(buffer);
        }
    }
    ring = RectLightBuffer{};
}

} // namespace uz
