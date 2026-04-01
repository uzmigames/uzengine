/// UzEngine Quality Preset Manager
///
/// Unified quality scalability system with 5 preset levels that map to
/// CVar bundles across 7 rendering groups. Inspired by industry-standard
/// scalability systems in production game engines.
///
/// # Quality Levels
/// | Level | Name       | Target                          |
/// |-------|------------|---------------------------------|
/// | 0     | Low        | Integrated GPU, 30 FPS          |
/// | 1     | Medium     | Entry GPU, 60 FPS               |
/// | 2     | High       | Mid-range GPU, 60 FPS           |
/// | 3     | Ultra      | High-end GPU, unlocked FPS      |
/// | 4     | Cinematic  | RTX 4090+, visual fidelity      |
///
/// # Usage
/// ```cpp
/// auto& qm = uz::QualityPresetManager::Get();
/// qm.ApplyPreset(QualityLevel::High);           // Apply all groups
/// qm.ApplyGroupPreset("shadows", 4);            // Cinematic shadows only
/// int level = qm.GetGroupLevel("shadows");       // Query current level
/// ```

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Quality Level Enum
// ─────────────────────────────────────────────────────────────────────────────

/// Global quality preset level.
enum class QualityLevel : uint8_t {
    Low       = 0,  ///< Minimal quality, maximum performance.
    Medium    = 1,  ///< Balanced quality for entry-level hardware.
    High      = 2,  ///< Good quality for mid-range hardware.
    Ultra     = 3,  ///< Maximum quality for high-end hardware (default).
    Cinematic = 4,  ///< Highest fidelity, no performance constraints.
    Count     = 5
};

/// Returns a human-readable name for the quality level.
[[nodiscard]] const char* QualityLevelName(QualityLevel level) noexcept;

/// Parse a quality level from string ("low", "medium", "high", "ultra", "cinematic").
/// Returns Ultra if unrecognized.
[[nodiscard]] QualityLevel ParseQualityLevel(const std::string& str) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Quality Preset Manager
// ─────────────────────────────────────────────────────────────────────────────

/// Manages quality presets across rendering groups.
///
/// Each group (shadows, textures, postprocess, etc.) has its own level (0-4)
/// that maps to a set of CVar overrides. Changing a group's level immediately
/// applies the corresponding CVars via CVarRegistry.
///
/// The overall quality level is the minimum across all groups.
class QualityPresetManager {
public:
    /// Returns the process-wide singleton instance.
    static QualityPresetManager& Get();

    /// Number of quality levels (Low through Cinematic).
    static constexpr int kLevelCount = static_cast<int>(QualityLevel::Count);

    /// Group names (used as keys in per-group APIs).
    static constexpr const char* kGroupShadows      = "shadows";
    static constexpr const char* kGroupTextures      = "textures";
    static constexpr const char* kGroupPostProcess   = "postprocess";
    static constexpr const char* kGroupEffects       = "effects";
    static constexpr const char* kGroupViewDistance   = "view_distance";
    static constexpr const char* kGroupGI            = "gi";
    static constexpr const char* kGroupScreen        = "screen";

    /// Apply a preset level to ALL groups at once.
    void ApplyPreset(QualityLevel level);

    /// Apply a preset level to a single group.
    /// @param groupName One of the kGroup* constants.
    /// @param level     0-4 (clamped).
    void ApplyGroupPreset(const char* groupName, int level);

    /// Get the current level for a specific group.
    /// Returns -1 if the group name is invalid.
    [[nodiscard]] int GetGroupLevel(const char* groupName) const;

    /// Get the overall quality level (minimum across all groups).
    [[nodiscard]] QualityLevel GetOverallLevel() const;

    /// Initialize with default preset (Ultra).
    void Init();

private:
    QualityPresetManager() = default;

    /// A single CVar override: name + value per quality level.
    struct CvarPreset {
        const char* cvarName;
        float       values[kLevelCount]; // [Low, Medium, High, Ultra, Cinematic]
    };

    /// A quality group with its CVar presets.
    struct Group {
        const char*             name;
        int                     currentLevel = 3; // default Ultra
        std::vector<CvarPreset> presets;
    };

    /// Register all built-in groups and their CVar presets.
    void RegisterBuiltinPresets();

    /// Apply all CVars for a group at the given level.
    void ApplyGroup(Group& group, int level);

    /// Find a group by name. Returns nullptr if not found.
    [[nodiscard]] Group* FindGroup(const char* name);
    [[nodiscard]] const Group* FindGroup(const char* name) const;

    std::vector<Group> m_groups;
    bool m_initialized = false;
};

} // namespace uz
