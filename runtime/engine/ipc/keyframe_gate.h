#pragma once

#include <cstddef>
#include <cstdint>

namespace uz {

/// Inspect the first VCL NAL (types 1-5) in an Annex-B access unit.
/// AUD/SPS/PPS without a slice are not a key frame.
[[nodiscard]] inline bool IsAnnexBKeyframe(const uint8_t* data, size_t size) noexcept {
    if (data == nullptr) return false;

    for (size_t i = 0; i < size && size - i >= 3; ++i) {
        if (data[i] != 0 || data[i + 1] != 0) continue;

        size_t prefixSize = 0;
        if (data[i + 2] == 1) {
            prefixSize = 3;
        } else if (size - i >= 4 && data[i + 2] == 0 && data[i + 3] == 1) {
            prefixSize = 4;
        }
        if (prefixSize == 0) continue;
        if (size - i <= prefixSize) return false;

        const uint8_t type = data[i + prefixSize] & 0x1F;
        if (type >= 1 && type <= 5) return type == 5;
        i += prefixSize;
    }
    return false;
}

/// After a dropped access unit, hold deltas until an IDR restores the chain.
/// The caller requests a keyframe when reporting a drop.
class KeyframeGate {
public:
    [[nodiscard]] bool ShouldDeliver(bool isKeyframe) noexcept {
        if (isKeyframe) m_awaitingKey = false;
        return !m_awaitingKey;
    }

    void OnDropped() noexcept { m_awaitingKey = true; }
    void Arm() noexcept { m_awaitingKey = true; }
    [[nodiscard]] bool AwaitingKeyframe() const noexcept { return m_awaitingKey; }

private:
    bool m_awaitingKey = true;
};

} // namespace uz
