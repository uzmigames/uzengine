/// UzEngine ShowFlags — Per-feature toggles polled by render passes.
///
/// Mirrors UE5's `FEngineShowFlags`: a wide set of booleans the editor
/// viewport toolbar exposes so artists can disable features (grid, fog,
/// bloom, shadows, ...) without restarting. Each render pass polls the
/// relevant flag and early-outs when disabled.
///
/// Defaults: every flag is ENABLED. The editor starts in a state visually
/// identical to previous builds; only user-initiated toggles change output.
///
/// # Usage
/// ```cpp
/// ShowFlags flags;                                // all enabled
/// flags.Set(ShowFlag::Fog, false);                // hide fog
/// if (flags.Get(ShowFlag::Grid)) { ... }           // pass polls
/// uint64_t bits = flags.ToBits();                 // serialize to IPC
/// ShowFlags restored = ShowFlags::FromBits(bits); // deserialize
/// ```
///
/// # Serialization
/// The `ShowFlag` enum values are stable bit indices. Adding a flag means
/// appending to the end of the enum — never reordering, never re-using
/// abandoned indices. The 64-bit width is a hard ceiling for Phase 0;
/// switch to `std::bitset<N>` if a future phase exceeds 64 flags.
///
/// # Thread Safety
/// NOT thread-safe. Edited on the game/editor thread, polled on the render
/// thread inside `RenderFrame`. Toolbar toggles must be applied between
/// frames (before `BeginFrame`) — do not mutate from a render pass callback.

#pragma once

#include <cstdint>
#include <string_view>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ShowFlag — bit index enum
// ─────────────────────────────────────────────────────────────────────────────

/// Bit index for each toggleable feature. Values are stable — serialized
/// into `ShowFlags::ToBits()` output and over IPC. Never renumber existing
/// entries; append new flags before `Count`.
enum class ShowFlag : uint8_t {
    // Editor overlays
    Grid              = 0,  ///< Editor floor grid.
    EditorOverlays    = 1,  ///< Selection outlines, gizmos, helpers.
    Stats             = 2,  ///< Frame-time stats HUD.

    // Geometry categories
    StaticMeshes      = 3,  ///< ECS `StaticMeshComponent` draws.
    SkeletalMeshes    = 4,  ///< ECS `SkeletalMeshComponent` draws.
    Particles         = 5,  ///< VFX/particle emitters.
    Decals            = 6,  ///< Deferred decals.
    Translucency      = 7,  ///< Forward translucent pass.

    // Lighting
    DirectionalLight  = 8,  ///< Primary sun light.
    PointLights       = 9,  ///< All point lights.
    SpotLights        = 10, ///< All spot lights.
    Shadows           = 11, ///< Shadow mapping (CSM + local atlas).
    ContactShadows    = 12, ///< Screen-space contact shadows.

    // Atmosphere & volumetrics
    Atmosphere        = 13, ///< Sky atmosphere pass.
    Clouds            = 14, ///< Volumetric clouds.
    Fog               = 15, ///< Exponential height fog.
    Volumetrics       = 16, ///< Volumetric fog / light shafts.

    // Global illumination & reflections
    AO                = 17, ///< GTAO / SSAO.
    SSR               = 18, ///< Screen-space reflections.
    SSGI              = 19, ///< Screen-space global illumination.
    IBL               = 20, ///< Image-based lighting (env cubemap + SH).

    // Post-processing
    PostProcess       = 21, ///< Master toggle — disables all post after lighting.
    AntiAliasing      = 22, ///< TAA / FXAA / DLSS.
    Bloom             = 23, ///< Bloom.
    MotionBlur        = 24, ///< Camera + object motion blur.
    DOF               = 25, ///< Depth of field.
    LensFlare         = 26, ///< Lens flare / bokeh.
    Tonemap           = 27, ///< Tonemap operator (off = pass-through).

    Count             = 28, ///< Sentinel — number of valid flags. Not a flag.
};

// ─────────────────────────────────────────────────────────────────────────────
// ShowFlags — 64-bit bitfield
// ─────────────────────────────────────────────────────────────────────────────

/// 64-bit bitfield of per-feature toggles. Default-constructed with every
/// flag enabled.
struct ShowFlags {
    uint64_t bits = kAllEnabled();

    // ── Factory helpers ─────────────────────────────────────────────────────

    /// All flags enabled (equivalent to default construction).
    [[nodiscard]] static constexpr ShowFlags AllEnabled() noexcept {
        return ShowFlags{kAllEnabled()};
    }

    /// All flags disabled — useful for minimal preview viewports.
    [[nodiscard]] static constexpr ShowFlags AllDisabled() noexcept {
        return ShowFlags{0ull};
    }

    /// Reconstructs from a raw 64-bit pattern (IPC / serialization inverse
    /// of `ToBits()`).
    [[nodiscard]] static constexpr ShowFlags FromBits(uint64_t raw) noexcept {
        return ShowFlags{raw};
    }

