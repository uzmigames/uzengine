/// UzEngine Compare Grid Implementation — CPU Image Compositing
///
/// Bilinear scaling, built-in 8x8 bitmap font rendering, and grid layout
/// composition. All CPU-side, no GPU resources.

#include "compare_grid.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "stb_image.h"
#include "stb_image_write.h"

namespace uz {

UZ_DEFINE_LOG_CATEGORY(CompareGrid, Info);

// ─────────────────────────────────────────────────────────────────────────────
// Built-in 8x8 Bitmap Font — IBM PC CP437 (public domain)
// ─────────────────────────────────────────────────────────────────────────────

/// CP437 8x8 font data for printable ASCII 32-126 (95 glyphs).
/// Each glyph is 8 bytes, one byte per row, MSB = leftmost pixel.
// clang-format off
static constexpr uint8_t kFont8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 ' '
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // 33 '!'
    {0x36,0x36,0x14,0x00,0x00,0x00,0x00,0x00}, // 34 '"'
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // 35 '#'
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // 36 '$'
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // 37 '%'
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // 38 '&'
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // 39 '''
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // 40 '('
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // 41 ')'
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // 42 '*'
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, // 43 '+'
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, // 44 ','
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, // 45 '-'
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // 46 '.'
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // 47 '/'
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, // 48 '0'
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, // 49 '1'
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, // 50 '2'
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, // 51 '3'
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, // 52 '4'
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, // 53 '5'
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, // 54 '6'
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, // 55 '7'
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, // 56 '8'
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, // 57 '9'
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, // 58 ':'
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, // 59 ';'
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // 60 '<'
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, // 61 '='
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // 62 '>'
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, // 63 '?'
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, // 64 '@'
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // 65 'A'
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, // 66 'B'
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // 67 'C'
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, // 68 'D'
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // 69 'E'
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, // 70 'F'
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // 71 'G'
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, // 72 'H'
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 73 'I'
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, // 74 'J'
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // 75 'K'
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, // 76 'L'
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // 77 'M'
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, // 78 'N'
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // 79 'O'
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, // 80 'P'
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // 81 'Q'
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, // 82 'R'
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // 83 'S'
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 84 'T'
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, // 85 'U'
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 86 'V'
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // 87 'W'
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, // 88 'X'
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // 89 'Y'
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, // 90 'Z'
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // 91 '['
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // 92 '\'
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // 93 ']'
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, // 94 '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // 95 '_'
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // 96 '`'
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // 97 'a'
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, // 98 'b'
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // 99 'c'
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, // 100 'd'
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, // 101 'e'
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, // 102 'f'
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // 103 'g'
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, // 104 'h'
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // 105 'i'
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, // 106 'j'
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // 107 'k'
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 108 'l'
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // 109 'm'
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, // 110 'n'
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // 111 'o'
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, // 112 'p'
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, // 113 'q'
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, // 114 'r'
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // 115 's'
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, // 116 't'
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // 117 'u'
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 118 'v'
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // 119 'w'
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, // 120 'x'
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // 121 'y'
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, // 122 'z'
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // 123 '{'
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // 124 '|'
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // 125 '}'
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, // 126 '~'
};
// clang-format on

// ─────────────────────────────────────────────────────────────────────────────
// Font Rendering Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Sets an RGBA pixel in a buffer, with bounds checking.
static void SetPixel(uint8_t* buf, uint32_t bufW, uint32_t bufH,
                     uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x >= bufW || y >= bufH) return;
    const uint32_t idx = (y * bufW + x) * 4;
    buf[idx + 0] = r;
    buf[idx + 1] = g;
    buf[idx + 2] = b;
    buf[idx + 3] = a;
}

