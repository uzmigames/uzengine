#pragma once

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace uz {

/// Absolute render indices, including preload frames. A sequence never changes
/// loading or presentation; early frames are captured only by explicit opt-in.
struct CaptureSequence {
    uint32_t first = 0;
    uint32_t last = 0;
    uint32_t step = 1;

    static std::optional<CaptureSequence> Parse(std::string_view text) {
        CaptureSequence sequence;
        auto number = [&](uint32_t& value) {
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            if (result.ec != std::errc{}) return false;
            text.remove_prefix(static_cast<size_t>(result.ptr - text.data()));
            return true;
        };
        if (text.empty() || !number(sequence.first)) return std::nullopt;
        sequence.last = sequence.first;
        if (text.empty()) return sequence;
        if (text.front() != '-') return std::nullopt;
        text.remove_prefix(1);
        if (!number(sequence.last) || sequence.last < sequence.first) return std::nullopt;
        if (text.empty()) return sequence;
        if (text.front() != ':') return std::nullopt;
        text.remove_prefix(1);
        if (!number(sequence.step) || sequence.step == 0 || !text.empty()) return std::nullopt;
        return sequence;
    }

    bool Includes(uint32_t frame, bool assetsReady, bool preloading, bool capturePreload) const {
        if (!capturePreload && (preloading || !assetsReady)) return false;
        return frame >= first && frame <= last && (frame - first) % step == 0;
    }

    static std::string ImagePath(const std::string& base, uint32_t frame) {
        const std::filesystem::path path(base);
        const std::string extension = path.has_extension() ? path.extension().string() : ".png";
        return (path.parent_path() / (path.stem().string() + "_" + std::to_string(frame) + extension)).string();
    }
};

} // namespace uz
