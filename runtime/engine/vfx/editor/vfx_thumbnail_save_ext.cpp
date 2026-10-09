#include "vfx_thumbnail_save_ext.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VfxThumbnailCaptureQueue
// ─────────────────────────────────────────────────────────────────────────────

void VfxThumbnailCaptureQueue::Push(VfxThumbnailCaptureRequest req) {
    std::lock_guard lock(mutex_);
    queue_.push_back(std::move(req));
}

const VfxThumbnailCaptureRequest* VfxThumbnailCaptureQueue::Peek() const {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) return nullptr;
    return &queue_.front();
}

void VfxThumbnailCaptureQueue::Pop() {
    std::lock_guard lock(mutex_);
    if (!queue_.empty()) {
        queue_.erase(queue_.begin());
    }
}

uint32_t VfxThumbnailCaptureQueue::Size() const {
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(queue_.size());
}

void VfxThumbnailCaptureQueue::Clear() {
    std::lock_guard lock(mutex_);
    queue_.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxThumbnailSaveExtension
// ─────────────────────────────────────────────────────────────────────────────

VfxThumbnailSaveExtension::VfxThumbnailSaveExtension(
    VfxThumbnailCaptureQueue* queue)
    : queue_(queue)
{
    // Default config: 128×128, frame 15, seed 42, 60 fps.
    // These match the VfxThumbnailConfig defaults.
}

void VfxThumbnailSaveExtension::PostSave(
    VfxSaveAssetKind kind,
    const void* /*asset*/,
    const std::string& outputPath) {

    if (!IsVfxAssetKindThumbnailable(kind)) {
        return;
    }

    VfxThumbnailCaptureRequest req;
    req.config           = defaultConfig_;
    req.config.assetPath = outputPath;
    req.kind             = kind;

    ++enqueueCount_;

    if (queue_ != nullptr) {
        queue_->Push(std::move(req));
        UZ_LOG(VFX, Info,
               "ThumbnailCapture: queued capture for '{}' (kind={})",
               outputPath, VfxSaveAssetKindName(kind));
    }
}

} // namespace uz
