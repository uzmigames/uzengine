#include "ffmpeg_encoder.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

// Windows headers — order matters
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <codecapi.h>
#include <strmif.h>      // ICodecAPI interface
#include <combaseapi.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace {

// CLSID for the built-in Microsoft H.264 encoder MFT
// {6ca50344-051a-4ded-9779-a43305165e35}
const CLSID CLSID_CMSH264EncoderMFT = {
    0x6ca50344, 0x051a, 0x4ded,
    {0x97, 0x79, 0xa4, 0x33, 0x05, 0x16, 0x5e, 0x35}
};

// H.264 NAL unit types
constexpr uint8_t kNalTypeSPS = 7;
constexpr uint8_t kNalTypePPS = 8;

// 100-nanosecond units per second (MF time base)
constexpr int64_t kMFTimeBase = 10'000'000LL;

/// Safe release helper for COM pointers stored as void*.
template <typename T>
void safeRelease(void*& ptr) {
    if (ptr) {
        static_cast<T*>(ptr)->Release();
        ptr = nullptr;
    }
}

/// Scan an Annex-B bitstream for NAL units of the given type.
/// Returns the raw NAL unit bytes (without the start code).
[[nodiscard]] bool findNALUnit(const uint8_t* data, size_t size, uint8_t nalType,
                               std::vector<uint8_t>& out) {
    // Look for 3-byte (00 00 01) or 4-byte (00 00 00 01) start codes
    for (size_t i = 0; i + 2 < size; ++i) {
        bool threeByteStart = (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1);
        bool fourByteStart  = (i + 3 < size && data[i] == 0 && data[i + 1] == 0 &&
                               data[i + 2] == 0 && data[i + 3] == 1);

        size_t nalStart = 0;
        if (fourByteStart) {
            nalStart = i + 4;
        } else if (threeByteStart) {
            nalStart = i + 3;
        } else {
            continue;
        }

        if (nalStart >= size) {
            continue;
        }

        const uint8_t currentType = data[nalStart] & 0x1F;
        if (currentType != nalType) {
            // Advance past this start code
            if (fourByteStart) {
                i += 3;
            } else {
                i += 2;
            }
            continue;
        }

        // Find end of this NAL unit (next start code or end of data)
        size_t nalEnd = size;
        for (size_t j = nalStart + 1; j + 2 < size; ++j) {
            if (data[j] == 0 && data[j + 1] == 0 &&
                (data[j + 2] == 1 || (j + 3 < size && data[j + 2] == 0 && data[j + 3] == 1))) {
                nalEnd = j;
                break;
            }
        }

        out.assign(data + nalStart, data + nalEnd);
        return true;
    }
    return false;
}

/// Convert BGRA pixel data to NV12 in-place into the destination buffer.
/// BGRA layout: B=byte0, G=byte1, R=byte2, A=byte3 (DX12 readback format).
/// NV12 layout: Y plane (width*height), then interleaved UV plane ((width/2)*(height/2)*2).
/// Uses full-range BT.601 coefficients for simplicity.
void convertBGRAtoNV12(const uint8_t* bgra, uint8_t* nv12,
                       uint32_t width, uint32_t height) {
    uint8_t* yPlane  = nv12;
    uint8_t* uvPlane = nv12 + width * height;

    // Y plane: one luma sample per pixel
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t srcIdx = (y * width + x) * 4;
            const uint8_t b = bgra[srcIdx + 0];
            const uint8_t g = bgra[srcIdx + 1];
            const uint8_t r = bgra[srcIdx + 2];
            // Y = (77*R + 150*G + 29*B) >> 8  (full range, 0..255)
            const int32_t luma = (77 * r + 150 * g + 29 * b) >> 8;
            yPlane[y * width + x] = static_cast<uint8_t>(std::clamp(luma, 0, 255));
        }
    }

    // UV plane: 2x2 subsampled, interleaved U,V pairs
    const uint32_t halfW = width / 2;
    const uint32_t halfH = height / 2;
    for (uint32_t y = 0; y < halfH; ++y) {
        for (uint32_t x = 0; x < halfW; ++x) {
            // Average the 2x2 block of source pixels
            int32_t sumR = 0, sumG = 0, sumB = 0;
            for (uint32_t dy = 0; dy < 2; ++dy) {
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    const uint32_t srcIdx = ((y * 2 + dy) * width + (x * 2 + dx)) * 4;
                    sumB += bgra[srcIdx + 0];
                    sumG += bgra[srcIdx + 1];
                    sumR += bgra[srcIdx + 2];
                }
            }
            const int32_t avgR = sumR / 4;
            const int32_t avgG = sumG / 4;
            const int32_t avgB = sumB / 4;

            // U = 128 + ((-43*R - 85*G + 128*B) >> 8)
            const int32_t u = 128 + ((-43 * avgR - 85 * avgG + 128 * avgB) >> 8);
            // V = 128 + ((128*R - 107*G - 21*B) >> 8)
            const int32_t v = 128 + ((128 * avgR - 107 * avgG - 21 * avgB) >> 8);

            const uint32_t uvIdx = (y * halfW + x) * 2;
            uvPlane[uvIdx + 0] = static_cast<uint8_t>(std::clamp(u, 0, 255));
            uvPlane[uvIdx + 1] = static_cast<uint8_t>(std::clamp(v, 0, 255));
        }
    }
}

