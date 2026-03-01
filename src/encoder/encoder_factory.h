#pragma once

#include <memory>

#include "encoder.h"

namespace uz {

/// Creates the best available H.264 encoder for the current system.
/// Tries NvEncEncoder (hardware) first, then FFmpegEncoder (Windows MF software).
/// Returns nullptr if no encoder is available.
[[nodiscard]] std::unique_ptr<IFrameEncoder> createEncoder(const EncoderConfig& config);

} // namespace uz