    // ── Accessors ───────────────────────────────────────────────────────────

    /// Returns true if the given flag is enabled.
    [[nodiscard]] constexpr bool Get(ShowFlag flag) const noexcept {
        return (bits & Mask(flag)) != 0ull;
    }

    /// Sets the given flag to `enabled`.
    constexpr void Set(ShowFlag flag, bool enabled) noexcept {
        if (enabled) { bits |= Mask(flag); }
        else         { bits &= ~Mask(flag); }
    }

    /// Toggles the given flag and returns its new value.
    constexpr bool Toggle(ShowFlag flag) noexcept {
        bits ^= Mask(flag);
        return Get(flag);
    }

    /// Enables every flag.
    constexpr void EnableAll() noexcept { bits = kAllEnabled(); }

    /// Disables every flag.
    constexpr void DisableAll() noexcept { bits = 0ull; }

    /// Raw 64-bit pattern for IPC / serialization.
    [[nodiscard]] constexpr uint64_t ToBits() const noexcept { return bits; }

    // ── Equality ────────────────────────────────────────────────────────────

    [[nodiscard]] constexpr bool operator==(const ShowFlags& other) const noexcept {
        return bits == other.bits;
    }
    [[nodiscard]] constexpr bool operator!=(const ShowFlags& other) const noexcept {
        return bits != other.bits;
    }

private:
    /// Bitmask for a single flag index. `ShowFlag::Count` is a sentinel and
    /// must never be passed here — callers should only use named flags.
    [[nodiscard]] static constexpr uint64_t Mask(ShowFlag flag) noexcept {
        return 1ull << static_cast<uint8_t>(flag);
    }

    /// Pattern with every defined flag bit set (bits >= Count are left 0).
    [[nodiscard]] static constexpr uint64_t kAllEnabled() noexcept {
        const auto n = static_cast<uint8_t>(ShowFlag::Count);
        return (n >= 64) ? ~0ull : ((1ull << n) - 1ull);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// String conversion utilities
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a stable human-readable name for the given flag. The returned
/// string is also the CVar suffix used by `r.Show.<Name>` and the IPC
/// command field — do not rename without also updating both.
[[nodiscard]] inline const char* ShowFlagName(ShowFlag flag) noexcept {
    switch (flag) {
        case ShowFlag::Grid:             return "Grid";
        case ShowFlag::EditorOverlays:   return "EditorOverlays";
        case ShowFlag::Stats:            return "Stats";
        case ShowFlag::StaticMeshes:     return "StaticMeshes";
        case ShowFlag::SkeletalMeshes:   return "SkeletalMeshes";
        case ShowFlag::Particles:        return "Particles";
        case ShowFlag::Decals:           return "Decals";
        case ShowFlag::Translucency:     return "Translucency";
        case ShowFlag::DirectionalLight: return "DirectionalLight";
        case ShowFlag::PointLights:      return "PointLights";
        case ShowFlag::SpotLights:       return "SpotLights";
        case ShowFlag::Shadows:          return "Shadows";
        case ShowFlag::ContactShadows:   return "ContactShadows";
        case ShowFlag::Atmosphere:       return "Atmosphere";
        case ShowFlag::Clouds:           return "Clouds";
        case ShowFlag::Fog:              return "Fog";
        case ShowFlag::Volumetrics:      return "Volumetrics";
        case ShowFlag::AO:               return "AO";
        case ShowFlag::SSR:              return "SSR";
        case ShowFlag::SSGI:             return "SSGI";
        case ShowFlag::IBL:              return "IBL";
        case ShowFlag::PostProcess:      return "PostProcess";
        case ShowFlag::AntiAliasing:     return "AntiAliasing";
        case ShowFlag::Bloom:            return "Bloom";
        case ShowFlag::MotionBlur:       return "MotionBlur";
        case ShowFlag::DOF:              return "DOF";
        case ShowFlag::LensFlare:        return "LensFlare";
        case ShowFlag::Tonemap:          return "Tonemap";
        case ShowFlag::Count:            return "Count";
    }
    return "Unknown";
}

/// Parses a flag name (case-insensitive). Returns `true` on success and
/// writes the parsed flag to `*out`; returns `false` on no match.
[[nodiscard]] inline bool ShowFlagFromString(std::string_view name, ShowFlag* out) noexcept {
    auto iequals = [](std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            const char ca = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] + 32) : a[i];
            const char cb = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] + 32) : b[i];
            if (ca != cb) return false;
        }
        return true;
    };

    const auto n = static_cast<uint8_t>(ShowFlag::Count);
    for (uint8_t i = 0; i < n; ++i) {
        const auto flag = static_cast<ShowFlag>(i);
        if (iequals(name, ShowFlagName(flag))) {
            if (out) { *out = flag; }
            return true;
        }
    }
    return false;
}

} // namespace uz
