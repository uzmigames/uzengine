#pragma once

/// VFX Editor Theme — single source of truth for color, spacing,
/// typography, and iconography.
///
/// Phase 12.1.0. Every widget in the VFX editor sources its rendering
/// decisions from the struct returned by `VfxTheme()`. Hard-coded
/// `Vec4{...}` palette constants in widget .cpp files are forbidden
/// (see `vfx-editor-theme` spec requirement "Single theme source of
/// truth").
///
/// # Design targets
///
/// - Match the look of the reference Niagara editor (dark neutral
///   panels, subtle borders, a single blue accent).
/// - Keep WCAG AA (4.5:1) contrast for primary text on panels and
///   3:1 contrast for dim text + accent strokes.
/// - Allow a later `.uztheme` JSON file + runtime swap without
///   touching any widget code.
///
/// # Token groups
///
/// | Group          | Purpose                                      |
/// |----------------|----------------------------------------------|
/// | Colors         | semantic roles (panelBg, cardBg, accent, …)  |
/// | Spacing        | padding + gap scale (xs..xxl)                |
/// | Typography     | pixel sizes for header / body / caption      |
/// | Radius         | corner-radius scale (sm / md / lg)           |
///
/// @ref docs/engine/25-vfx/53-editor-theme.md
/// @ref UE5 `SNiagaraStack` reference visuals

#include "vector4.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Spacing scale
// ─────────────────────────────────────────────────────────────────────────────

/// Padding + gap pixel values. Use the smallest value that still
/// preserves visual separation — every widget that pads its content
/// MUST pick from this scale, never a raw literal.
struct VfxSpacingScale {
    float xs  =  2.0f;
    float sm  =  4.0f;
    float md  =  8.0f;
    float lg  = 12.0f;
    float xl  = 16.0f;
    float xxl = 24.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// Typography scale
// ─────────────────────────────────────────────────────────────────────────────

/// Typography pixel sizes. `BitmapFont` is currently baked at a single
/// pixel height — these values are advisory hints for widgets that
/// want to communicate intent (e.g. "render this as a header"). When
/// the font is single-size, all four collapse to the bound font's
/// size; callers that need exact sizing MUST use the bound font's
/// actual `pixelHeight`.
struct VfxTypographyScale {
    /// V4 UE5 Slate — menu strip + tooltips, densely packed.
    float menuPx    =  9.0f;
    /// V4 UE5 Slate — default body text / labels / row text.
    float bodyPx    = 10.0f;
    /// V4 UE5 Slate — section captions between body + title.
    float captionPx = 11.0f;
    /// V4 UE5 Slate — header text (tab labels, panel titles).
    float headerPx  = 12.0f;
    /// V4 UE5 Slate — window titles / major headings.
    float titlePx   = 14.0f;
    /// Monospace code / identifier fields.
    float codePx    = 11.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// Corner-radius scale
// ─────────────────────────────────────────────────────────────────────────────

/// Corner-radius scale consumed by `TextRenderer::AddRoundedRect`.
/// V4 UE5 — tight corners throughout, panels render with square
/// corners (`none = 0`). Semantic roles:
///
///   - `none` — 0 px: panels, full-width surfaces
///   - `xs`   — 2 px: buttons, toolbar items
///   - `sm`   — 3 px: tabs, card corners, pills
///   - `md`   — 4 px: dropdown popups, context menus
///   - `lg`   — 6 px: dialog panels, module picker
struct VfxRadiusScale {
    float none = 0.0f;
    float xs   = 2.0f;
    float sm   = 3.0f;
    float md   = 4.0f;
    float lg   = 6.0f;
};

/// Shadow scale — `offset` + `blur` tokens consumed by
/// `AddRoundedRect`. V2 leans slightly heavier than phase 12's
/// `shadow` alpha (0.45) to match Niagara's visual depth cue.
struct VfxShadowScale {
    /// V4 UE5 — lighter shadows than Linear/minimalist. UE5 uses
    /// shadows sparingly; intensities capped at 0.50 alpha.
    struct Drop { float offsetY; float blur; float alpha; };
    Drop hover   { 1.0f,  2.0f, 0.15f };
    Drop popup   { 2.0f,  6.0f, 0.35f };
    Drop dialog  { 4.0f, 12.0f, 0.50f };
};

// ─────────────────────────────────────────────────────────────────────────────
// Color palette
// ─────────────────────────────────────────────────────────────────────────────

/// Semantic color palette. All tokens are Vec4 sRGB with premultiplied
/// alpha where appropriate. Widgets MUST NOT reach into this struct
/// to pick their own shade mix — every color is pre-named.
struct VfxColorPalette {
    // ── Backgrounds (V4 UE5 Slate ladder — five-step greys, opaque) ───
    /// Outer window frame. `#0B0B0B` = UE5 Slate darkest tone.
    Vec4 backgroundBase {0.043f, 0.043f, 0.043f, 1.00f};
    /// Primary panel fill. `#1C1C1C` — UE5 Slate.Panel.
    Vec4 panelBg        {0.110f, 0.110f, 0.110f, 1.00f};
    /// Alternate panel fill (zebra stripe / nested sections).
    /// `#161616` — 1 step darker than `panelBg`.
    Vec4 panelBgAlt     {0.086f, 0.086f, 0.086f, 1.00f};
    /// Title / menu bar fill. `#242424` — UE5 Slate.Header.
    Vec4 barBg          {0.141f, 0.141f, 0.141f, 1.00f};
    /// Dropdown / popup fill. `#2C2C2C` — UE5 Slate.Row (elevates
    /// popups above panel body).
    Vec4 popupBg        {0.173f, 0.173f, 0.173f, 1.00f};

