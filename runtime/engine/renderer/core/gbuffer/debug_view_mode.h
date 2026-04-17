/// UzEngine Debug View Mode — Selects which G-Buffer channel or rendering
/// state to visualize in real-time for debugging and profiling.
///
/// `DebugViewMode::Lit` is the default — no debug pass is executed, and
/// the normal PBR lighting output is shown. All other modes replace the
/// backbuffer with a diagnostic visualization of the selected channel.
///
/// # Usage
/// ```cpp
/// renderer.SetDebugViewMode(DebugViewMode::WorldNormal);
/// // Next frame shows octahedral-decoded normals remapped to [0,1]
///
/// renderer.SetDebugViewMode(DebugViewMode::Lit);
/// // Reverts to normal PBR output
/// ```
///
/// # String Conversion
/// `DebugViewModeName()` and `DebugViewModeFromString()` enable CLI arg
/// parsing and log output without manual switch statements.

#pragma once

#include <cstdint>
#include <string_view>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DebugViewMode — channel selector enum
// ─────────────────────────────────────────────────────────────────────────────

/// Debug view mode selector — determines which G-Buffer channel or derived
/// quantity is visualized when debug rendering is active.
///
/// Numeric values must remain stable (serialized to CLI args and IPC).
/// The shader reads `mode` as a uint32 root constant and branches on it.
enum class DebugViewMode : uint32_t {
    Lit           = 0,  ///< Normal PBR output (default — debug pass skipped entirely).
    Albedo        = 1,  ///< G-Buffer RT0 RGB (base color, linear).
    WorldNormal   = 2,  ///< Octahedral-decoded RT1 RG, remapped [-1,1] -> [0,1].
    Metallic      = 3,  ///< RT1 blue channel (grayscale: 0=dielectric, 1=metal).
    Roughness     = 4,  ///< RT1 alpha channel (grayscale: 0=mirror, 1=fully rough).
    AO            = 5,  ///< RT0 alpha channel (grayscale: 1=fully lit).
    Depth         = 6,  ///< Linearized depth (white=near, black=far).
    WorldPosition = 7,  ///< RT2 XYZ world position (fractional color grid).
    Lighting      = 8,  ///< Direct lighting Lo without albedo multiply.
    Shadows       = 9,  ///< Shadow factor (0=shadowed=black, 1=lit=white).
    Wireframe     = 10, ///< Not implemented in post-process — handled by rasterizer state.
    Unlit         = 11, ///< Alias for Albedo (same G-Buffer read, no lighting).
    Overview      = 12, ///< Composite grid of all channels (2x3 or similar layout).
    Tangent        = 13, ///< Derived tangent direction (cross(N, up) approximation).
    Faceted        = 14, ///< Highlight hard edges — pixels where adjacent normals diverge.

    // Extended view modes (toolbar additions)
    DetailLighting = 15, ///< Directional light × NdotL only — no albedo, no IBL. Shows pure shading term. (ALT+5)
    LightingOnly   = 16, ///< Full lighting (direct + indirect + IBL) without albedo multiply. Shows light intensity distribution. (ALT+6)
    LitWireframe   = 17, ///< Normal Lit output with wireframe overlay (dual-pass: lit + wireframe blend).
    Reflections    = 18, ///< SSR + IBL specular contribution only. Isolates reflection quality.

    Count          = 19, ///< Sentinel — number of valid modes (not a displayable mode).
};

// ─────────────────────────────────────────────────────────────────────────────
// String conversion utilities
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a human-readable name for the given debug view mode.
///
/// # Arguments
/// * `mode` — The debug view mode to convert.
///
/// # Returns
/// Static string literal (never null). Returns "Unknown" for out-of-range values.
inline const char* DebugViewModeName(DebugViewMode mode) {
    switch (mode) {
        case DebugViewMode::Lit:           return "Lit";
        case DebugViewMode::Albedo:        return "Albedo";
        case DebugViewMode::WorldNormal:   return "WorldNormal";
        case DebugViewMode::Metallic:      return "Metallic";
        case DebugViewMode::Roughness:     return "Roughness";
        case DebugViewMode::AO:            return "AO";
        case DebugViewMode::Depth:         return "Depth";
        case DebugViewMode::WorldPosition: return "WorldPosition";
        case DebugViewMode::Lighting:      return "Lighting";
        case DebugViewMode::Shadows:       return "Shadows";
        case DebugViewMode::Wireframe:     return "Wireframe";
        case DebugViewMode::Unlit:         return "Unlit";
        case DebugViewMode::Overview:      return "Overview";
        case DebugViewMode::Tangent:        return "Tangent";
        case DebugViewMode::Faceted:        return "Faceted";
        case DebugViewMode::DetailLighting: return "DetailLighting";
        case DebugViewMode::LightingOnly:   return "LightingOnly";
        case DebugViewMode::LitWireframe:   return "LitWireframe";
        case DebugViewMode::Reflections:    return "Reflections";
        case DebugViewMode::Count:          return "Count";
    }
    return "Unknown";
}

/// Parses a debug view mode from a string (case-insensitive).
///
/// # Arguments
/// * `name` — The string to parse (e.g., "albedo", "WorldNormal", "DEPTH").
///
/// # Returns
/// The matching `DebugViewMode` if found, or `DebugViewMode::Lit` if the
/// string does not match any known mode name.
inline DebugViewMode DebugViewModeFromString(std::string_view name) {
    // Case-insensitive comparison via manual tolower.
    auto iequals = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            const char ca = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] + 32) : a[i];
            const char cb = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] + 32) : b[i];
            if (ca != cb) return false;
        }
        return true;
    };

    if (iequals(name, "lit"))           return DebugViewMode::Lit;
    if (iequals(name, "albedo"))        return DebugViewMode::Albedo;
    if (iequals(name, "worldnormal"))   return DebugViewMode::WorldNormal;
    if (iequals(name, "metallic"))      return DebugViewMode::Metallic;
    if (iequals(name, "roughness"))     return DebugViewMode::Roughness;
    if (iequals(name, "ao"))            return DebugViewMode::AO;
    if (iequals(name, "depth"))         return DebugViewMode::Depth;
    if (iequals(name, "worldposition")) return DebugViewMode::WorldPosition;
    if (iequals(name, "lighting"))      return DebugViewMode::Lighting;
    if (iequals(name, "shadows"))       return DebugViewMode::Shadows;
    if (iequals(name, "wireframe"))     return DebugViewMode::Wireframe;
    if (iequals(name, "unlit"))         return DebugViewMode::Unlit;
    if (iequals(name, "overview"))      return DebugViewMode::Overview;
    if (iequals(name, "tangent"))        return DebugViewMode::Tangent;
    if (iequals(name, "faceted"))        return DebugViewMode::Faceted;
    if (iequals(name, "detaillighting")) return DebugViewMode::DetailLighting;
    if (iequals(name, "lightingonly"))   return DebugViewMode::LightingOnly;
    if (iequals(name, "litwireframe"))   return DebugViewMode::LitWireframe;
    if (iequals(name, "reflections"))    return DebugViewMode::Reflections;

    return DebugViewMode::Lit;
}

} // namespace uz
