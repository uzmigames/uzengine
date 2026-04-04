/// UzEngine KTX2 Texture Loader
///
/// Loads KTX2 textures (UASTC HDR, UASTC LDR, ETC1S) via basis_universal's
/// ktx2_transcoder. Transcodes to RGBA16Float for GPU upload.
///
/// @ref Khronos KTX2 specification
/// @ref Basis Universal — GPU texture supercompression

#include "texture_loader.h"
#include "upload_context.h"
#include "log.h"
#include "profiler.h"

#define BASISU_NO_ITERATOR_DEBUG_LEVEL
#include <basisu_transcoder.h>

#include <cmath>
#include <cstring>
#include <memory>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

static bool s_basisInitialized = false;
static void EnsureBasisInit() {
    if (!s_basisInitialized) {
        basist::basisu_transcoder_init();
        s_basisInitialized = true;
    }
}

static uint16_t FloatToHalf(float value) noexcept {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(float));
    const uint32_t sign     = (bits >> 16) & 0x8000u;
    const int32_t  exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127;
    const uint32_t mantissa = bits & 0x007FFFFFu;
    if (exponent <= -15) {
        if (exponent < -24) return static_cast<uint16_t>(sign);
        const uint32_t shift = static_cast<uint32_t>(-14 - exponent);
        const uint32_t denorm = (0x00800000u | mantissa) >> shift;
        return static_cast<uint16_t>(sign | (denorm >> 13));
    }
    if (exponent < 16)
        return static_cast<uint16_t>(sign | static_cast<uint32_t>((exponent + 15) << 10) | (mantissa >> 13));
    if (exponent == 128 && mantissa != 0)
        return static_cast<uint16_t>(sign | 0x7C00u | (mantissa >> 13) | 1u);
    return static_cast<uint16_t>(sign | 0x7C00u);
}