    // ── Cards (stack modules, overview rows) — `#2C2C2C` UE5 Row ──────
    Vec4 cardBg         {0.173f, 0.173f, 0.173f, 1.00f};
    /// Hovered card — `#383838` UE5 Slate.Row.Hover.
    Vec4 cardBgHover    {0.220f, 0.220f, 0.220f, 1.00f};
    /// Selected card — `#1C3A5E` UE5 selected-row blue tint.
    Vec4 cardBgSelected {0.110f, 0.227f, 0.369f, 1.00f};
    /// Dragged card — accent-tinted with slight transparency for
    /// the drop-target affordance.
    Vec4 cardBgDrag     {0.20f, 0.26f, 0.38f, 0.65f};
    /// Disabled card fill.
    Vec4 cardBgDisabled {0.09f, 0.09f, 0.10f, 1.00f};

    // ── Borders + dividers (V4 UE5 — `#1A1A1A` subtle, `#404040` strong) ──
    Vec4 borderSubtle   {0.102f, 0.102f, 0.102f, 1.00f};
    Vec4 borderStrong   {0.251f, 0.251f, 0.251f, 1.00f};
    /// Accent border — same hue as `accentPrimary`.
    Vec4 borderAccent   {0.000f, 0.439f, 0.878f, 1.00f};

    // ── Accents (V4 UE5 — blue `#0070E0` + orange `#FF7B00`) ──────────
    /// Primary accent. UE5 Slate.SelectionColor.
    Vec4 accentPrimary  {0.000f, 0.439f, 0.878f, 1.00f};
    /// Muted accent — darker variant for fills.
    Vec4 accentMuted    {0.000f, 0.298f, 0.596f, 1.00f};
    /// Brightened accent for hover.
    Vec4 accentHover    {0.200f, 0.561f, 0.953f, 1.00f};
    /// Darkened accent for pressed.
    Vec4 accentPressed  {0.000f, 0.329f, 0.659f, 1.00f};
    /// Orange accent — UE5 transient / build-in-progress / warning
    /// highlight. `#FF7B00`.
    Vec4 accentOrange   {1.000f, 0.482f, 0.000f, 1.00f};

    // ── Text (V4 UE5 tiers) ──────────────────────────────────────────
    /// `#E5E5E5` primary.
    Vec4 textPrimary    {0.898f, 0.898f, 0.898f, 1.00f};
    /// `#A0A0A0` secondary (labels).
    Vec4 textSecondary  {0.627f, 0.627f, 0.627f, 1.00f};
    /// `#707070` dim (hints, badges).
    Vec4 textDim        {0.439f, 0.439f, 0.439f, 1.00f};
    /// `#4A4A4A` disabled.
    Vec4 textDisabled   {0.290f, 0.290f, 0.290f, 1.00f};
    /// Accent-coloured text.
    Vec4 textAccent     {0.200f, 0.561f, 0.953f, 1.00f};
    /// Text on accent-coloured background.
    Vec4 textOnAccent   {1.00f, 1.00f, 1.00f, 1.00f};

