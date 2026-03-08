#pragma once

// project_config.h -- .uzproj TOML project configuration loader/saver.
//
// Parses a TOML file with project metadata and renderer settings.
// All fields have sensible defaults; only [project].name is required.

#include "engine/core/result.h"

#include <string>

namespace uz {

// ============================================================================
// Resolution -- integer width/height pair for renderer resolution config
// ============================================================================

struct Resolution {
    int width  = 1920;
    int height = 1080;

    [[nodiscard]] constexpr bool operator==(const Resolution& o) const noexcept {
        return width == o.width && height == o.height;
    }
    [[nodiscard]] constexpr bool operator!=(const Resolution& o) const noexcept {
        return !(*this == o);
    }
};

// ============================================================================
// RendererConfig -- [renderer] table fields
// ============================================================================

struct RendererConfig {
    bool vsync       = true;
    int  target_fps  = 60;
    Resolution resolution{1920, 1080};

    [[nodiscard]] bool operator==(const RendererConfig& o) const noexcept {
        return vsync == o.vsync
            && target_fps == o.target_fps
            && resolution == o.resolution;
    }
    [[nodiscard]] bool operator!=(const RendererConfig& o) const noexcept {
        return !(*this == o);
    }
};

// ============================================================================
// ProjectConfig -- full .uzproj configuration
// ============================================================================

struct ProjectConfig {
    std::string    name;
    std::string    version;
    std::string    main_scene;
    RendererConfig renderer;

    // -- Factory methods ----------------------------------------------------

    /// Returns a config with sensible defaults. name = "Untitled".
    [[nodiscard]] static ProjectConfig defaults() noexcept;

    // -- I/O ----------------------------------------------------------------

    /// Parse a .uzproj TOML file at `path`.
    /// Returns Err if the file cannot be read or the TOML is malformed.
    [[nodiscard]] static Result<ProjectConfig, std::string> load(const std::string& path);

    /// Serialize this config back to TOML and write it to `path`.
    /// Returns Err if the file cannot be written.
    [[nodiscard]] static Result<bool, std::string> save(const ProjectConfig& config,
                                                        const std::string& path);

    // -- Comparison ---------------------------------------------------------

    [[nodiscard]] bool operator==(const ProjectConfig& o) const noexcept {
        return name == o.name
            && version == o.version
            && main_scene == o.main_scene
            && renderer == o.renderer;
    }
    [[nodiscard]] bool operator!=(const ProjectConfig& o) const noexcept {
        return !(*this == o);
    }
};

} // namespace uz
