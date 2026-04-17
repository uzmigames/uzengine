/// UzEngine Orthographic View Presets — String utilities.

#include "ortho_view.h"

#include <algorithm>
#include <cctype>

namespace uz {

const char* OrthoViewName(OrthoView view) noexcept {
    switch (view) {
        case OrthoView::Top:    return "Top";
        case OrthoView::Bottom: return "Bottom";
        case OrthoView::Front:  return "Front";
        case OrthoView::Back:   return "Back";
        case OrthoView::Left:   return "Left";
        case OrthoView::Right:  return "Right";
        default:                return "None";
    }
}

OrthoView OrthoViewFromString(std::string_view str) noexcept {
    // Case-insensitive compare via lowercase transform.
    char buf[16] = {};
    const size_t n = std::min(str.size(), sizeof(buf) - 1);
    for (size_t i = 0; i < n; ++i) {
        buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(str[i])));
    }
    const std::string_view lower(buf, n);

    if (lower == "top")    return OrthoView::Top;
    if (lower == "bottom") return OrthoView::Bottom;
    if (lower == "front")  return OrthoView::Front;
    if (lower == "back")   return OrthoView::Back;
    if (lower == "left")   return OrthoView::Left;
    if (lower == "right")  return OrthoView::Right;
    return OrthoView::None;
}

} // namespace uz
