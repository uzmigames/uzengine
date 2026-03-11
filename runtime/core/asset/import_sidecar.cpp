/// UzEngine Import Sidecar — Implementation
///
/// Uses nlohmann/json for serialization. All hash values are stored
/// as 16-char lowercase hex strings for human readability.

#include "import_sidecar.h"
#include "log.h"

#include <json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Hex conversion
// ─────────────────────────────────────────────────────────────────────────────

std::string HashToHex(uint64_t hash) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return ss.str();
}

uint64_t HexToHash(const std::string& hex) {
    if (hex.size() != 16) return 0;
    try {
        return std::stoull(hex, nullptr, 16);
    } catch (...) {
        return 0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON serialization
// ─────────────────────────────────────────────────────────────────────────────

std::string WriteSidecarJson(const ImportSidecar& sidecar) {
    json j;
    j["importer"] = sidecar.importer;
    j["version"] = sidecar.version;
    j["source_hash"] = sidecar.sourceHash;

    j["options"] = json::object();
    for (const auto& [k, v] : sidecar.options) {
        j["options"][k] = v;
    }

    j["outputs"] = json::object();
    for (const auto& [k, v] : sidecar.outputs) {
        j["outputs"][k] = v;
    }

    j["dependencies"] = sidecar.dependencies;

    return j.dump(4);
}

SidecarParseResult ReadSidecarJson(const std::string& jsonStr) {
    SidecarParseResult result;

    try {
        json j = json::parse(jsonStr);

        result.sidecar.importer = j.value("importer", "");
        result.sidecar.version = j.value("version", 0u);
        result.sidecar.sourceHash = j.value("source_hash", "");

        if (j.contains("options") && j["options"].is_object()) {
            for (auto& [k, v] : j["options"].items()) {
                result.sidecar.options[k] = v.get<std::string>();
            }
        }

        if (j.contains("outputs") && j["outputs"].is_object()) {
            for (auto& [k, v] : j["outputs"].items()) {
                result.sidecar.outputs[k] = v.get<std::string>();
            }
        }

        if (j.contains("dependencies") && j["dependencies"].is_array()) {
            for (const auto& dep : j["dependencies"]) {
                result.sidecar.dependencies.push_back(dep.get<std::string>());
            }
        }

        result.success = true;
    } catch (const json::exception& e) {
        result.error = e.what();
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// File I/O
// ─────────────────────────────────────────────────────────────────────────────

bool WriteSidecarFile(const std::string& path, const ImportSidecar& sidecar) {
    std::ofstream file(path);
    if (!file.is_open()) {
        UZ_LOG(Asset, Error, "Cannot write sidecar: path='{}'", path);
        return false;
    }

    file << WriteSidecarJson(sidecar);
    file.close();

    UZ_LOG(Asset, Debug, "Wrote sidecar: path='{}', importer='{}', hash='{}'",
           path, sidecar.importer, sidecar.sourceHash);
    return true;
}

SidecarParseResult ReadSidecarFile(const std::string& path) {
    SidecarParseResult result;

    std::ifstream file(path);
    if (!file.is_open()) {
        result.error = "Cannot open sidecar file: '" + path + "'";
        return result;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return ReadSidecarJson(content);
}

// ─────────────────────────────────────────────────────────────────────────────
// Reimport detection
// ─────────────────────────────────────────────────────────────────────────────

bool NeedsReimport(
    const ImportSidecar& sidecar,
    const std::string& currentSourceHash,
    uint32_t currentVersion) {

    if (sidecar.sourceHash != currentSourceHash) {
        UZ_LOG(Asset, Debug, "Reimport needed: source hash changed ('{}' → '{}')",
               sidecar.sourceHash, currentSourceHash);
        return true;
    }

    if (sidecar.version != currentVersion) {
        UZ_LOG(Asset, Debug, "Reimport needed: importer version changed ({} → {})",
               sidecar.version, currentVersion);
        return true;
    }

    for (const auto& [variant, outputPath] : sidecar.outputs) {
        if (!fs::exists(outputPath)) {
            UZ_LOG(Asset, Debug, "Reimport needed: output missing: variant='{}', path='{}'",
                   variant, outputPath);
            return true;
        }
    }

    return false;
}

} // namespace uz