/// Create an IMFSample containing pixel data for MF input.
/// When useNV12 is true, converts BGRA->NV12 before filling the buffer.
/// When useNV12 is false, swizzles RGBA->BGRA for ARGB32 input.
[[nodiscard]] HRESULT createInputSample(const uint8_t* rgbaData, uint32_t width, uint32_t height,
                                        int64_t timestamp, int64_t duration, bool useNV12,
                                        IMFSample** outSample) {
    // NV12: width * height * 3/2;  ARGB32: width * height * 4
    const DWORD bufferSize = useNV12
        ? (width * height * 3 / 2)
        : (width * height * 4);

    IMFMediaBuffer* buffer = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(bufferSize, &buffer);
    if (FAILED(hr)) {
        return hr;
    }

    BYTE* bufferData = nullptr;
    hr = buffer->Lock(&bufferData, nullptr, nullptr);
    if (FAILED(hr)) {
        buffer->Release();
        return hr;
    }

    if (useNV12) {
        // DX12 readback is BGRA. Convert to NV12 for the MF encoder.
        convertBGRAtoNV12(rgbaData, bufferData, width, height);
    } else {
        // MF expects top-down ARGB32 (which is BGRA in memory).
        // Our DX12 readback is already BGRA, so copy directly.
        // Note: the original code swizzled R<->B assuming RGBA input, but DX12
        // DXGI_FORMAT_B8G8R8A8_UNORM readback is already BGRA — just copy.
        std::memcpy(bufferData, rgbaData, width * height * 4);
    }

    buffer->Unlock();
    hr = buffer->SetCurrentLength(bufferSize);
    if (FAILED(hr)) {
        buffer->Release();
        return hr;
    }

    IMFSample* sample = nullptr;
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) {
        buffer->Release();
        return hr;
    }

    hr = sample->AddBuffer(buffer);
    buffer->Release(); // sample now owns the buffer
    if (FAILED(hr)) {
        sample->Release();
        return hr;
    }

    sample->SetSampleTime(timestamp);
    sample->SetSampleDuration(duration);

    *outSample = sample;
    return S_OK;
}

} // anonymous namespace

namespace uz {

FFmpegEncoder::FFmpegEncoder() = default;

FFmpegEncoder::~FFmpegEncoder() {
    shutdown();
}

bool FFmpegEncoder::isAvailable() {
    // Temporarily init COM + MF to probe encoder availability
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInitialized = SUCCEEDED(hr) || hr == S_FALSE; // S_FALSE = already init'd
    if (hr == RPC_E_CHANGED_MODE) {
        // COM already initialized with a different threading model — that's fine
        comInitialized = true;
    }

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        if (comInitialized) {
            CoUninitialize();
        }
        return false;
    }

    IMFTransform* transform = nullptr;
    hr = CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&transform));
    bool available = SUCCEEDED(hr);
    if (transform) {
        transform->Release();
    }

    MFShutdown();
    if (comInitialized) {
        CoUninitialize();
    }

    return available;
}

