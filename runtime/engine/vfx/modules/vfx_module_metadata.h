#pragma once

/// VFX module metadata catalog — category, description, and stage compatibility.
///
/// Phase 11.3.0. Static companion table to `ModuleRegistry`. Registry holds
/// the runtime factory; this table holds editor-facing metadata (category
/// tab, one-line description, which `VfxStage`s can host the module). Kept
/// separate so the runtime registry stays free of UI strings and can still
/// be tested in isolation.
///
/// # Lookup
///
/// ```cpp
/// const VfxModuleCatalogEntry* e = VfxModuleMetadata::Find("curl_noise");
/// if (e != nullptr) {
///     // e->category     == "Forces"
///     // e->description  == "Perlin-style curl noise advection."
///     // e->IsValidInStage(VfxStage::ParticleUpdate) == true
/// }
/// ```
///
/// # Categories
///
/// - `"Simulation"` — spawn, update, decay, turbulence, constraint.
/// - `"Forces"`     — attractor, vector_field, orbit, pin, drag-like.
/// - `"Collision"`  — collision, distance_constraint.
/// - `"Render"`     — sprite, mesh, light renderers.
/// - `"DI"`         — data-interface modules (skeletal_spawn, audio_spectrum,
///                   render_target_di, neighbor_interaction, async_gpu_trace).
/// - `"Dynamic"`    — dynamic material parameter feeds.
/// - `"Events"`     — sub-emitters.
/// - `"Custom"`     — user modules outside the built-in table.
///
/// @ref engine/vfx/modules/module_registry.h
/// @ref engine/vfx/stack/vfx_stage.h

#include "engine/vfx/stack/vfx_stage.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Catalog entry
// ─────────────────────────────────────────────────────────────────────────────

/// One row in the static metadata catalog.
struct VfxModuleCatalogEntry {
    /// Registered module name (matches `ModuleRegistry::Register` key).
    const char* name = nullptr;

    /// Category tab label (see file header). Stable across runs.
    const char* category = nullptr;

    /// One-line description shown under each tile in the picker.
    const char* description = nullptr;

    /// Bitmask over `VfxStage` — bit N = compatible with
    /// `static_cast<uint8_t>(stage) == N`.
    uint32_t validStageMask = 0;

    /// Phase 12.3.0 — icon glyph rendered on the picker tile + stack
    /// card. ASCII-safe constant from `vfx_editor_icons.h`. Never null
    /// for built-in entries (a value is always present, even if only a
    /// fallback category glyph).
    const char* iconGlyph = nullptr;

    /// Returns true when the module may be added to `stage`.
    [[nodiscard]] constexpr bool IsValidInStage(VfxStage stage) const noexcept {
        const uint32_t bit = 1u << static_cast<uint8_t>(stage);
        return (validStageMask & bit) != 0;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Static catalog access
// ─────────────────────────────────────────────────────────────────────────────

/// Stage-bitmask builder (constexpr so the catalog table can be constexpr).
[[nodiscard]] constexpr uint32_t VfxStageBits() noexcept { return 0u; }

template <typename... Stages>
[[nodiscard]] constexpr uint32_t VfxStageBits(VfxStage s, Stages... rest) noexcept {
    return (1u << static_cast<uint8_t>(s)) | VfxStageBits(rest...);
}

/// Static metadata catalog accessor. All operations are O(N) scans over the
/// built-in table (≤ 24 entries); no hashing needed.
class VfxModuleMetadata {
public:
    /// Returns a pointer to the entry for `name`, or nullptr on miss.
    /// Matching is exact (case-sensitive).
    [[nodiscard]] static const VfxModuleCatalogEntry* Find(const char* name) noexcept;
    [[nodiscard]] static const VfxModuleCatalogEntry* Find(const std::string& name) noexcept {
        return Find(name.c_str());
    }

    /// Returns the category for `name`, or `"Custom"` when the module is
    /// not in the built-in catalog.
    [[nodiscard]] static std::string GetCategory(const std::string& name);

    /// Returns the description for `name`, or an empty string on miss.
    [[nodiscard]] static std::string GetDescription(const std::string& name);

    /// Phase 12.3.0 — returns the icon glyph for `name`, or the
    /// generic-category fallback when the module is not in the built-in
    /// catalog. Always returns a non-null, non-empty C string.
    [[nodiscard]] static const char* GetIconGlyph(const std::string& name) noexcept;

    /// Phase 12.3.0 — returns the count of built-in modules whose
    /// `category == category` AND whose `validStageMask` allows `stage`.
    /// Used by the picker's category-tab badges ("Forces · 4").
    [[nodiscard]] static size_t CountByCategoryAndStage(
        const std::string& category, VfxStage stage) noexcept;

    /// Returns whether the module is compatible with the stage. Unknown
    /// modules are treated as compatible with every stage (the picker
    /// relies on the author knowing what they're doing for custom modules).
    [[nodiscard]] static bool IsValidInStage(const std::string& name,
                                              VfxStage stage) noexcept;

    /// Returns a pointer to the raw catalog table and its size. The
    /// pointer is stable across calls — underlying table is `static
    /// constexpr`.
    [[nodiscard]] static const VfxModuleCatalogEntry* Table(size_t& outCount) noexcept;

    /// Returns distinct category names in catalog order of first
    /// appearance. Always includes `"All"` as the first entry so
    /// dialogs can show a tab that matches everything.
    [[nodiscard]] static std::vector<std::string> Categories();

    /// Returns the names of every built-in module whose category matches
    /// `category` ("All" matches everything) and whose `validStageMask`
    /// contains `stage`. Case-sensitive.
    [[nodiscard]] static std::vector<std::string> FilterByCategoryAndStage(
        const std::string& category, VfxStage stage);
};

} // namespace uz
