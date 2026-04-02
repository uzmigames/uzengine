/// VFX Composite implementation -- JSON parsing for .uzvfx_composite files.

#include "vfx_composite.h"

#include "engine/logging/log.h"
#include "file_system.h"
#include "vfs.h"

#include <json.hpp>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

using json = nlohmann::json;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// JSON Parsing Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Parses a 3-element JSON array into a Vec3.
///
/// # Returns
/// true on success, false if the array is not exactly 3 numeric elements.
[[nodiscard]] bool ParseVec3(const json& arr, Vec3& out) {
    if (!arr.is_array() || arr.size() != 3) return false;
    if (!arr[0].is_number() || !arr[1].is_number() || !arr[2].is_number()) return false;

    out.x = arr[0].get<float>();
    out.y = arr[1].get<float>();
    out.z = arr[2].get<float>();
    return true;
}

/// Parses a single emitter entry from the "emitters" array.
///
/// # Returns
/// true on success with `out` populated, false on validation failure.
[[nodiscard]] bool ParseEntry(const json& obj, VFXCompositeEntry& out) {
    if (!obj.is_object()) return false;

    if (!obj.contains("name") || !obj["name"].is_string()) return false;
    if (!obj.contains("graph") || !obj["graph"].is_string()) return false;

    out.name = obj["name"].get<std::string>();
    out.graphPath = obj["graph"].get<std::string>();

    // Offset defaults to zero if absent.
    if (obj.contains("offset")) {
        if (!ParseVec3(obj["offset"], out.offset)) return false;
    }

    // Delay defaults to 0 if absent; must be non-negative.
    if (obj.contains("delay")) {
        if (!obj["delay"].is_number()) return false;
        out.delay = obj["delay"].get<float>();
        if (out.delay < 0.0f) return false;
    }

    return true;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// VFXComposite::LoadFromString
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<VFXComposite> VFXComposite::LoadFromString(const std::string& jsonStr) {
    json doc;
    try {
        doc = json::parse(jsonStr);
    } catch (const json::parse_error& e) {
        UZ_LOG(VFX, Error, "VFXComposite: JSON parse error: {}", e.what());
        return nullptr;
    }

    // Validate format field.
    if (!doc.contains("format") || doc["format"] != "uzvfx_composite") {
        UZ_LOG(VFX, Error, "VFXComposite: missing or incorrect 'format' field");
        return nullptr;
    }

    auto composite = std::make_unique<VFXComposite>();

    // Parse version (required).
    if (doc.contains("version") && doc["version"].is_number_integer()) {
        composite->version_ = doc["version"].get<uint32_t>();
    }

    // Parse name (required).
    if (doc.contains("name") && doc["name"].is_string()) {
        composite->name_ = doc["name"].get<std::string>();
    }

    // Parse emitters array (required, at least 1 entry).
    if (!doc.contains("emitters") || !doc["emitters"].is_array()) {
        UZ_LOG(VFX, Error, "VFXComposite: missing or invalid 'emitters' array");
        return nullptr;
    }

    const auto& emittersArr = doc["emitters"];
    if (emittersArr.empty()) {
        UZ_LOG(VFX, Error, "VFXComposite: 'emitters' array must have at least 1 entry");
        return nullptr;
    }

    composite->entries_.reserve(emittersArr.size());

    for (size_t i = 0; i < emittersArr.size(); ++i) {
        VFXCompositeEntry entry;
        if (!ParseEntry(emittersArr[i], entry)) {
            UZ_LOG(VFX, Error, "VFXComposite: invalid emitter entry at index {}", i);
            return nullptr;
        }
        composite->entries_.push_back(std::move(entry));
    }

    return composite;
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXComposite::Load
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<VFXComposite> VFXComposite::Load(const std::string& path, IVFS* vfs) {
    // Read via VFS when available (supports .uzpak), fallback to disk.
    std::string content;
    if (vfs) {
        auto data = vfs->Read(path);
        content.assign(data.begin(), data.end());
    } else {
        content = FileReadAllText(path.c_str());
    }
    if (content.empty()) {
        UZ_LOG(VFX, Error, "VFXComposite: failed to read file '{}'", path);
        return nullptr;
    }

    auto composite = LoadFromString(content);
    if (!composite) {
        UZ_LOG(VFX, Error, "VFXComposite: failed to parse file '{}'", path);
    }
    return composite;
}

} // namespace uz