bool FFmpegEncoder::initialize(const EncoderConfig& config) {
    if (initialized_) {
        fprintf(stderr, "[FFmpegEncoder] Already initialized\n");
        return false;
    }

    width_  = config.width;
    height_ = config.height;
    fps_    = config.fps;
    pts_    = 0;
    frameDuration_ = kMFTimeBase / static_cast<int64_t>(fps_);

    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        fprintf(stderr, "[FFmpegEncoder] CoInitializeEx failed: 0x%08lX\n", hr);
        return false;
    }

    // Initialize Media Foundation
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        fprintf(stderr, "[FFmpegEncoder] MFStartup failed: 0x%08lX\n", hr);
        return false;
    }
    mfStarted_ = true;

    if (!configureEncoder(config)) {
        shutdown();
        return false;
    }

    initialized_ = true;
    printf("[FFmpegEncoder] Initialized (%ux%u @ %u fps, %u bps)\n",
           width_, height_, fps_, config.bitrate);
    return true;
}

bool FFmpegEncoder::configureEncoder(const EncoderConfig& config) {
    // Create the H.264 encoder MFT
    IMFTransform* mft = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&mft));
    if (FAILED(hr)) {
        fprintf(stderr, "[FFmpegEncoder] CoCreateInstance(H264 MFT) failed: 0x%08lX\n", hr);
        return false;
    }
    transform_ = mft;

    // --- Configure output type (H.264) first, as MF requires output before input ---
    IMFMediaType* outType = nullptr;
    hr = MFCreateMediaType(&outType);
    if (FAILED(hr)) {
        fprintf(stderr, "[FFmpegEncoder] MFCreateMediaType (output) failed\n");
        return false;
    }
    outputType_ = outType;

    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, width_, height_);
    MFSetAttributeRatio(outType, MF_MT_FRAME_RATE, fps_, 1);
    outType->SetUINT32(MF_MT_AVG_BITRATE, config.bitrate);
    outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeRatio(outType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

    // Try profile/level combos from most to least capable.
    // Main@4.2 supports 1280x720@60fps; Baseline@4.0 supports 1280x720@30fps.
    // The MFT on some systems rejects Baseline above level 3.1, so we fall
    // through to progressively lower settings until one is accepted.
    struct ProfileLevel { UINT32 profile; UINT32 level; const char* name; };
    const ProfileLevel candidates[] = {
        { eAVEncH264VProfile_Main, eAVEncH264VLevel4_2, "Main@4.2" },
        { eAVEncH264VProfile_Main, eAVEncH264VLevel4,   "Main@4.0" },
        { eAVEncH264VProfile_Base, eAVEncH264VLevel4_2, "Baseline@4.2" },
        { eAVEncH264VProfile_Base, eAVEncH264VLevel4,   "Baseline@4.0" },
        { eAVEncH264VProfile_Base, eAVEncH264VLevel3_1, "Baseline@3.1" },
    };
    hr = E_FAIL;
    for (const auto& pl : candidates) {
        outType->SetUINT32(MF_MT_MPEG2_PROFILE, pl.profile);
        outType->SetUINT32(MF_MT_MPEG2_LEVEL,   pl.level);
        hr = mft->SetOutputType(0, outType, 0);
        if (SUCCEEDED(hr)) {
            fprintf(stderr, "[FFmpegEncoder] Using H.264 %s\n", pl.name);
            break;
        }
    }
    if (FAILED(hr)) {
        fprintf(stderr, "[FFmpegEncoder] SetOutputType failed all profile/level combos: 0x%08lX\n", hr);
        return false;
    }

    // --- Configure input type (ARGB32 = BGRA in memory) ---
    IMFMediaType* inType = nullptr;
    hr = MFCreateMediaType(&inType);
    if (FAILED(hr)) {
        fprintf(stderr, "[FFmpegEncoder] MFCreateMediaType (input) failed\n");
        return false;
    }
    inputType_ = inType;

    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
    MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, width_, height_);
    MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, fps_, 1);
    inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeRatio(inType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    // Stride: 4 bytes per pixel, top-down (negative for bottom-up, positive for top-down)
    inType->SetUINT32(MF_MT_DEFAULT_STRIDE, width_ * 4);

    hr = mft->SetInputType(0, inType, 0);
    if (FAILED(hr)) {
        fprintf(stderr, "[FFmpegEncoder] SetInputType (ARGB32) failed: 0x%08lX, trying NV12\n", hr);
        // Try NV12 as fallback input format
        inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        inType->SetUINT32(MF_MT_DEFAULT_STRIDE, width_);
        hr = mft->SetInputType(0, inType, 0);
        if (FAILED(hr)) {
            fprintf(stderr, "[FFmpegEncoder] SetInputType (NV12 fallback) also failed: 0x%08lX\n", hr);
            return false;
        }
        useNV12_ = true;
        fprintf(stderr, "[FFmpegEncoder] Using NV12 input -- BGRA->NV12 conversion enabled\n");
    }

    // Configure encoder via ICodecAPI (bitrate, low latency)
    ICodecAPI* codecApi = nullptr;
    hr = mft->QueryInterface(__uuidof(ICodecAPI), reinterpret_cast<void**>(&codecApi));
    if (SUCCEEDED(hr)) {
        VARIANT val;
        VariantInit(&val);

        // Set CBR rate control
        val.vt = VT_UI4;
        val.ulVal = eAVEncCommonRateControlMode_CBR;
        codecApi->SetValue(&CODECAPI_AVEncCommonRateControlMode, &val);

        // Set bitrate
        val.vt = VT_UI4;
        val.ulVal = config.bitrate;
        codecApi->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &val);

        // Enable low-latency mode
        val.vt = VT_BOOL;
        val.boolVal = VARIANT_TRUE;
        codecApi->SetValue(&CODECAPI_AVLowLatencyMode, &val);

        codecApi->Release();
    }

    // Start streaming
    hr = mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    if (FAILED(hr)) {
        fprintf(stderr, "[FFmpegEncoder] MFT_MESSAGE_NOTIFY_BEGIN_STREAMING failed: 0x%08lX\n", hr);
        return false;
    }

    hr = mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    if (FAILED(hr)) {
        fprintf(stderr, "[FFmpegEncoder] MFT_MESSAGE_NOTIFY_START_OF_STREAM failed: 0x%08lX\n", hr);
        return false;
    }

    return true;
}

