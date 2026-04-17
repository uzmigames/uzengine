#include "engine/vfx/asset/vfx_emitter_inheritance.h"

#include "engine/logging/log.h"

#include <json.hpp>

#include <algorithm>
#include <cctype>

namespace uz {

namespace {

using json = nlohmann::json;

/// Returns the `"type"` string of a module JSON entry, or empty when absent.
std::string ModuleType(const json& entry) {
    if (entry.is_object() && entry.contains("type") && entry["type"].is_string()) {
        return entry["type"].get<std::string>();
    }
    return {};
}

/// Deep-merges `patch` into `base` — object keys from `patch` win, primitive
/// values replace in full, arrays replace in full (no element-wise merge,
/// matching every Niagara and Unity override model).
void DeepMerge(json& base, const json& patch) {
    if (!patch.is_object()) {
        base = patch;
        return;
    }
    if (!base.is_object()) {
        base = json::object();
    }
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it.value().is_object()) {
            DeepMerge(base[it.key()], it.value());
        } else {
            base[it.key()] = it.value();
        }
    }
}

/// Builds the final modules array from parent + child override entries.
///
/// Strips `action` keys so downstream parsers see a plain module list.
json ApplyModuleOverrides(const json& parentModules, const json& childModules) {
    json result = json::array();
    if (parentModules.is_array()) {
        for (const auto& m : parentModules) {
            // Drop any leftover action field from a prior merge — keeps the
            // serialiser idempotent across chained resolutions.
            json clean = m;
            if (clean.is_object() && clean.contains("action")) {
                clean.erase("action");
            }
            result.push_back(std::move(clean));
        }
    }

    if (!childModules.is_array()) {
        return result;
    }

    for (const auto& override : childModules) {
        if (!override.is_object()) {
            UZ_LOG(VFX, Warning, "VfxInheritance: module override entry is not an object, skipping");
            continue;
        }
        const std::string type = ModuleType(override);
        const std::string action = override.value("action", std::string("add"));

        if (action == "remove") {
            if (type.empty()) {
                UZ_LOG(VFX, Warning, "VfxInheritance: 'remove' override missing 'type', ignored");
                continue;
            }
            auto it = std::find_if(result.begin(), result.end(), [&](const json& m) {
                return ModuleType(m) == type;
            });
            if (it == result.end()) {
                UZ_LOG(VFX, Warning, "VfxInheritance: 'remove' target '{}' not found in parent", type);
                continue;
            }
            result.erase(it);
            continue;
        }

        json entry = override;
        entry.erase("action");

        if (action == "replace") {
            if (type.empty()) {
                UZ_LOG(VFX, Warning, "VfxInheritance: 'replace' override missing 'type', appending");
                result.push_back(std::move(entry));
                continue;
            }
            auto it = std::find_if(result.begin(), result.end(), [&](const json& m) {
                return ModuleType(m) == type;
            });
            if (it == result.end()) {
                UZ_LOG(VFX, Warning, "VfxInheritance: 'replace' target '{}' not found in parent, appending", type);
                result.push_back(std::move(entry));
                continue;
            }
            // Merge parent config with child override (child keys win).
            if (entry.contains("config") && it->contains("config")) {
                json merged = (*it)["config"];
                DeepMerge(merged, entry["config"]);
                entry["config"] = std::move(merged);
            }
            *it = std::move(entry);
            continue;
        }

        if (action != "add") {
            UZ_LOG(VFX, Warning,
                   "VfxInheritance: unknown action '{}', treating as 'add'", action);
        }
        result.push_back(std::move(entry));
    }

    return result;
}

/// Appends `extra` onto `base` array, preferring child entries on duplicate
/// `name` keys (logs a warning). Used for `parameters` and `events`.
json AppendByName(const json& parentArr, const json& childArr) {
    json result = json::array();
    if (parentArr.is_array()) {
        for (const auto& p : parentArr) {
            result.push_back(p);
        }
    }
    if (!childArr.is_array()) return result;

    for (const auto& c : childArr) {
        if (!c.is_object() || !c.contains("name") || !c["name"].is_string()) {
            result.push_back(c);
            continue;
        }
        const std::string name = c["name"].get<std::string>();
        auto it = std::find_if(result.begin(), result.end(), [&](const json& p) {
            return p.is_object() && p.contains("name") && p["name"].is_string()
                   && p["name"].get<std::string>() == name;
        });
        if (it == result.end()) {
            result.push_back(c);
        } else {
            UZ_LOG(VFX, Warning,
                   "VfxInheritance: duplicate parameter/event '{}' — child overrides parent", name);
            *it = c;
        }
    }
    return result;
}

} // namespace

