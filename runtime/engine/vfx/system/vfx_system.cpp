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

/// `.uzvfxsys` documents are handled as insertion-ordered JSON so the
/// `parameters` object keeps its authoring order on load and on save
/// (`VfxSystem::parameters` is documented as "in authoring order"). The
/// default `nlohmann::json` stores objects in a std::map, which would
/// silently re-sort parameters alphabetically.
using VfxSystemJson = nlohmann::ordered_json;

// ─────────────────────────────────────────────────────────────────────────────
// Core Parse + Validate
// ─────────────────────────────────────────────────────────────────────────────

/// Parses and validates a `.uzvfxsys` JSON root object into a `VfxSystem`.
///
/// # Returns
/// Populated unique_ptr on success, nullptr on validation failure.
/// Logs all validation errors with VFX category.
[[nodiscard]] std::unique_ptr<VfxSystem> ParseAndValidate(const VfxSystemJson& root) {
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

    // -- parameters block (optional, added in phase 7.2.0) --
    // Parses: "parameters": { "<name>": {"type": "float|vec3|vec4", "value": ...} }
    // Unknown types are rejected with a Warning (the rest of the asset loads).
    if (root.contains("parameters") && root["parameters"].is_object()) {
        const auto& paramsJson = root["parameters"];
        asset->parameters.reserve(paramsJson.size());
        for (auto it = paramsJson.begin(); it != paramsJson.end(); ++it) {
            const std::string paramName = it.key();
            if (paramName.empty()) {
                UZ_LOG(VFX, Warning,
                       "VfxSystem[parameters]: empty parameter name, skipping");
                continue;
            }
            if (!it.value().is_object()) {
                UZ_LOG(VFX, Warning,
                       "VfxSystem[parameters][{}]: entry must be an object, skipping",
                       paramName);
                continue;
            }
            const auto& entry = it.value();
            if (!entry.contains("type") || !entry["type"].is_string()) {
                UZ_LOG(VFX, Warning,
                       "VfxSystem[parameters][{}]: missing or invalid 'type', skipping",
                       paramName);
                continue;
            }
            const std::string typeStr = entry["type"].get<std::string>();

            VfxSystem::ParameterDefault def;
            def.name = paramName;

            if (typeStr == "float") {
                if (!entry.contains("value") || !entry["value"].is_number()) {
                    UZ_LOG(VFX, Warning,
                           "VfxSystem[parameters][{}]: float 'value' must be a number, skipping",
                           paramName);
                    continue;
                }
                def.value = Vec4{entry["value"].get<float>(), 0.0f, 0.0f, 0.0f};
                def.componentCount = 1;
            } else if (typeStr == "vec3" || typeStr == "vec4") {
                if (!entry.contains("value") || !entry["value"].is_array()) {
                    UZ_LOG(VFX, Warning,
                           "VfxSystem[parameters][{}]: {} 'value' must be an array, skipping",
                           paramName, typeStr);
                    continue;
                }
                const auto& arr = entry["value"];
                const size_t required = (typeStr == "vec3") ? 3u : 4u;
                if (arr.size() != required) {
                    UZ_LOG(VFX, Warning,
                           "VfxSystem[parameters][{}]: {} 'value' must have {} elements (got {}), skipping",
                           paramName, typeStr, required, arr.size());
                    continue;
                }
                def.value = Vec4{
                    arr[0].get<float>(),
                    arr[1].get<float>(),
                    arr[2].get<float>(),
                    (required == 4u) ? arr[3].get<float>() : 0.0f
                };
                def.componentCount = static_cast<uint8_t>(required);
            } else {
                UZ_LOG(VFX, Warning,
                       "VfxSystem[parameters][{}]: unknown type '{}' (expected float/vec3/vec4), skipping",
                       paramName, typeStr);
                continue;
            }

            asset->parameters.push_back(std::move(def));
        }
    }

    // -- tags array (optional, added in phase 8.2.0) --
    // JSON: "tags": ["fire", "ambient", ...]. Non-string entries are skipped
    // with a warning so a malformed element does not nullify the whole load.
    if (root.contains("tags")) {
        if (!root["tags"].is_array()) {
            UZ_LOG(VFX, Warning, "VfxSystem[tags]: 'tags' must be an array, skipping");
        } else {
            for (const auto& entry : root["tags"]) {
                if (entry.is_string()) {
                    asset->tags.push_back(entry.get<std::string>());
                } else {
                    UZ_LOG(VFX, Warning,
                           "VfxSystem[tags]: non-string tag skipped");
                }
            }
        }
    }

    return asset;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// VfxSystem::LoadFromString
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<VfxSystem> VfxSystem::LoadFromString(const std::string& jsonStr) {
    VfxSystemJson root;
    try {
        root = VfxSystemJson::parse(jsonStr);
    } catch (const VfxSystemJson::parse_error& e) {
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
    VfxSystemJson root;
    root["format"] = "uzvfxsys";
    root["version"] = 1;
    if (!name.empty()) {
        root["name"] = name;
    }

    VfxSystemJson emittersJson = VfxSystemJson::array();
    for (const auto& ref : emitters) {
        VfxSystemJson e;
        if (!ref.name.empty()) e["name"] = ref.name;
        e["path"] = ref.emitterPath;
        if (!ref.id.empty()) e["id"] = ref.id;
        e["enabled"]    = ref.enabled;
        e["localSpace"] = ref.localSpace;
        emittersJson.push_back(std::move(e));
    }
    root["emitters"] = std::move(emittersJson);

    // Parameters block (7.2.0): only written when non-empty to keep legacy
    // fireball.uzvfxsys round-trip stable.
    if (!parameters.empty()) {
        VfxSystemJson paramsJson = VfxSystemJson::object();
        for (const auto& p : parameters) {
            VfxSystemJson entry;
            switch (p.componentCount) {
                case 1:
                    entry["type"] = "float";
                    entry["value"] = p.value.x;
                    break;
                case 3:
                    entry["type"] = "vec3";
                    entry["value"] = {p.value.x, p.value.y, p.value.z};
                    break;
                case 4:
                    entry["type"] = "vec4";
                    entry["value"] = {p.value.x, p.value.y, p.value.z, p.value.w};
                    break;
                default:
                    // Should never happen; loader validates componentCount.
                    entry["type"] = "vec4";
                    entry["value"] = {p.value.x, p.value.y, p.value.z, p.value.w};
                    break;
            }
            paramsJson[p.name] = std::move(entry);
        }
        root["parameters"] = std::move(paramsJson);
    }

    // Tag metadata block (8.2.0): only emitted when non-empty to keep diffs
    // minimal for legacy assets. @ref docs/engine/25-vfx/19-asset-tagging.md
    if (!tags.empty()) {
        root["tags"] = tags;
    }

    std::ofstream ofs(path);
    if (!ofs) {
        UZ_LOG(VFX, Error, "VfxSystem: failed to open '{}' for writing", path);
        return false;
    }
    ofs << root.dump(2);
    return static_cast<bool>(ofs);
}

} // namespace uz