bool FFmpegEncoder::encode(const uint8_t* rgbaData, uint32_t width, uint32_t height,
                           std::vector<uint8_t>& outNALUs) {
    if (!initialized_) {
        fprintf(stderr, "[FFmpegEncoder] Not initialized\n");
        return false;
    }

    if (width != width_ || height != height_) {
        fprintf(stderr, "[FFmpegEncoder] Resolution mismatch: expected %ux%u, got %ux%u\n",
                width_, height_, width, height);
        return false;
    }

    auto* mft = static_cast<IMFTransform*>(transform_);
    outNALUs.clear();

    // Create input sample — converts BGRA->NV12 when useNV12_ is active
    const int64_t timestamp = pts_ * frameDuration_;
    IMFSample* inputSample = nullptr;
    HRESULT hr = createInputSample(rgbaData, width, height, timestamp, frameDuration_, useNV12_, &inputSample);
    if (FAILED(hr)) {
        fprintf(stderr, "[FFmpegEncoder] createInputSample failed: 0x%08lX\n", hr);
        return false;
    }

    // Feed input to encoder
    hr = mft->ProcessInput(0, inputSample, 0);
    inputSample->Release();
    if (FAILED(hr)) {
        fprintf(stderr, "[FFmpegEncoder] ProcessInput failed: 0x%08lX\n", hr);
        return false;
    }

    ++pts_;

    // Drain output — may produce 0 or more output samples
    bool gotOutput = false;
    for (;;) {
        // Query output stream info for buffer requirements
        MFT_OUTPUT_STREAM_INFO streamInfo{};
        hr = mft->GetOutputStreamInfo(0, &streamInfo);
        if (FAILED(hr)) {
            break;
        }

        // Allocate output sample + buffer
        IMFSample* outputSample = nullptr;
        IMFMediaBuffer* outputBuffer = nullptr;
        bool weAllocated = false;

        if (!(streamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
            // We must provide the output sample
            const DWORD outBufSize = (streamInfo.cbSize > 0) ? streamInfo.cbSize : (width_ * height_ * 4);
            hr = MFCreateMemoryBuffer(outBufSize, &outputBuffer);
            if (FAILED(hr)) {
                break;
            }
            hr = MFCreateSample(&outputSample);
            if (FAILED(hr)) {
                outputBuffer->Release();
                break;
            }
            outputSample->AddBuffer(outputBuffer);
            outputBuffer->Release(); // sample owns it now
            weAllocated = true;
        }

        MFT_OUTPUT_DATA_BUFFER outputData{};
        outputData.dwStreamID = 0;
        outputData.pSample = outputSample;
        DWORD status = 0;

        hr = mft->ProcessOutput(0, 1, &outputData, &status);

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            // Encoder needs more frames before producing output — normal for B-frames or startup
            if (weAllocated && outputSample) {
                outputSample->Release();
            }
            break;
        }

        if (FAILED(hr)) {
            fprintf(stderr, "[FFmpegEncoder] ProcessOutput failed: 0x%08lX\n", hr);
            if (weAllocated && outputSample) {
                outputSample->Release();
            }
            if (outputData.pEvents) {
                outputData.pEvents->Release();
            }
            break;
        }

        // Extract encoded data from output sample
        IMFSample* resultSample = outputData.pSample;
        if (resultSample) {
            IMFMediaBuffer* resultBuffer = nullptr;
            hr = resultSample->ConvertToContiguousBuffer(&resultBuffer);
            if (SUCCEEDED(hr)) {
                BYTE* bufData = nullptr;
                DWORD bufLen = 0;
                hr = resultBuffer->Lock(&bufData, nullptr, &bufLen);
                if (SUCCEEDED(hr) && bufLen > 0) {
                    // Append encoded NAL units
                    const size_t prevSize = outNALUs.size();
                    outNALUs.resize(prevSize + bufLen);
                    std::memcpy(outNALUs.data() + prevSize, bufData, bufLen);
                    gotOutput = true;

                    // Extract SPS/PPS from the first frame that contains them
                    if (codecConfig_.sps.empty()) {
                        parseNALUnits(bufData, bufLen);
                    }
                }
                resultBuffer->Unlock();
                resultBuffer->Release();
            }
        }

        if (outputData.pEvents) {
            outputData.pEvents->Release();
        }

        // If the MFT provided the sample itself, release it
        if (!weAllocated && outputData.pSample) {
            outputData.pSample->Release();
        } else if (weAllocated && outputSample) {
            outputSample->Release();
        }
    }

    // It's valid for the first few frames to produce no output (encoder buffering)
    return gotOutput || pts_ <= 2;
}

