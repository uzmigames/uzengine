#include "nvenc_encoder.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>

// Full NVENC implementation requires NVIDIA Video Codec SDK headers from
// https://developer.nvidia.com/video-codec-sdk
// Currently stubbed -- the encoder factory detects isAvailable()==true but
// initialize()==false and falls through to the FFmpeg/x264 software encoder.

namespace {

/// Name of the 64-bit NVENC runtime library shipped with NVIDIA drivers.
constexpr const wchar_t* kNvEncDllName = L"nvEncodeAPI64.dll";

/// Name of the entry point that creates the NVENC function table.
constexpr const char* kNvEncCreateInstanceProc = "NvEncodeAPICreateInstance";

/// Attempt to load the NVENC DLL and verify the entry point exists.
/// Returns the HMODULE on success (caller must FreeLibrary), or nullptr.
[[nodiscard]] HMODULE probeNvEncLibrary() {
    HMODULE lib = LoadLibraryW(kNvEncDllName);
    if (!lib) {
        return nullptr;
    }

    auto proc = GetProcAddress(lib, kNvEncCreateInstanceProc);
    if (!proc) {
        FreeLibrary(lib);
        return nullptr;
    }

    return lib;
}

} // anonymous namespace

namespace uz {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

NvEncEncoder::NvEncEncoder() = default;

NvEncEncoder::~NvEncEncoder() {
    shutdown();
}

// ---------------------------------------------------------------------------
// isAvailable  --  can be called without constructing an instance
// ---------------------------------------------------------------------------

bool NvEncEncoder::isAvailable() {
    HMODULE lib = probeNvEncLibrary();
    if (lib) {
        FreeLibrary(lib);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// initialize
// ---------------------------------------------------------------------------

bool NvEncEncoder::initialize(const EncoderConfig& config) {
    // Load the runtime library
    auto lib = probeNvEncLibrary();
    if (!lib) {
        std::fprintf(stderr, "[NvEncEncoder] nvEncodeAPI64.dll not found.\n");
        return false;
    }

    nvencLib_ = static_cast<void*>(lib);
    width_    = config.width;
    height_   = config.height;

    // Without the NVIDIA Video Codec SDK headers we cannot populate
    // NV_ENCODE_API_FUNCTION_LIST or create an encoder session.
    // Log the situation and return false so the factory falls back.
    std::fprintf(stderr,
        "[NvEncEncoder] NVENC DLL found -- full implementation requires "
        "NVIDIA Video Codec SDK headers.\n"
        "               Falling back to FFmpeg/x264 encoder.\n");

    // Clean up -- we loaded the DLL only to confirm availability.
    FreeLibrary(lib);
    nvencLib_ = nullptr;

    return false;
}

// ---------------------------------------------------------------------------
// encode  --  stub (unreachable unless initialize() succeeds)
// ---------------------------------------------------------------------------

bool NvEncEncoder::encode(const uint8_t* /*rgbaData*/, uint32_t /*width*/,
                          uint32_t /*height*/, std::vector<uint8_t>& /*outNALUs*/) {
    // Unreachable in the stub build -- initialize() always returns false.
    std::fprintf(stderr, "[NvEncEncoder] encode() called but encoder is not initialized.\n");
    return false;
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

void NvEncEncoder::shutdown() {
    // Future: destroy encoder session, unregister buffers, etc.

    if (nvencLib_) {
        FreeLibrary(static_cast<HMODULE>(nvencLib_));
        nvencLib_ = nullptr;
    }

    nvencPtr_     = nullptr;
    hEncoder_     = nullptr;
    inputBuf_     = nullptr;
    outputBuf_    = nullptr;
    bitstreamBuf_ = nullptr;
    width_        = 0;
    height_       = 0;
    firstFrame_   = true;

    codecConfig_ = CodecConfig{};
    stagingRGBA_.clear();
}

// ---------------------------------------------------------------------------
// extractSPSPPS  --  Annex-B NAL unit scanner
// ---------------------------------------------------------------------------

bool NvEncEncoder::extractSPSPPS(const uint8_t* data, size_t size) {
    // Walk the Annex-B byte stream looking for 3-byte (00 00 01) or
    // 4-byte (00 00 00 01) start codes.  Extract SPS (NAL type 7) and
    // PPS (NAL type 8) units into codecConfig_.

    if (!data || size < 4) {
        return false;
    }

    auto findStartCode = [](const uint8_t* p, const uint8_t* end) -> const uint8_t* {
        while (p + 2 < end) {
            if (p[0] == 0x00 && p[1] == 0x00) {
                if (p[2] == 0x01) {
                    return p; // 3-byte start code
                }
                if (p + 3 < end && p[2] == 0x00 && p[3] == 0x01) {
                    return p; // 4-byte start code
                }
            }
            ++p;
        }
        return nullptr;
    };

    auto startCodeLen = [](const uint8_t* p) -> size_t {
        // Assumes p points to a valid start code
        return (p[2] == 0x01) ? 3u : 4u;
    };

    bool foundSPS = false;
    bool foundPPS = false;

    const uint8_t* end = data + size;
    const uint8_t* pos = findStartCode(data, end);

    while (pos) {
        const size_t scLen = startCodeLen(pos);
        const uint8_t* nalStart = pos + scLen;

        // Find the next start code (or end of data) to determine NAL length
        const uint8_t* next = findStartCode(nalStart, end);
        const uint8_t* nalEnd = next ? next : end;

        if (nalStart < nalEnd) {
            const uint8_t nalType = nalStart[0] & 0x1F;

            if (nalType == 7) {
                // SPS
                codecConfig_.sps.assign(nalStart, nalEnd);
                foundSPS = true;

                // Extract profile and level from the SPS header
                if (nalEnd - nalStart >= 4) {
                    codecConfig_.profileIdc = nalStart[1];
                    codecConfig_.levelIdc   = nalStart[3];
                }
            } else if (nalType == 8) {
                // PPS
                codecConfig_.pps.assign(nalStart, nalEnd);
                foundPPS = true;
            }
        }

        pos = next;
    }

    return foundSPS && foundPPS;
}

} // namespace uz
