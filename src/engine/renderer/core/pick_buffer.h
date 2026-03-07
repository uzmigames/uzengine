#pragma once

// pick_buffer.h — Pure CPU pick-buffer query functions.
//
// These functions operate on a flat RGBA8 pixel buffer (the GPU pick readback)
// and resolve screen coordinates to node IDs. Extracted from Renderer for
// testability without GPU/RHI dependencies.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace uz {

/// Decode a node ID from the pick buffer at screen coordinates (x, y).
/// The pick buffer is 1/4 resolution — screen coords are divided by 4.
/// Returns 0 if out of bounds or no node at that pixel.
inline uint32_t pick_node_at_screen(
    const uint8_t* data, size_t data_size,
    uint32_t pick_w, uint32_t pick_h,
    int screen_x, int screen_y)
{
    if (!data || data_size == 0 || pick_w == 0 || pick_h == 0) return 0;

    const int px = std::clamp(screen_x / 4, 0, static_cast<int>(pick_w) - 1);
    const int py = std::clamp(screen_y / 4, 0, static_cast<int>(pick_h) - 1);

    const size_t idx = static_cast<size_t>(py) * pick_w + px;
    if (idx * 4 + 2 >= data_size) return 0;

    const uint32_t r = data[idx * 4 + 0];
    const uint32_t g = data[idx * 4 + 1];
    const uint32_t b = data[idx * 4 + 2];
    return r | (g << 8) | (b << 16);
}

/// Collect unique node IDs within a screen-space box region.
/// Coordinates and dimensions are in full-resolution screen pixels.
/// Returns a deduplicated vector of non-zero node IDs.
inline std::vector<uint32_t> box_pick_nodes(
    const uint8_t* data, size_t data_size,
    uint32_t pick_w, uint32_t pick_h,
    int screen_x, int screen_y, int box_w, int box_h)
{
    if (!data || data_size == 0 || pick_w == 0 || pick_h == 0) return {};

    const int x0 = std::clamp(screen_x           / 4, 0, static_cast<int>(pick_w));
    const int y0 = std::clamp(screen_y           / 4, 0, static_cast<int>(pick_h));
    const int x1 = std::clamp((screen_x + box_w) / 4, 0, static_cast<int>(pick_w));
    const int y1 = std::clamp((screen_y + box_h) / 4, 0, static_cast<int>(pick_h));

    std::vector<uint32_t> result;
    for (int row = y0; row < y1; ++row) {
        for (int col = x0; col < x1; ++col) {
            const size_t idx = static_cast<size_t>(row) * pick_w + col;
            if (idx * 4 + 2 >= data_size) continue;

            const uint32_t r  = data[idx * 4 + 0];
            const uint32_t g  = data[idx * 4 + 1];
            const uint32_t b  = data[idx * 4 + 2];
            const uint32_t id = r | (g << 8) | (b << 16);
            if (id == 0) continue;

            bool found = false;
            for (uint32_t ex : result) { if (ex == id) { found = true; break; } }
            if (!found) result.push_back(id);
        }
    }
    return result;
}

} // namespace uz