bool FFmpegEncoder::parseNALUnits(const uint8_t* data, size_t size) {
    std::vector<uint8_t> sps;
    std::vector<uint8_t> pps;

    if (findNALUnit(data, size, kNalTypeSPS, sps) &&
        findNALUnit(data, size, kNalTypePPS, pps)) {
        codecConfig_.sps = std::move(sps);
        codecConfig_.pps = std::move(pps);

        // Extract profile and level from SPS (bytes 1 and 3 after NAL type byte)
        if (codecConfig_.sps.size() >= 4) {
            codecConfig_.profileIdc = codecConfig_.sps[1]; // profile_idc
            codecConfig_.levelIdc   = codecConfig_.sps[3]; // level_idc
        }

        printf("[FFmpegEncoder] Extracted SPS (%zu bytes) + PPS (%zu bytes), "
               "profile=0x%02X level=0x%02X\n",
               codecConfig_.sps.size(), codecConfig_.pps.size(),
               codecConfig_.profileIdc, codecConfig_.levelIdc);
        return true;
    }
    return false;
}

bool FFmpegEncoder::extractCodecConfig() {
    // Codec config is extracted lazily from the first encoded frame
    // that contains SPS/PPS NAL units. This method is a no-op placeholder
    // kept for interface symmetry.
    return !codecConfig_.sps.empty();
}

void FFmpegEncoder::shutdown() {
    if (!initialized_ && !mfStarted_) {
        return;
    }

    auto* mft = static_cast<IMFTransform*>(transform_);
    if (mft) {
        // Signal end of stream so the encoder flushes
        mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    }

    safeRelease<IMFMediaType>(inputType_);
    safeRelease<IMFMediaType>(outputType_);
    safeRelease<IMFTransform>(transform_);

    if (mfStarted_) {
        MFShutdown();
        mfStarted_ = false;
    }

    width_       = 0;
    height_      = 0;
    fps_         = 0;
    pts_         = 0;
    initialized_ = false;
    useNV12_     = false;
    codecConfig_ = {};

    printf("[FFmpegEncoder] Shutdown complete\n");
}

} // namespace uz
