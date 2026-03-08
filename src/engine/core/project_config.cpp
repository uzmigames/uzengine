// project_config.cpp -- .uzproj TOML project configuration loader/saver.

#include "engine/core/project_config.h"

#include <toml++/toml.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace uz {

// ============================================================================
// defaults()
// ============================================================================

ProjectConfig ProjectConfig::defaults() noexcept {
    ProjectConfig cfg;
    cfg.name       = "Untitled";
    cfg.version    = "0.1.0";
    cfg.main_scene = "scenes/main.uzscene";
    cfg.renderer   = RendererConfig{};
    return cfg;
}

// ============================================================================
// load()
// ============================================================================

Result<ProjectConfig, std::string> ProjectConfig::load(const std::string& path) {
    // Attempt to parse the TOML file.
    toml::parse_result parsed;
    try {
        parsed = toml::parse_file(path);
    } catch (const toml::parse_error& err) {
        // Covers both "file not found" and "malformed TOML" from toml++.
        std::ostringstream oss;
        oss << err;
        return Result<ProjectConfig, std::string>::Err(oss.str());
    }

    const auto& tbl = parsed;

    // [project] table -- name is required.
    const auto project_tbl = tbl["project"];
    if (!project_tbl || !project_tbl.is_table()) {
        return Result<ProjectConfig, std::string>::Err(
            "missing required [project] table");
    }

    auto name_node = project_tbl["name"];
    if (!name_node || !name_node.is_string()) {
        return Result<ProjectConfig, std::string>::Err(
            "missing required field: project.name (string)");
    }

    ProjectConfig cfg;
    cfg.name = name_node.as_string()->get();

    // Optional fields with defaults.
    if (auto v = project_tbl["version"]; v && v.is_string()) {
        cfg.version = v.as_string()->get();
    } else {
        cfg.version = "0.1.0";
    }

    if (auto v = project_tbl["main_scene"]; v && v.is_string()) {
        cfg.main_scene = v.as_string()->get();
    } else {
        cfg.main_scene = "scenes/main.uzscene";
    }

    // [renderer] table -- entirely optional.
    const auto renderer_tbl = tbl["renderer"];
    if (renderer_tbl && renderer_tbl.is_table()) {
        if (auto v = renderer_tbl["vsync"]; v && v.is_boolean()) {
            cfg.renderer.vsync = v.as_boolean()->get();
        }
        if (auto v = renderer_tbl["target_fps"]; v && v.is_integer()) {
            cfg.renderer.target_fps = static_cast<int>(v.as_integer()->get());
        }

        const auto res_tbl = renderer_tbl["resolution"];
        if (res_tbl && res_tbl.is_table()) {
            if (auto w = res_tbl["width"]; w && w.is_integer()) {
                cfg.renderer.resolution.width = static_cast<int>(w.as_integer()->get());
            }
            if (auto h = res_tbl["height"]; h && h.is_integer()) {
                cfg.renderer.resolution.height = static_cast<int>(h.as_integer()->get());
            }
        }
    }

    return Result<ProjectConfig, std::string>::Ok(std::move(cfg));
}

// ============================================================================
// save()
// ============================================================================

Result<bool, std::string> ProjectConfig::save(const ProjectConfig& config,
                                              const std::string& path) {
    // Build the TOML table manually.
    toml::table tbl;

    toml::table project_tbl;
    project_tbl.insert("name", config.name);
    project_tbl.insert("version", config.version);
    project_tbl.insert("main_scene", config.main_scene);
    tbl.insert("project", std::move(project_tbl));

    toml::table renderer_tbl;
    renderer_tbl.insert("vsync", config.renderer.vsync);
    renderer_tbl.insert("target_fps", static_cast<int64_t>(config.renderer.target_fps));

    toml::table res_tbl;
    res_tbl.insert("width", static_cast<int64_t>(config.renderer.resolution.width));
    res_tbl.insert("height", static_cast<int64_t>(config.renderer.resolution.height));
    renderer_tbl.insert("resolution", std::move(res_tbl));

    tbl.insert("renderer", std::move(renderer_tbl));

    // Write to file.
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return Result<bool, std::string>::Err(
            "failed to open file for writing: " + path);
    }

    out << tbl;
    out.flush();

    if (out.fail()) {
        return Result<bool, std::string>::Err(
            "write error for file: " + path);
    }

    return Result<bool, std::string>::Ok(true);
}

} // namespace uz
