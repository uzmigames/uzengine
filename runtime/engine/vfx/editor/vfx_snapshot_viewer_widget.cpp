#include "vfx_snapshot_viewer_widget.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace uz {

namespace {

/// Returns the number of float components exposed by `type`.
/// Non-float types return 1 (value reinterpret-cast into float for stats).
uint32_t ComponentCount(VfxSnapshotElementType type) noexcept {
    switch (type) {
        case VfxSnapshotElementType::Float2: return 2;
        case VfxSnapshotElementType::Float3: return 3;
        case VfxSnapshotElementType::Float4: return 4;
        default: return 1;
    }
}

/// Reads one element's component values into `out[componentCount]`.
/// Returns false when the channel data is malformed.
bool ReadElementAsFloats(const VfxSnapshotChannel& ch, uint32_t index,
                          float out[4]) noexcept {
    const uint32_t elemSize = VfxSnapshotElementSize(ch.elementType);
    if (elemSize == 0) return false;
    const size_t offset = static_cast<size_t>(index) * elemSize;
    if (offset + elemSize > ch.data.size()) return false;
    const uint8_t* p = ch.data.data() + offset;

    switch (ch.elementType) {
        case VfxSnapshotElementType::Float:  std::memcpy(&out[0], p, 4); return true;
        case VfxSnapshotElementType::Float2: std::memcpy(&out[0], p, 8); return true;
        case VfxSnapshotElementType::Float3: std::memcpy(&out[0], p, 12); return true;
        case VfxSnapshotElementType::Float4: std::memcpy(&out[0], p, 16); return true;
        case VfxSnapshotElementType::Uint32: {
            uint32_t v; std::memcpy(&v, p, 4);
            out[0] = static_cast<float>(v);
            return true;
        }
        case VfxSnapshotElementType::Int32: {
            int32_t v; std::memcpy(&v, p, 4);
            out[0] = static_cast<float>(v);
            return true;
        }
        case VfxSnapshotElementType::Bool: {
            out[0] = (*p != 0) ? 1.0f : 0.0f;
            return true;
        }
        case VfxSnapshotElementType::Half: {
            // We don't have a full half→float converter here; store the
            // raw u16 as a float for stats purposes.
            uint16_t raw; std::memcpy(&raw, p, 2);
            out[0] = static_cast<float>(raw);
            return true;
        }
    }
    return false;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Load / Set / Clear
// ─────────────────────────────────────────────────────────────────────────────

bool VfxSnapshotViewerWidget::LoadFromFile(const std::string& path,
                                              std::string* outError) {
    VfxSnapshot snap;
    std::string err;
    if (!VfxSnapshotLoadFromFile(path, snap, err)) {
        if (outError) *outError = std::move(err);
        Clear();
        return false;
    }
    SetSnapshot(std::move(snap));
    return true;
}

void VfxSnapshotViewerWidget::SetSnapshot(VfxSnapshot snapshot) {
    snapshot_ = std::move(snapshot);
    loaded_   = true;
    channelNames_.clear();
    for (const auto& ch : snapshot_.channels) {
        channelNames_.push_back(ch.name);
    }
    selectedChannel_ = channelNames_.empty() ? UINT32_MAX : 0u;
    particleCursor_  = 0;
    RecomputeStats();
}

void VfxSnapshotViewerWidget::Clear() noexcept {
    snapshot_ = VfxSnapshot{};
    loaded_   = false;
    channelNames_.clear();
    selectedChannel_ = UINT32_MAX;
    particleCursor_  = 0;
    stats_ = VfxSnapshotChannelStats{};
}

// ─────────────────────────────────────────────────────────────────────────────
// Channel selection
// ─────────────────────────────────────────────────────────────────────────────

void VfxSnapshotViewerWidget::SelectChannel(uint32_t channelIndex) {
    if (!loaded_) return;
    if (channelIndex >= channelNames_.size()) return;
    selectedChannel_ = channelIndex;
    RecomputeStats();
}

// ─────────────────────────────────────────────────────────────────────────────
// Particle cursor + alive count
// ─────────────────────────────────────────────────────────────────────────────

uint32_t VfxSnapshotViewerWidget::AliveCount() const noexcept {
    return loaded_ ? snapshot_.header.aliveCount : 0u;
}

void VfxSnapshotViewerWidget::SetParticleCursor(uint32_t index) noexcept {
    const uint32_t alive = AliveCount();
    if (alive == 0) { particleCursor_ = 0; return; }
    particleCursor_ = std::min(index, alive - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera
// ─────────────────────────────────────────────────────────────────────────────

void VfxSnapshotViewerWidget::Tumble(float dYaw, float dPitch) noexcept {
    orbitYaw   += dYaw;
    // Clamp pitch to avoid gimbal flip.
    const float kPitchLimit = 1.5533f; // ~89 degrees
    orbitPitch = std::clamp(orbitPitch + dPitch, -kPitchLimit, kPitchLimit);
}

void VfxSnapshotViewerWidget::Dolly(float delta) noexcept {
    orbitDistance = std::max(0.1f, orbitDistance - delta);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stats recompute
// ─────────────────────────────────────────────────────────────────────────────

void VfxSnapshotViewerWidget::RecomputeStats() {
    stats_ = VfxSnapshotChannelStats{};
    if (!loaded_ || selectedChannel_ >= snapshot_.channels.size()) return;

    const auto& ch = snapshot_.channels[selectedChannel_];
    const uint32_t components = ComponentCount(ch.elementType);
    const uint32_t count = std::min(ch.elementCount, snapshot_.header.aliveCount);
    if (count == 0) return;

    stats_.componentCount = components;
    for (uint32_t c = 0; c < components; ++c) {
        stats_.minValue[c] =  std::numeric_limits<float>::infinity();
        stats_.maxValue[c] = -std::numeric_limits<float>::infinity();
        stats_.avgValue[c] = 0.0f;
    }

    uint32_t valid = 0;
    for (uint32_t i = 0; i < count; ++i) {
        float vals[4] = {0, 0, 0, 0};
        if (!ReadElementAsFloats(ch, i, vals)) continue;
        ++valid;
        for (uint32_t c = 0; c < components; ++c) {
            stats_.minValue[c] = std::min(stats_.minValue[c], vals[c]);
            stats_.maxValue[c] = std::max(stats_.maxValue[c], vals[c]);
            stats_.avgValue[c] += vals[c];
        }
    }
    if (valid > 0) {
        const float inv = 1.0f / static_cast<float>(valid);
        for (uint32_t c = 0; c < components; ++c) {
            stats_.avgValue[c] *= inv;
        }
    } else {
        for (uint32_t c = 0; c < components; ++c) {
            stats_.minValue[c] = 0.0f;
            stats_.maxValue[c] = 0.0f;
            stats_.avgValue[c] = 0.0f;
        }
    }
    stats_.sampleCount = valid;
}

} // namespace uz
