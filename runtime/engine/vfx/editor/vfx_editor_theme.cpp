/// VFX Editor Theme — implementation (phase 12.1.0).
///
/// Stores the single global `VfxEditorTheme` instance + implements
/// the WCAG 2.x contrast-ratio helper consumed by the test suite.

#include "vfx_editor_theme.h"

#include <algorithm>
#include <cmath>

namespace uz {

const VfxEditorTheme& VfxTheme() noexcept {
    // The default-constructed palette / scales are already calibrated
    // in the header. Keeping the accessor out-of-line lets future
    // code swap the backing instance (user-loaded `.uztheme` JSON)
    // without any ABI churn.
    static const VfxEditorTheme kDefault{};
    return kDefault;
}

// ─────────────────────────────────────────────────────────────────────────────
// WCAG 2.x contrast ratio
//
// L = 0.2126 * R' + 0.7152 * G' + 0.0722 * B'
// where R'/G'/B' come from sRGB gamma de-linearization:
//   c' = c / 12.92                        if c <= 0.03928
//   c' = ((c + 0.055) / 1.055) ^ 2.4      otherwise
// Then contrast = (max(L1,L2) + 0.05) / (min(L1,L2) + 0.05).
// ─────────────────────────────────────────────────────────────────────────────

static float SrgbToLinear(float c) noexcept {
    return (c <= 0.03928f) ? (c / 12.92f)
                            : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

static float RelativeLuminance(const Vec4& c) noexcept {
    const float r = SrgbToLinear(std::clamp(c.x, 0.0f, 1.0f));
    const float g = SrgbToLinear(std::clamp(c.y, 0.0f, 1.0f));
    const float b = SrgbToLinear(std::clamp(c.z, 0.0f, 1.0f));
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

float VfxContrastRatio(const Vec4& a, const Vec4& b) noexcept {
    const float la = RelativeLuminance(a);
    const float lb = RelativeLuminance(b);
    const float hi = std::max(la, lb);
    const float lo = std::min(la, lb);
    return (hi + 0.05f) / (lo + 0.05f);
}

} // namespace uz