std::string VfxNormalizeInheritancePath(const std::string& path) {
    std::string out = path;
    std::replace(out.begin(), out.end(), '\\', '/');
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::optional<nlohmann::json>
VfxResolveInheritanceImpl(const nlohmann::json& childJson,
                          const VfxInheritanceLoader& loader,
                          std::unordered_set<std::string>& visited,
                          uint32_t depth,
                          uint32_t maxDepth) {
    if (depth > maxDepth) {
        UZ_LOG(VFX, Error,
               "VfxInheritance: chain depth {} exceeds max {}", depth, maxDepth);
        return std::nullopt;
    }

    // When the current node has no parent, strip action metadata and return.
    if (!childJson.contains("parent") || !childJson["parent"].is_string()) {
        json self = childJson;
        if (self.contains("modules") && self["modules"].is_array()) {
            for (auto& m : self["modules"]) {
                if (m.is_object() && m.contains("action")) m.erase("action");
            }
        }
        return self;
    }

    const std::string parentPath = childJson["parent"].get<std::string>();
    const std::string normalised = VfxNormalizeInheritancePath(parentPath);

    if (visited.count(normalised) > 0) {
        UZ_LOG(VFX, Error,
               "VfxInheritance: cycle detected on '{}'", parentPath);
        return std::nullopt;
    }
    if (!loader) {
        UZ_LOG(VFX, Error, "VfxInheritance: null loader, cannot resolve '{}'", parentPath);
        return std::nullopt;
    }

    const std::string parentText = loader(parentPath);
    if (parentText.empty()) {
        UZ_LOG(VFX, Error, "VfxInheritance: cannot load parent '{}'", parentPath);
        return std::nullopt;
    }

    json parentJson;
    try {
        parentJson = json::parse(parentText);
    } catch (const json::parse_error& e) {
        UZ_LOG(VFX, Error,
               "VfxInheritance: parent '{}' parse error: {}", parentPath, e.what());
        return std::nullopt;
    }

    visited.insert(normalised);
    auto resolvedParent =
        VfxResolveInheritanceImpl(parentJson, loader, visited, depth + 1, maxDepth);
    visited.erase(normalised);

    if (!resolvedParent) {
        return std::nullopt;
    }

    // Merge child onto the resolved parent.
    json merged = *resolvedParent;
    merged.erase("parent"); // resolved chain no longer has a dangling parent ref

    // Propagate format / version from child (child is the authoritative author).
    if (childJson.contains("format"))  merged["format"]  = childJson["format"];
    if (childJson.contains("version")) merged["version"] = childJson["version"];
    if (childJson.contains("name") && childJson["name"].is_string() &&
        !childJson["name"].get<std::string>().empty()) {
        merged["name"] = childJson["name"];
    }

    // Emitter block: key-level deep merge (child keys win).
    if (childJson.contains("emitter") && childJson["emitter"].is_object()) {
        if (!merged.contains("emitter")) merged["emitter"] = json::object();
        DeepMerge(merged["emitter"], childJson["emitter"]);
    }

    // Modules: apply action-based overrides.
    merged["modules"] = ApplyModuleOverrides(
        merged.contains("modules") ? merged["modules"] : json::array(),
        childJson.contains("modules") ? childJson["modules"] : json::array());

    // Parameters and events: union by name, child wins duplicates.
    merged["parameters"] = AppendByName(
        merged.contains("parameters") ? merged["parameters"] : json::array(),
        childJson.contains("parameters") ? childJson["parameters"] : json::array());
    merged["events"] = AppendByName(
        merged.contains("events") ? merged["events"] : json::array(),
        childJson.contains("events") ? childJson["events"] : json::array());

    // Tags (added in 8.2.0): union of parent + child, de-dup by equality.
    if (childJson.contains("tags") || merged.contains("tags")) {
        json tagsOut = json::array();
        auto push = [&](const json& src) {
            if (!src.is_array()) return;
            for (const auto& t : src) {
                if (!t.is_string()) continue;
                const std::string s = t.get<std::string>();
                bool dup = false;
                for (const auto& existing : tagsOut) {
                    if (existing.is_string() &&
                        existing.get<std::string>() == s) { dup = true; break; }
                }
                if (!dup) tagsOut.push_back(s);
            }
        };
        push(merged.contains("tags") ? merged["tags"] : json::array());
        push(childJson.contains("tags") ? childJson["tags"] : json::array());
        merged["tags"] = std::move(tagsOut);
    }

    return merged;
}

std::optional<nlohmann::json>
VfxResolveInheritance(const nlohmann::json& childJson,
                      const VfxInheritanceLoader& loader,
                      uint32_t maxDepth) {
    std::unordered_set<std::string> visited;
    return VfxResolveInheritanceImpl(childJson, loader, visited, 0u, maxDepth);
}

} // namespace uz
