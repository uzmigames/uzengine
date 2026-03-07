#pragma once

// render_path.h -- Configurable rendering path (Forward / Deferred)
//
// Like UE5 (r.Forward CVar) and Godot (rendering_method project setting),
// UzEngine supports both forward and deferred shading. The render path
// can be set per-project or toggled at runtime via WS API.

#include <cstdint>

namespace uz {

enum class RenderPath : uint8_t {
    Forward  = 0,  // Single-pass PBR: geometry + lighting in one draw
    Deferred = 1,  // G-Buffer write + fullscreen deferred lighting pass
};

} // namespace uz
