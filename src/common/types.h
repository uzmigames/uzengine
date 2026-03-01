#pragma once

#include <cstdint>
#include <vector>

namespace uz {

constexpr uint32_t kDefaultWidth  = 1280;
constexpr uint32_t kDefaultHeight = 720;
constexpr uint32_t kTargetFPS     = 60;
constexpr uint32_t kFrameCount    = 2; // double buffering

struct FrameData {
    std::vector<uint8_t> pixels;  // RGBA, row-major
    uint32_t width   = 0;
    uint32_t height  = 0;
    uint64_t timestamp    = 0;   // microseconds since stream start
    uint32_t frameNumber  = 0;
};

struct EncoderConfig {
    uint32_t width      = kDefaultWidth;
    uint32_t height     = kDefaultHeight;
    uint32_t fps        = kTargetFPS;
    uint32_t bitrate    = 4'000'000; // 4 Mbps
    bool     preferNVENC = true;
};

} // namespace uz
