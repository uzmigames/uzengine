#pragma once

#include "mat4.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace uz {

/// Only a transform observed on the immediately preceding frame is reusable.
/// Packed EnTT ids include the entity generation, so recycled entities start fresh.
class PrevTransformCache {
public:
    [[nodiscard]] bool HasPrevious(uint32_t id, uint32_t frame) const {
        const auto it = m_entries.find(id);
        return it != m_entries.end() && it->second.frame + 1u == frame;
    }

    [[nodiscard]] Mat4 Lookup(uint32_t id, const Mat4& current, uint32_t frame) const {
        const auto it = m_entries.find(id);
        return it != m_entries.end() && it->second.frame + 1u == frame
            ? it->second.world : current;
    }

    void Record(uint32_t id, const Mat4& current, uint32_t frame) {
        m_entries.insert_or_assign(id, Entry{current, frame});
    }

    /// Call before recording the current frame. Unsigned age handles frame wrap.
    void Sweep(uint32_t frame) {
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (frame - it->second.frame > 1u) it = m_entries.erase(it);
            else ++it;
        }
    }

    [[nodiscard]] size_t Size() const noexcept { return m_entries.size(); }

private:
    struct Entry { Mat4 world; uint32_t frame; };
    std::unordered_map<uint32_t, Entry> m_entries;
};

} // namespace uz
