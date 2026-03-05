// ---------------------------------------------------------------------------
// fps_overlay.cpp -- FPSOverlay implementation
//
// CPU-side bitmap font renderer that draws an "FPS: XXX" counter directly
// into an RGBA pixel buffer.  Uses a hardcoded 5x7 bitmap font scaled 2x
// for readability.  No GPU resources or external font libraries required.
// ---------------------------------------------------------------------------

#include "fps_overlay.h"

#include <cstdio>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// 5x7 bitmap font glyphs.
//
// Each glyph is 7 rows of 5 bits.  Bit 4 (0x10) is the leftmost pixel,
// bit 0 (0x01) is the rightmost.  The glyph order matches kGlyphOrder
// below so we can map characters to indices.
// ---------------------------------------------------------------------------

// Glyph order: ' ', '0'-'9', ':', 'F', 'P', 'S'
//               0    1-10    11   12   13   14
static constexpr int kGlyphCount = 15;

static const uint8_t kFont[kGlyphCount][7] = {
    // [0] Space
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},

    // [1] '0'
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},

    // [2] '1'
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},

    // [3] '2'
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},

    // [4] '3'
    {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E},

    // [5] '4'
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},

    // [6] '5'
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},

    // [7] '6'
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},

    // [8] '7'
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},

    // [9] '8'
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},

    // [10] '9'
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},

    // [11] ':'
    {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00},

    // [12] 'F'
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},

    // [13] 'P'
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},

    // [14] 'S'
    {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E},
};

static constexpr int kGlyphWidth  = 5;
static constexpr int kGlyphHeight = 7;
static constexpr int kScale       = 2;
static constexpr int kCharWidth   = kGlyphWidth * kScale;   // 10px per char
static constexpr int kCharHeight  = kGlyphHeight * kScale;   // 14px per char
static constexpr int kCharSpacing = 1 * kScale;              // 2px between chars
static constexpr int kPadding     = 2;                        // 2px padding around text
static constexpr int kOriginX     = 8;                        // top-left x
static constexpr int kOriginY     = 8;                        // top-left y

// ---------------------------------------------------------------------------
// Map an ASCII character to a glyph index.  Returns 0 (space) for any
// character not in our font set.
// ---------------------------------------------------------------------------
[[nodiscard]] int charToGlyphIndex(char ch) noexcept {
    if (ch == ' ') return 0;
    if (ch >= '0' && ch <= '9') return 1 + (ch - '0');
    if (ch == ':') return 11;
    if (ch == 'F') return 12;
    if (ch == 'P') return 13;
    if (ch == 'S') return 14;
    return 0; // fallback to space for unsupported chars
}

// ---------------------------------------------------------------------------
// Write a single RGBA pixel at (px, py) into the buffer, with bounds checking.
// ---------------------------------------------------------------------------
inline void setPixel(uint8_t* rgbaPixels, uint32_t width, uint32_t height,
                     uint32_t rowPitch, int px, int py,
                     uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept {
    if (px < 0 || py < 0 ||
        static_cast<uint32_t>(px) >= width ||
        static_cast<uint32_t>(py) >= height) {
        return;
    }

    uint8_t* row = rgbaPixels + static_cast<uint32_t>(py) * rowPitch;
    uint8_t* pixel = row + static_cast<uint32_t>(px) * 4;
    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
    pixel[3] = a;
}

} // anonymous namespace

namespace uz {

// ---------------------------------------------------------------------------
// update -- accumulate frame times and compute a rolling average FPS.
//
// Uses a circular buffer of kSampleCount entries.  The FPS is computed as
// the number of samples divided by their sum, giving a stable rolling
// average rather than a noisy instantaneous value.
// ---------------------------------------------------------------------------
void FPSOverlay::update(double deltaSeconds) {
    frameTimes_[sampleIndex_ % kSampleCount] = deltaSeconds;
    sampleIndex_++;

    if (samplesFilled_ < kSampleCount) {
        samplesFilled_++;
    }

    // Sum only the filled portion of the circular buffer
    double sum = 0.0;
    for (int i = 0; i < samplesFilled_; ++i) {
        sum += frameTimes_[i];
    }

    // Avoid division by zero for the degenerate case where all deltas are 0
    if (sum > 0.0) {
        fps_ = static_cast<double>(samplesFilled_) / sum;
    } else {
        fps_ = 0.0;
    }
}

// ---------------------------------------------------------------------------
// renderInto -- stamp the FPS text into an RGBA buffer.
//
// Draws a dark semi-transparent background rectangle behind the text,
// then renders each character glyph at 2x scale in bright green.
// All pixel writes are bounds-checked so we never write outside the buffer.
// ---------------------------------------------------------------------------
void FPSOverlay::renderInto(uint8_t* rgbaPixels, uint32_t width,
                            uint32_t height, uint32_t rowPitch) const {
    if (rgbaPixels == nullptr || width == 0 || height == 0) {
        return;
    }

    // Build the display string: "FPS: " + integer fps value
    char text[32];
    std::snprintf(text, sizeof(text), "FPS: %d", static_cast<int>(fps_));
    const int textLen = static_cast<int>(std::strlen(text));

    // Compute the bounding rectangle for the text block
    const int textPixelWidth  = textLen * (kCharWidth + kCharSpacing) - kCharSpacing;
    const int textPixelHeight = kCharHeight;

    const int bgX0 = kOriginX - kPadding;
    const int bgY0 = kOriginY - kPadding;
    const int bgX1 = kOriginX + textPixelWidth + kPadding;
    const int bgY1 = kOriginY + textPixelHeight + kPadding;

    // Draw dark background rectangle (RGBA: 20, 20, 20, 200)
    for (int py = bgY0; py < bgY1; ++py) {
        for (int px = bgX0; px < bgX1; ++px) {
            setPixel(rgbaPixels, width, height, rowPitch,
                     px, py, 20, 20, 20, 200);
        }
    }

    // Draw each character glyph at 2x scale in bright green
    int cursorX = kOriginX;

    for (int ci = 0; ci < textLen; ++ci) {
        const int glyphIdx = charToGlyphIndex(text[ci]);
        const uint8_t* glyph = kFont[glyphIdx];

        for (int gy = 0; gy < kGlyphHeight; ++gy) {
            const uint8_t row = glyph[gy];

            for (int gx = 0; gx < kGlyphWidth; ++gx) {
                // Bit 4 is leftmost pixel, bit 0 is rightmost
                const bool lit = (row >> (kGlyphWidth - 1 - gx)) & 1;

                if (lit) {
                    // Scale 2x: each font pixel becomes a 2x2 block
                    const int px0 = cursorX + gx * kScale;
                    const int py0 = kOriginY + gy * kScale;

                    for (int sy = 0; sy < kScale; ++sy) {
                        for (int sx = 0; sx < kScale; ++sx) {
                            setPixel(rgbaPixels, width, height, rowPitch,
                                     px0 + sx, py0 + sy,
                                     0, 255, 0, 255);
                        }
                    }
                }
            }
        }

        cursorX += kCharWidth + kCharSpacing;
    }
}

} // namespace uz
