/// VfxSystem -- JSON deserialization and validation for `.uzvfxsys` files.

#include "vfx_system.h"

#include "engine/logging/log.h"
#include "file_system.h"
#include "vfs.h"

#include <json.hpp>

#include <fstream>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Core Parse + Validate
// ─────────────────────────────────────────────────────────────────────────────

/// Parses and validates a `.uzvfxsys` JSON root object into a `VfxSystem`.
///
/// # Returns
/// Populated unique_ptr on success, nullptr on validation failure.
/// Logs all validation errors with VFX category.
[[nodiscard]] std::unique_ptr<VfxSystem> ParseAndValidate(const nlohmann::json& root) {
    // -- format field --
    if (!root.contains("format") || !root["format"].is_string()) {
        UZ_LOG(VFX, Error, "VfxSystem: missing or invalid 'format' field");
        return nullptr;
    }
    if (root["format"].get<std::string>() != "uzvfxsys") {
        UZ_LOG(VFX, Error, "VfxSystem: expected format 'uzvfxsys', got '{}'",
               root["format"].get<std::string>());
        return nullptr;
    }

    // -- version field (informational, must be present) --
    if (!root.contains("version") || !root["version"].is_number_integer()) {
        UZ_LOG(VFX, Error, "VfxSystem: missing or invalid 'version' field");
        return nullptr;
    }

    // -- name (optional, default empty) --
    std::string name;
    if (root.contains("name")) {
        if (!root["name"].is_string()) {
            UZ_LOG(VFX, Error, "VfxSystem: 'name' must be a string");
            return nullptr;
        }
        name = root["name"].get<std::string>();
    }

    // -- emitters array (required, non-empty) --
    if (!root.contains("emitters") || !root["emitters"].is_array()) {
        UZ_LOG(VFX, Error, "VfxSystem: missing or invalid 'emitters' array");
        return nullptr;
    }
    const auto& emittersJson = root["emitters"];
    if (emittersJson.empty()) {
        UZ_LOG(VFX, Error, "VfxSystem: 'emitters' array must not be empty");
        return nullptr;
    }

    std::vector<VfxSystemEmitterRef> refs;
    refs.reserve(emittersJson.size());

    for (size_t i = 0; i < emittersJson.size(); ++i) {
        const auto& e = emittersJson[i];
        if (!e.is_object()) {
            UZ_LOG(VFX, Error,
                   "VfxSystem: emitter ref at index {} must be an object", i);
            return nullptr;
        }

        VfxSystemEmitterRef ref;

        // path (required, non-empty)
        if (!e.contains("path") || !e["path"].is_string()) {
            UZ_LOG(VFX, Error,
                   "VfxSystem: emitter ref [{}] missing or invalid 'path' field", i);
            return nullptr;
        }
        ref.emitterPath = e["path"].get<std::string>();
        if (ref.emitterPath.empty()) {
            UZ_LOG(VFX, Error,
                   "VfxSystem: emitter ref [{}] 'path' must not be empty", i);
            return nullptr;
        }

        // name (optional)
        if (e.contains("name")) {
            if (!e["name"].is_string()) {
                UZ_LOG(VFX, Error,
                       "VfxSystem: emitter ref [{}] 'name' must be a string", i);
                return nullptr;
            }
            ref.name = e["name"].get<std::string>();
        }

        // id (optional — auto-generate if missing)
        if (e.contains("id")) {
            if (!e["id"].is_string()) {
                UZ_LOG(VFX, Error,
                       "VfxSystem: emitter ref [{}] 'id' must be a string", i);
                return nullptr;
            }
            ref.id = e["id"].get<std::string>();
        }
        if (ref.id.empty()) {
            ref.id = "e" + std::to_string(i);
        }

        // enabled (optional, default true)
        if (e.contains("enabled")) {
            if (!e["enabled"].is_boolean()) {
                UZ_LOG(VFX, Error,
                       "VfxSystem: emitter ref [{}] 'enabled' must be a boolean", i);
                return nullptr;
            }
            ref.enabled = e["enabled"].get<bool>();
        }

        // localSpace (optional, default true)
        if (e.contains("localSpace")) {
            if (!e["localSpace"].is_boolean()) {
                UZ_LOG(VFX, Error,
                       "VfxSystem: emitter ref [{}] 'localSpace' must be a boolean", i);
                return nullptr;
            }
            ref.localSpace = e["localSpace"].get<bool>();
        }

        refs.push_back(std::move(ref));
    }

    auto asset = std::make_unique<VfxSystem>();
    asset->name = std::move(name);
    asset->emitters = std::move(refs);
    return asset;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// VfxSystem::LoadFromString
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<VfxSystem> VfxSystem::LoadFromString(const std::string& jsonStr) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonStr);
    } catch (const nlohmann::json::parse_error& e) {
        UZ_LOG(VFX, Error, "VfxSystem: JSON parse error: {}", e.what());
        return nullptr;
    }
    return ParseAndValidate(root);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxSystem::Load
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<VfxSystem> VfxSystem::Load(const std::string& path, IVFS* vfs) {
    // Read via VFS when available (supports .uzpak), fallback to disk.
    std::string content;
    if (vfs) {
        auto data = vfs->Read(path);
        content.assign(data.begin(), data.end());
    } else {
        content = FileReadAllText(path.c_str());
    }
    if (content.empty()) {
        UZ_LOG(VFX, Error, "VfxSystem: failed to read file '{}'", path);
        return nullptr;
    }

    auto asset = LoadFromString(content);
    if (asset) {
        asset->sourcePath = path;
    }
    return asset;
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxSystem::SaveToFile
// ─────────────────────────────────────────────────────────────────────────────

bool VfxSystem::SaveToFile(const std::string& path) const {
    nlohmann::json root;
    root["format"] = "uzvfxsys";
    root["version"] = 1;
    if (!name.empty()) {
        root["name"] = name;
    }

    nlohmann::json emittersJson = nlohmann::json::array();
    for (const auto& ref : emitters) {
        nlohmann::json e;
        if (!ref.name.empty()) e["name"] = ref.name;
        e["path"] = ref.emitterPath;
        if (!ref.id.empty()) e["id"] = ref.id;
        e["enabled"]    = ref.enabled;
        e["localSpace"] = ref.localSpace;
        emittersJson.push_back(std::move(e));
    }
    root["emitters"] = std::move(emittersJson);

    std::ofstream ofs(path);
    if (!ofs) {
        UZ_LOG(VFX, Error, "VfxSystem: failed to open '{}' for writing", path);
        return false;
    }
    ofs << root.dump(2);
    return static_cast<bool>(ofs);
}

} // namespace uz