TextureLoadResult TextureLoader::LoadKTX2FromMemory(
    IRHIDevice* device,
    const void* data,
    size_t dataSize,
    const char* debugName)
{
    UZ_PROFILE_FUNCTION();

    if (!device || !data || dataSize == 0) {
        UZ_LOG(Renderer, Error, "LoadKTX2FromMemory: invalid args");
        return {};
    }

    EnsureBasisInit();

    basist::ktx2_transcoder transcoder;

    if (!transcoder.init(static_cast<const uint8_t*>(data), static_cast<uint32_t>(dataSize))) {
        UZ_LOG(Renderer, Error, "LoadKTX2FromMemory: init failed for '{}'", debugName);
        return {};
    }

    const uint32_t width  = transcoder.get_width();
    const uint32_t height = transcoder.get_height();
    const uint32_t levels = transcoder.get_levels();
    const bool isHDR = transcoder.is_hdr();

    const uint32_t faces = transcoder.get_faces();

    UZ_LOG(Renderer, Info, "KTX2: '{}' — {}x{}, {} levels, {} faces, HDR={}",
           debugName, width, height, levels, faces, isHDR);

    if (width == 0 || height == 0) { transcoder.clear(); return {}; }

    // ── PMREM atlas detection ──────────────────────────────────────────
    // Three.js CubeUV PMREM atlas: face_count=1, width = 3 * faceSize,
    // height ≈ 4 * faceSize (LOD groups stacked vertically).
    // @ref Three.js PMREMGenerator — octahedral atlas packing
    bool isPMREM = false;
    uint32_t pmremFaceSize = 0;
    uint32_t pmremLodCount = 0;
    if (faces == 1 && (width % 3) == 0) {
        const uint32_t faceSize = width / 3;
        // Validate: height should be approximately sum of 2*faceSize geometric series
        // 2*faceSize + 2*(faceSize/2) + 2*(faceSize/4) + ... ≈ 4*faceSize
        if (height >= faceSize * 3 && height <= faceSize * 5 && faceSize >= 64) {
            isPMREM = true;
            pmremFaceSize = faceSize;
            // Count LOD levels: sum of (2 * faceSize >> lod) until height is filled
            uint32_t accHeight = 0;
            for (uint32_t lod = 0; lod < 16; ++lod) {
                uint32_t lodFace = faceSize >> lod;
                if (lodFace < 4) break;
                accHeight += lodFace * 2; // 2 rows per LOD (3×2 face grid)
                pmremLodCount = lod + 1;
                if (accHeight >= height) break;
            }
            UZ_LOG(Renderer, Info, "KTX2: '{}' detected as PMREM atlas — face={}px, {} LODs",
                   debugName, pmremFaceSize, pmremLodCount);
        }
    }

    if (!transcoder.start_transcoding()) {
        UZ_LOG(Renderer, Error, "LoadKTX2FromMemory: start_transcoding failed");
        transcoder.clear();
        return {};
    }

    const uint32_t pixelCount = width * height;
    constexpr size_t kBytesPerTexelF16 = 8;

    // Transcode to RGBA half-float (HDR) or RGBA32 (LDR → convert to F16)
    auto halfPixels = std::make_unique<uint16_t[]>(pixelCount * 4);

    if (isHDR) {
        if (!transcoder.transcode_image_level(0, 0, 0,
                halfPixels.get(), pixelCount,
                basist::transcoder_texture_format::cTFRGBA_HALF)) {
            UZ_LOG(Renderer, Error, "LoadKTX2FromMemory: transcode HDR failed");
            transcoder.clear();
            return {};
        }
    } else {
        // LDR: decode to RGBA8 then convert
        auto rgba8 = std::make_unique<uint8_t[]>(pixelCount * 4);
        if (!transcoder.transcode_image_level(0, 0, 0,
                rgba8.get(), pixelCount,
                basist::transcoder_texture_format::cTFRGBA32)) {
            UZ_LOG(Renderer, Error, "LoadKTX2FromMemory: transcode LDR failed");
            transcoder.clear();
            return {};
        }
        for (uint32_t i = 0; i < pixelCount * 4; ++i) {
            float v = static_cast<float>(rgba8[i]) / 255.0f;
            v = (v <= 0.04045f) ? (v / 12.92f) : std::pow((v + 0.055f) / 1.055f, 2.4f);
            halfPixels[i] = FloatToHalf(v);
        }
    }

    transcoder.clear();

    // GPU upload
    auto* cmdList = device->CreateCommandList(RHIQueueType::Graphics);
    if (!cmdList) return {};
    cmdList->Begin();

    RHITextureDesc desc{};
    desc.width     = width;
    desc.height    = height;
    desc.mipLevels = 1;
    desc.format    = RHIFormat::RGBA16Float;
    desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    desc.debugName = debugName;

    auto texHandle = device->CreateTexture(desc);
    if (!texHandle.IsValid()) {
        cmdList->End();
        device->DestroyCommandList(cmdList);
        return {};
    }

    UploadContext upload(device);
    const uint64_t uploadSize = static_cast<uint64_t>(pixelCount) * kBytesPerTexelF16;
    if (!upload.UploadTexture(cmdList, texHandle, halfPixels.get(), uploadSize, 0)) {
        UZ_LOG(Renderer, Error, "LoadKTX2FromMemory: upload failed");
        device->DestroyTexture(texHandle);
        cmdList->End();
        device->DestroyCommandList(cmdList);
        return {};
    }

    // Transition CopyDst → ShaderResource
    RHIBarrierDesc barrier{};
    barrier.texture     = texHandle;
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter  = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    cmdList->End();
    cmdList->Execute();
    device->WaitIdle();
    upload.ReleaseStaging();
    device->DestroyCommandList(cmdList);

    TextureLoadResult result;
    result.handle   = texHandle;
    result.srvIndex = device->GetTextureSRVIndex(texHandle);
    result.width    = width;
    result.height   = height;
    result.isPMREM       = isPMREM;
    result.pmremFaceSize = pmremFaceSize;
    result.pmremLodCount = pmremLodCount;

    UZ_LOG(Renderer, Info, "KTX2 loaded: '{}' {}x{} HDR={} SRV={} PMREM={}",
           debugName, width, height, isHDR, result.srvIndex, isPMREM);
    return result;
}

} // namespace uz
