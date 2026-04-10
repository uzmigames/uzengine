/// project_loader.cpp — Implementation of .uzproject JSON parsing.
///
/// Reads a project file from disk using std::ifstream and parses it with
/// nlohmann::json. All field extractions use value_or() semantics so that
/// missing keys produce the ProjectConfig defaults without any special-casing.

#include "project_loader.h"

#include "../logging/log.h"

#include <json.hpp>

#include <fstream>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Extracts a string field from a JSON object, returning `defaultValue` if the
/// key is absent or the value is not a string.
[[nodiscard]] std::string JsonString(const nlohmann::json& obj,
                                     const char* key,
                                     const std::string& defaultValue = {}) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        return defaultValue;
    }
    return it->get<std::string>();
}

/// Extracts an integer field from a JSON object, returning `defaultValue` if
/// the key is absent or the value is not a number.
[[nodiscard]] int JsonInt(const nlohmann::json& obj, const char* key, int defaultValue) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_integer()) {
        return defaultValue;
    }
    return it->get<int>();
}

/// Extracts a boolean field from a JSON object, returning `defaultValue` if
/// the key is absent or the value is not a boolean.
[[nodiscard]] bool JsonBool(const nlohmann::json& obj, const char* key, bool defaultValue) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_boolean()) {
        return defaultValue;
    }
    return it->get<bool>();
}

/// Parses the "window" sub-object into the relevant ProjectConfig fields.
///
/// All sub-fields are optional. Defaults are taken from `cfg` as already
/// populated by the struct initializer.
void ParseWindowSection(const nlohmann::json& root, ProjectConfig& cfg) {
    auto it = root.find("window");
    if (it == root.end() || !it->is_object()) {
        return;
    }
    const auto& win = *it;
    cfg.windowTitle  = JsonString(win, "title",      cfg.windowTitle);
    cfg.windowWidth  = JsonInt   (win, "width",      cfg.windowWidth);
    cfg.windowHeight = JsonInt   (win, "height",     cfg.windowHeight);
    cfg.fullscreen   = JsonBool  (win, "fullscreen", cfg.fullscreen);
}

/// Parses the "render" sub-object into the relevant ProjectConfig fields.
void ParseRenderSection(const nlohmann::json& root, ProjectConfig& cfg) {
    auto it = root.find("render");
    if (it == root.end() || !it->is_object()) {
        return;
    }
    const auto& render = *it;
    cfg.renderBackend = JsonString(render, "backend", cfg.renderBackend);
    cfg.vsync         = JsonBool  (render, "vsync",   cfg.vsync);
}

/// Parses the optional "cvars" sub-object into `cfg.cvarOverrides`.
///
/// Accepted value types per entry:
///   - number  → stored as float
///   - boolean → stored as 0.0f / 1.0f
/// Strings, arrays, and nested objects are silently skipped with a warning,
/// matching the "unknown keys are ignored" policy used elsewhere in the
/// loader. Order of iteration follows JSON object insertion order (nlohmann
/// preserves it for ordered_json; for default `json` the order is
/// insertion order as well in practice).
void ParseCvarsSection(const nlohmann::json& root, ProjectConfig& cfg) {
    auto it = root.find("cvars");
    if (it == root.end() || !it->is_object()) {
        return;
    }
    for (auto entryIt = it->begin(); entryIt != it->end(); ++entryIt) {
        const std::string& name = entryIt.key();
        const auto& value = entryIt.value();

        if (value.is_number()) {
            cfg.cvarOverrides.emplace_back(name, value.get<float>());
        } else if (value.is_boolean()) {
            cfg.cvarOverrides.emplace_back(name, value.get<bool>() ? 1.0f : 0.0f);
        } else {
            UZ_LOG(Config, Warning,
                   "LoadProject: cvar '{}' has unsupported type — must be "
                   "number or boolean (skipped)", name);
        }
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// LoadProject
// ─────────────────────────────────────────────────────────────────────────────

std::optional<ProjectConfig> LoadProject(const std::string& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        UZ_LOG(Config, Error, "LoadProject: cannot open file '{}'", path);
        return std::nullopt;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const nlohmann::json::parse_error& ex) {
        UZ_LOG(Config, Error,
               "LoadProject: JSON parse error in '{}' at byte {}: {}",
               path, ex.byte, ex.what());
        return std::nullopt;
    }

    if (!root.is_object()) {
        UZ_LOG(Config, Error,
               "LoadProject: root element in '{}' is not a JSON object", path);
        return std::nullopt;
    }

    ProjectConfig cfg;

    // ── Top-level scalar fields ───────────────────────────────────────────────
    cfg.name          = JsonString(root, "name",          cfg.name);
    cfg.version       = JsonString(root, "version",       cfg.version);
    // Support both camelCase and snake_case field names for compatibility.
    cfg.engineVersion = JsonString(root, "engineVersion", cfg.engineVersion);
    if (cfg.engineVersion.empty())
        cfg.engineVersion = JsonString(root, "engine_version", cfg.engineVersion);
    cfg.defaultScene  = JsonString(root, "defaultScene",  cfg.defaultScene);
    if (cfg.defaultScene.empty())
        cfg.defaultScene = JsonString(root, "default_scene", cfg.defaultScene);
    cfg.contentRoot   = JsonString(root, "contentRoot",   cfg.contentRoot);
    if (cfg.contentRoot == "content") // only override if still default
        cfg.contentRoot = JsonString(root, "content_root", cfg.contentRoot);

    // ── Nested configuration sections ─────────────────────────────────────────
    ParseWindowSection(root, cfg);
    ParseRenderSection(root, cfg);
    ParseCvarsSection (root, cfg);

    UZ_LOG(Config, Info,
           "LoadProject: loaded '{}' (version='{}', defaultScene='{}', backend='{}', cvarOverrides={})",
           cfg.name.empty() ? path : cfg.name,
           cfg.version,
           cfg.defaultScene,
           cfg.renderBackend,
           cfg.cvarOverrides.size());

    return cfg;
}

} // namespace uz
