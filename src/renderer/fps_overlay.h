#pragma once

#include <cstdint>

namespace uz {

// ---------------------------------------------------------------------------
// FPSOverlay -- CPU-side bitmap font renderer for FPS counter.
//
// Renders "FPS: XXX" text directly into an RGBA pixel buffer using a
// hardcoded 5x7 bitmap font.  No GPU resources needed.
// ---------------------------------------------------------------------------

class FPSOverlay {
public:
    // Update the FPS measurement.  Call once per frame with the frame delta.
    void update(double deltaSeconds);

    // Render the FPS text into an RGBA pixel buffer.
    // The text is drawn in the top-left corner with a dark background.
    void renderInto(uint8_t* rgbaPixels, uint32_t width, uint32_t height,
                    uint32_t rowPitch) const;

    // Get the current FPS value.
    double fps() const { return fps_; }

private:
    static constexpr int kSampleCount = 60;

    double frameTimes_[kSampleCount]{};
    int    sampleIndex_ = 0;
    int    samplesFilled_ = 0;
    double fps_ = 0.0;
};

} // namespace uz