/// Draws a single character with a 1px black outline for readability.
/// Scale multiplies the 8x8 base size.
static void DrawChar(uint8_t* buf, uint32_t bufW, uint32_t bufH,
                     uint32_t x0, uint32_t y0, char ch, uint32_t scale) {
    if (ch < 32 || ch > 126) ch = '?';
    const auto& glyph = kFont8x8[ch - 32];

    // Draw black outline first (offset by -1, 0, +1 in each direction)
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            for (uint32_t row = 0; row < 8; ++row) {
                for (uint32_t col = 0; col < 8; ++col) {
                    if (glyph[row] & (1 << col)) {
                        for (uint32_t sy = 0; sy < scale; ++sy) {
                            for (uint32_t sx = 0; sx < scale; ++sx) {
                                const auto px = static_cast<int32_t>(x0 + col * scale + sx) + dx;
                                const auto py = static_cast<int32_t>(y0 + row * scale + sy) + dy;
                                if (px >= 0 && py >= 0) {
                                    SetPixel(buf, bufW, bufH,
                                             static_cast<uint32_t>(px),
                                             static_cast<uint32_t>(py),
                                             0, 0, 0, 255);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Draw white foreground
    for (uint32_t row = 0; row < 8; ++row) {
        for (uint32_t col = 0; col < 8; ++col) {
            if (glyph[row] & (1 << col)) {
                for (uint32_t sy = 0; sy < scale; ++sy) {
                    for (uint32_t sx = 0; sx < scale; ++sx) {
                        SetPixel(buf, bufW, bufH,
                                 x0 + col * scale + sx,
                                 y0 + row * scale + sy,
                                 255, 255, 255, 255);
                    }
                }
            }
        }
    }
}

/// Draws a string of characters starting at (x0, y0).
static void DrawString(uint8_t* buf, uint32_t bufW, uint32_t bufH,
                       uint32_t x0, uint32_t y0, const std::string& text, uint32_t scale) {
    const uint32_t charW = 8 * scale;
    uint32_t x = x0;
    for (char ch : text) {
        if (x + charW > bufW) break; // Clip at right edge
        DrawChar(buf, bufW, bufH, x, y0, ch, scale);
        x += charW;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Bilinear Image Scaling
// ─────────────────────────────────────────────────────────────────────────────

/// Scales an RGBA8 image using bilinear interpolation.
static void BilinearScale(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                          uint8_t* dst, uint32_t dstW, uint32_t dstH) {
    for (uint32_t dy = 0; dy < dstH; ++dy) {
        const float srcY = static_cast<float>(dy) * static_cast<float>(srcH) / static_cast<float>(dstH);
        const auto y0 = static_cast<uint32_t>(srcY);
        const uint32_t y1 = std::min(y0 + 1, srcH - 1);
        const float fy = srcY - static_cast<float>(y0);

        for (uint32_t dx = 0; dx < dstW; ++dx) {
            const float srcX = static_cast<float>(dx) * static_cast<float>(srcW) / static_cast<float>(dstW);
            const auto x0 = static_cast<uint32_t>(srcX);
            const uint32_t x1 = std::min(x0 + 1, srcW - 1);
            const float fx = srcX - static_cast<float>(x0);

            for (uint32_t c = 0; c < 4; ++c) {
                const float tl = static_cast<float>(src[(y0 * srcW + x0) * 4 + c]);
                const float tr = static_cast<float>(src[(y0 * srcW + x1) * 4 + c]);
                const float bl = static_cast<float>(src[(y1 * srcW + x0) * 4 + c]);
                const float br = static_cast<float>(src[(y1 * srcW + x1) * 4 + c]);

                const float top = tl + (tr - tl) * fx;
                const float bot = bl + (br - bl) * fx;
                const float val = top + (bot - top) * fy;

                dst[(dy * dstW + dx) * 4 + c] = static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f));
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CompareGrid Implementation
// ─────────────────────────────────────────────────────────────────────────────

void CompareGrid::SetCellSize(uint32_t w, uint32_t h) {
    m_cellWidth = w;
    m_cellHeight = h;
}

void CompareGrid::SetPadding(uint32_t px) {
    m_padding = px;
}

void CompareGrid::SetLabelHeight(uint32_t px) {
    m_labelHeight = px;
}

void CompareGrid::SetColumns(uint32_t cols) {
    m_forceColumns = cols;
}

void CompareGrid::AddCell(const GridCellInfo& info) {
    m_cells.push_back(info);
}

bool CompareGrid::AddCellFromPNG(const std::string& path, const std::string& label, const std::string& sublabel) {
    int w = 0, h = 0, channels = 0;
    uint8_t* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        UZ_LOG(CompareGrid, Error, "Failed to load PNG: path='{}'", path);
        return false;
    }

    m_ownedImages.emplace_back(data);

    GridCellInfo info;
    info.label = label;
    info.sublabel = sublabel;
    info.pixels = data;
    info.srcWidth = static_cast<uint32_t>(w);
    info.srcHeight = static_cast<uint32_t>(h);
    m_cells.push_back(std::move(info));

    UZ_LOG(CompareGrid, Trace, "Loaded PNG: path='{}' ({}x{})", path, w, h);
    return true;
}

bool CompareGrid::Compose(std::vector<uint8_t>& outPixels, uint32_t& outWidth, uint32_t& outHeight) const {
    const auto cellCount = static_cast<uint32_t>(m_cells.size());
    if (cellCount == 0) {
        UZ_LOG(CompareGrid, Error, "Cannot compose — no cells added");
        return false;
    }

    // Grid layout: forced columns or roughly square
    const uint32_t cols = m_forceColumns > 0
        ? m_forceColumns
        : static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(cellCount))));
    const uint32_t rows = (cellCount + cols - 1) / cols;

    const uint32_t cellTotalH = m_cellHeight + m_labelHeight;
    outWidth = m_padding + cols * (m_cellWidth + m_padding);
    outHeight = m_padding + rows * (cellTotalH + m_padding);

    outPixels.resize(static_cast<size_t>(outWidth) * outHeight * 4);

    // Fill with dark background (#1a1a1a)
    for (size_t i = 0; i < outPixels.size(); i += 4) {
        outPixels[i + 0] = 0x1A;
        outPixels[i + 1] = 0x1A;
        outPixels[i + 2] = 0x1A;
        outPixels[i + 3] = 0xFF;
    }

    // Temporary buffer for scaled cell images
    std::vector<uint8_t> scaledBuf(static_cast<size_t>(m_cellWidth) * m_cellHeight * 4);

    for (uint32_t i = 0; i < cellCount; ++i) {
        const auto& cell = m_cells[i];
        const uint32_t col = i % cols;
        const uint32_t row = i / cols;
        const uint32_t cellX = m_padding + col * (m_cellWidth + m_padding);
        const uint32_t cellY = m_padding + row * (cellTotalH + m_padding);

        // Scale or center the source image into the cell area
        if (cell.pixels && cell.srcWidth > 0 && cell.srcHeight > 0) {
            if (cell.srcWidth <= m_cellWidth && cell.srcHeight <= m_cellHeight) {
                // Source is smaller — center it on dark gray (#333333) background
                std::memset(scaledBuf.data(), 0, scaledBuf.size());
                for (size_t j = 0; j < scaledBuf.size(); j += 4) {
                    scaledBuf[j + 0] = 0x33;
                    scaledBuf[j + 1] = 0x33;
                    scaledBuf[j + 2] = 0x33;
                    scaledBuf[j + 3] = 0xFF;
                }
                const uint32_t offX = (m_cellWidth - cell.srcWidth) / 2;
                const uint32_t offY = (m_cellHeight - cell.srcHeight) / 2;
                for (uint32_t sy = 0; sy < cell.srcHeight; ++sy) {
                    std::memcpy(
                        scaledBuf.data() + ((offY + sy) * m_cellWidth + offX) * 4,
                        cell.pixels + sy * cell.srcWidth * 4,
                        cell.srcWidth * 4);
                }
            } else {
                BilinearScale(cell.pixels, cell.srcWidth, cell.srcHeight,
                              scaledBuf.data(), m_cellWidth, m_cellHeight);
            }

            // Blit scaled image into output
            for (uint32_t sy = 0; sy < m_cellHeight; ++sy) {
                std::memcpy(
                    outPixels.data() + ((cellY + sy) * outWidth + cellX) * 4,
                    scaledBuf.data() + sy * m_cellWidth * 4,
                    m_cellWidth * 4);
            }
        }

        // Draw label area — semi-transparent dark bar
        const uint32_t labelY = cellY + m_cellHeight;
        for (uint32_t ly = 0; ly < m_labelHeight; ++ly) {
            for (uint32_t lx = 0; lx < m_cellWidth; ++lx) {
                SetPixel(outPixels.data(), outWidth, outHeight,
                         cellX + lx, labelY + ly,
                         0x10, 0x10, 0x10, 0xE0);
            }
        }

        // Determine font scale to fit within label height
        // Try 2x (16px) if label height >= 20, otherwise 1x (8px)
        const uint32_t fontScale = (m_labelHeight >= 20) ? 2 : 1;
        const uint32_t textH = 8 * fontScale;
        const uint32_t textPadX = 4;

        if (!cell.label.empty()) {
            // If we have room for both label and sublabel (two lines)
            if (!cell.sublabel.empty() && m_labelHeight >= textH * 2 + 2) {
                DrawString(outPixels.data(), outWidth, outHeight,
                           cellX + textPadX, labelY + 1,
                           cell.label, fontScale);
                DrawString(outPixels.data(), outWidth, outHeight,
                           cellX + textPadX, labelY + 1 + textH + 1,
                           cell.sublabel, fontScale);
            } else {
                // Single line — vertically center the label
                const uint32_t textY = labelY + (m_labelHeight - textH) / 2;
                DrawString(outPixels.data(), outWidth, outHeight,
                           cellX + textPadX, textY,
                           cell.label, fontScale);
            }
        }
    }

    UZ_LOG(CompareGrid, Info, "Grid composed: {}x{} ({} cells, {}x{} grid)",
           outWidth, outHeight, cellCount, cols, rows);
    return true;
}

bool CompareGrid::SaveToPNG(const std::string& path) const {
    std::vector<uint8_t> pixels;
    uint32_t width = 0, height = 0;

    if (!Compose(pixels, width, height)) {
        return false;
    }

    const int result = stbi_write_png(
        path.c_str(),
        static_cast<int>(width),
        static_cast<int>(height),
        4,
        pixels.data(),
        static_cast<int>(width * 4));

    if (result == 0) {
        UZ_LOG(CompareGrid, Error, "stbi_write_png failed: path='{}'", path);
        return false;
    }

    UZ_LOG(CompareGrid, Info, "Grid saved: path='{}' ({}x{})", path, width, height);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ImageDiff Implementation
// ─────────────────────────────────────────────────────────────────────────────

ImageDiffResult ImageDiff::Compare(
    const uint8_t* pixelsA, uint32_t widthA, uint32_t heightA,
    const uint8_t* pixelsB, uint32_t widthB, uint32_t heightB,
    uint8_t threshold) {

    ImageDiffResult result;
    const uint32_t w = std::max(widthA, widthB);
    const uint32_t h = std::max(heightA, heightB);
    result.totalPixels = w * h;

    double sumSqDiff = 0.0;
    double sumAbsDiff = 0.0;
    float maxDiff = 0.f;

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t ra = 0, ga = 0, ba = 0;
            uint8_t rb = 0, gb = 0, bb = 0;

            if (x < widthA && y < heightA) {
                const uint32_t idx = (y * widthA + x) * 4;
                ra = pixelsA[idx]; ga = pixelsA[idx + 1]; ba = pixelsA[idx + 2];
            }
            if (x < widthB && y < heightB) {
                const uint32_t idx = (y * widthB + x) * 4;
                rb = pixelsB[idx]; gb = pixelsB[idx + 1]; bb = pixelsB[idx + 2];
            }

            const int dr = std::abs(static_cast<int>(ra) - rb);
            const int dg = std::abs(static_cast<int>(ga) - gb);
            const int db = std::abs(static_cast<int>(ba) - bb);
            const int maxCh = std::max({dr, dg, db});

            if (maxCh > threshold) {
                result.diffPixels++;
            }
            if (maxCh > 0) {
                result.identical = false;
            }

            const float fmax = static_cast<float>(maxCh);
            if (fmax > maxDiff) maxDiff = fmax;

            sumSqDiff += static_cast<double>(dr * dr + dg * dg + db * db);
            sumAbsDiff += static_cast<double>(dr + dg + db);
        }
    }

    result.maxChannelDiff = maxDiff;
    const double totalChannels = static_cast<double>(result.totalPixels) * 3.0;
    result.avgChannelDiff = totalChannels > 0 ? static_cast<float>(sumAbsDiff / totalChannels) : 0.f;
    result.diffPercentage = result.totalPixels > 0
        ? static_cast<float>(result.diffPixels) * 100.f / static_cast<float>(result.totalPixels)
        : 0.f;

    // PSNR: 10 * log10(255^2 / MSE), infinity if identical
    const double mse = totalChannels > 0 ? sumSqDiff / totalChannels : 0.0;
    if (mse < 1e-10) {
        result.psnr = 999.f; // Effectively infinite — identical images
    } else {
        result.psnr = static_cast<float>(10.0 * std::log10(255.0 * 255.0 / mse));
    }

    return result;
}

void ImageDiff::GenerateHeatmap(
    const uint8_t* pixelsA, uint32_t widthA, uint32_t heightA,
    const uint8_t* pixelsB, uint32_t widthB, uint32_t heightB,
    std::vector<uint8_t>& outPixels, uint32_t& outWidth, uint32_t& outHeight,
    uint32_t amplify) {

    outWidth = std::max(widthA, widthB);
    outHeight = std::max(heightA, heightB);
    outPixels.resize(static_cast<size_t>(outWidth) * outHeight * 4);

    for (uint32_t y = 0; y < outHeight; ++y) {
        for (uint32_t x = 0; x < outWidth; ++x) {
            const uint32_t outIdx = (y * outWidth + x) * 4;

            // Out-of-bounds in either image → magenta
            const bool inA = (x < widthA && y < heightA);
            const bool inB = (x < widthB && y < heightB);

            if (!inA || !inB) {
                outPixels[outIdx + 0] = 0xFF;
                outPixels[outIdx + 1] = 0x00;
                outPixels[outIdx + 2] = 0xFF;
                outPixels[outIdx + 3] = 0xFF;
                continue;
            }

            const uint32_t idxA = (y * widthA + x) * 4;
            const uint32_t idxB = (y * widthB + x) * 4;

            const int dr = std::abs(static_cast<int>(pixelsA[idxA]) - pixelsB[idxB]);
            const int dg = std::abs(static_cast<int>(pixelsA[idxA + 1]) - pixelsB[idxB + 1]);
            const int db = std::abs(static_cast<int>(pixelsA[idxA + 2]) - pixelsB[idxB + 2]);

            const int maxCh = std::max({dr, dg, db});
            const int amplified = std::min(static_cast<int>(maxCh * amplify), 255);

            // Heatmap: black→red→yellow→white as diff increases
            if (amplified == 0) {
                outPixels[outIdx + 0] = 0;
                outPixels[outIdx + 1] = 0;
                outPixels[outIdx + 2] = 0;
            } else if (amplified < 128) {
                // Black to red
                outPixels[outIdx + 0] = static_cast<uint8_t>(amplified * 2);
                outPixels[outIdx + 1] = 0;
                outPixels[outIdx + 2] = 0;
            } else if (amplified < 200) {
                // Red to yellow
                outPixels[outIdx + 0] = 255;
                outPixels[outIdx + 1] = static_cast<uint8_t>((amplified - 128) * 255 / 72);
                outPixels[outIdx + 2] = 0;
            } else {
                // Yellow to white
                outPixels[outIdx + 0] = 255;
                outPixels[outIdx + 1] = 255;
                outPixels[outIdx + 2] = static_cast<uint8_t>((amplified - 200) * 255 / 55);
            }
            outPixels[outIdx + 3] = 0xFF;
        }
    }
}

bool ImageDiff::DiffToPNG(
    const std::string& pathA, const std::string& labelA,
    const std::string& pathB, const std::string& labelB,
    const std::string& outputPath,
    uint32_t cellSize, uint8_t threshold) {

    int wA = 0, hA = 0, chA = 0;
    uint8_t* dataA = stbi_load(pathA.c_str(), &wA, &hA, &chA, 4);
    if (!dataA) {
        UZ_LOG(CompareGrid, Error, "ImageDiff: failed to load A: path='{}'", pathA);
        return false;
    }

    int wB = 0, hB = 0, chB = 0;
    uint8_t* dataB = stbi_load(pathB.c_str(), &wB, &hB, &chB, 4);
    if (!dataB) {
        UZ_LOG(CompareGrid, Error, "ImageDiff: failed to load B: path='{}'", pathB);
        stbi_image_free(dataA);
        return false;
    }

    // Compute diff stats
    const auto result = Compare(
        dataA, static_cast<uint32_t>(wA), static_cast<uint32_t>(hA),
        dataB, static_cast<uint32_t>(wB), static_cast<uint32_t>(hB),
        threshold);

    // Generate heatmap
    std::vector<uint8_t> heatmap;
    uint32_t heatW = 0, heatH = 0;
    GenerateHeatmap(
        dataA, static_cast<uint32_t>(wA), static_cast<uint32_t>(hA),
        dataB, static_cast<uint32_t>(wB), static_cast<uint32_t>(hB),
        heatmap, heatW, heatH);

    // Build stats sublabel
    char statsBuf[128];
    std::snprintf(statsBuf, sizeof(statsBuf), "%.1f%% diff, PSNR %.1f dB",
                  static_cast<double>(result.diffPercentage),
                  static_cast<double>(result.psnr));

    // Compose 3-panel horizontal grid: [A] [B] [Diff]
    CompareGrid grid;
    grid.SetColumns(3);
    grid.SetCellSize(cellSize, cellSize);
    grid.SetLabelHeight(36); // Two lines of text at 2x scale

    GridCellInfo cellA;
    cellA.label = labelA;
    cellA.sublabel = "Reference";
    cellA.pixels = dataA;
    cellA.srcWidth = static_cast<uint32_t>(wA);
    cellA.srcHeight = static_cast<uint32_t>(hA);
    grid.AddCell(cellA);

    GridCellInfo cellB;
    cellB.label = labelB;
    cellB.sublabel = "Compare";
    cellB.pixels = dataB;
    cellB.srcWidth = static_cast<uint32_t>(wB);
    cellB.srcHeight = static_cast<uint32_t>(hB);
    grid.AddCell(cellB);

    GridCellInfo cellDiff;
    cellDiff.label = result.identical ? "IDENTICAL" : "DIFF";
    cellDiff.sublabel = statsBuf;
    cellDiff.pixels = heatmap.data();
    cellDiff.srcWidth = heatW;
    cellDiff.srcHeight = heatH;
    grid.AddCell(cellDiff);

    const bool ok = grid.SaveToPNG(outputPath);

    stbi_image_free(dataA);
    stbi_image_free(dataB);

    if (ok) {
        UZ_LOG(CompareGrid, Info,
               "Diff saved: path='{}' ({}% diff, PSNR={:.1f} dB, maxDiff={:.0f}, identical={})",
               outputPath, result.diffPercentage, result.psnr, result.maxChannelDiff,
               result.identical ? "yes" : "no");
    }
    return ok;
}

} // namespace uz