    // ── State colors ───────────────────────────────────────────────
    /// Success / running state.
    Vec4 stateSuccess   {0.36f, 0.72f, 0.36f, 1.00f};
    /// Warning / paused state.
    Vec4 stateWarning   {0.94f, 0.68f, 0.31f, 1.00f};
    /// Error / invalid state.
    Vec4 stateError     {0.85f, 0.33f, 0.31f, 1.00f};
    /// Info state (burst markers, tooltips).
    Vec4 stateInfo      {0.32f, 0.66f, 0.88f, 1.00f};

    // ── Inputs ─────────────────────────────────────────────────────
    /// Text input field fill.
    Vec4 inputBg        {0.08f, 0.08f, 0.10f, 1.00f};
    /// Focused input fill.
    Vec4 inputBgFocused {0.10f, 0.10f, 0.13f, 1.00f};
    /// Slider track fill.
    Vec4 sliderTrack    {0.10f, 0.10f, 0.13f, 1.00f};
    /// Slider thumb / fill.
    Vec4 sliderThumb    {0.22f, 0.42f, 0.68f, 1.00f};

    // ── Misc ───────────────────────────────────────────────────────
    /// Grid major line (curves editor etc.).
    Vec4 gridMajor      {0.22f, 0.22f, 0.26f, 1.00f};
    /// Grid minor line.
    Vec4 gridMinor      {0.16f, 0.16f, 0.19f, 1.00f};
    /// Scroll thumb.
    Vec4 scrollThumb    {0.35f, 0.35f, 0.40f, 1.00f};
    /// Shadow (drop shadow under popups / cards; expects alpha blend).
    /// Alpha lives here so callers can scale it by
    /// `VfxShadowScale::*::alpha` without editing the colour.
    Vec4 shadow         {0.00f, 0.00f, 0.00f, 1.00f};
    /// Phase 13.5 V2 — accent glow (used for hovered cards + active
    /// selections). Premultiplied alpha handled by the rrect shader.
    Vec4 accentGlow     {0.30f, 0.63f, 1.00f, 0.18f};
    /// Drag insertion gap indicator (stack panel reorder).
    Vec4 insertionGap   {0.35f, 0.75f, 0.95f, 1.00f};
};

// ─────────────────────────────────────────────────────────────────────────────
// Composite theme
// ─────────────────────────────────────────────────────────────────────────────

/// Aggregate theme struct.
struct VfxEditorTheme {
    VfxColorPalette    colors{};
    VfxSpacingScale    spacing{};
    VfxTypographyScale typography{};
    VfxRadiusScale     radius{};
    VfxShadowScale     shadows{};
};

/// Returns the active theme. Currently a static instance; swapping
/// the global pointer for a user-loaded `.uztheme` is a follow-up.
/// Thread-safe: the theme is immutable once constructed.
[[nodiscard]] const VfxEditorTheme& VfxTheme() noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Convenience accessors — save callers from writing `.colors.` each time
// ─────────────────────────────────────────────────────────────────────────────

/// Shorthand for `VfxTheme().colors`.
[[nodiscard]] inline const VfxColorPalette& VfxThemeColors() noexcept {
    return VfxTheme().colors;
}
/// Shorthand for `VfxTheme().spacing`.
[[nodiscard]] inline const VfxSpacingScale& VfxThemeSpacing() noexcept {
    return VfxTheme().spacing;
}
/// Shorthand for `VfxTheme().shadows`.
[[nodiscard]] inline const VfxShadowScale& VfxThemeShadows() noexcept {
    return VfxTheme().shadows;
}
/// Shorthand for `VfxTheme().radius`.
[[nodiscard]] inline const VfxRadiusScale& VfxThemeRadius() noexcept {
    return VfxTheme().radius;
}
/// Shorthand for `VfxTheme().typography`.
[[nodiscard]] inline const VfxTypographyScale& VfxThemeTypography() noexcept {
    return VfxTheme().typography;
}

// ─────────────────────────────────────────────────────────────────────────────
// Contrast utility (used by the spec tests)
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the WCAG 2.x relative-luminance contrast ratio between
/// two sRGB colors (alpha ignored). Range is `[1.0, 21.0]`. Used by
/// `test_vfx_editor_theme.cpp` to assert AA / AAA compliance for the
/// shipped palette.
[[nodiscard]] float VfxContrastRatio(const Vec4& a, const Vec4& b) noexcept;

} // namespace uz
