#include "vfx_thumbnail.h"

#include "engine/logging/log.h"

#include <fstream>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Path resolver
// ─────────────────────────────────────────────────────────────────────────────

std::string VfxThumbnailPathForAsset(const std::string& assetPath) {
    const auto dot = assetPath.rfind('.');
    if (dot == std::string::npos) {
        return assetPath + ".thumb.png";
    }
    return assetPath.substr(0, dot) + ".thumb.png";
}

// ─────────────────────────────────────────────────────────────────────────────
// PrepareCapture
// ─────────────────────────────────────────────────────────────────────────────

bool VfxThumbnailCapturer::PrepareCapture(const VfxThumbnailConfig& cfg) {
    if (cfg.fps <= 0.0f) {
        UZ_LOG(VFX, Error, "VfxThumbnailCapturer: fps must be > 0 (got {})", cfg.fps);
        return false;
    }
    if (cfg.width == 0 || cfg.height == 0) {
        UZ_LOG(VFX, Error, "VfxThumbnailCapturer: resolution must be > 0");
        return false;
    }

    config_      = cfg;
    stepDt_      = 1.0f / cfg.fps;
    currentTick_ = 0u;
    ready_       = (cfg.captureFrame == 0u);
    prepared_    = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// AdvanceFrame
// ─────────────────────────────────────────────────────────────────────────────

void VfxThumbnailCapturer::AdvanceFrame() noexcept {
    if (!prepared_ || ready_) return;
    ++currentTick_;
    if (currentTick_ >= config_.captureFrame ||
        currentTick_ >= config_.maxTicks) {
        ready_ = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FinishCapture
// ─────────────────────────────────────────────────────────────────────────────

VfxThumbnailResult VfxThumbnailCapturer::FinishCapture(
    const std::vector<uint8_t>& pngData) const {
    VfxThumbnailResult result;
    result.outputPath     = VfxThumbnailPathForAsset(config_.assetPath);
    result.framesCapturedAt = currentTick_;

    if (pngData.empty()) {
        UZ_LOG(VFX, Error, "VfxThumbnailCapturer: empty PNG data, skipping write");
        return result;
    }

    std::ofstream ofs(result.outputPath, std::ios::binary);
    if (!ofs) {
        UZ_LOG(VFX, Error,
               "VfxThumbnailCapturer: failed to open '{}' for writing",
               result.outputPath);
        return result;
    }
    ofs.write(reinterpret_cast<const char*>(pngData.data()),
              static_cast<std::streamsize>(pngData.size()));
    if (!ofs) {
        UZ_LOG(VFX, Error,
               "VfxThumbnailCapturer: write failed to '{}'", result.outputPath);
        return result;
    }

    result.success = true;
    UZ_LOG(VFX, Info,
           "VfxThumbnailCapturer: saved {}×{} thumbnail to '{}' at tick {}",
           config_.width, config_.height, result.outputPath, currentTick_);
    return result;
}

} // namespace uz
