#include "vfx_simulation_stage.h"

#include "engine/logging/log.h"

#include <json.hpp>

#include <algorithm>
#include <unordered_set>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VfxSimulationStageList::FindReadersOf
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::string> VfxSimulationStageList::FindReadersOf(size_t producerIndex) const {
    std::vector<std::string> readers;
    if (producerIndex >= stages.size()) return readers;

    const VfxSimulationStage& producer = stages[producerIndex];
    if (producer.writeSet.empty()) return readers;

    const std::unordered_set<std::string> writeSet(producer.writeSet.begin(),
                                                     producer.writeSet.end());

    for (size_t i = producerIndex + 1u; i < stages.size(); ++i) {
        const VfxSimulationStage& consumer = stages[i];
        for (const auto& read : consumer.readSet) {
            if (writeSet.count(read) > 0u) {
                readers.push_back(consumer.name);
                break;
            }
        }
    }
    return readers;
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxSimulationStageAsset — JSON round-trip
// ─────────────────────────────────────────────────────────────────────────────

namespace {

std::vector<std::string> ReadStringArray(const nlohmann::json& j, const char* key) {
    std::vector<std::string> out;
    if (!j.contains(key) || !j[key].is_array()) return out;
    for (const auto& v : j[key]) {
        if (v.is_string()) {
            out.push_back(v.get<std::string>());
        } else {
            UZ_LOG(VFX, Warning,
                   "VfxSimulationStage[{}]: non-string entry skipped", key);
        }
    }
    return out;
}

} // namespace

bool VfxSimulationStageAsset::LoadFromJson(const std::string& jsonStr,
                                             VfxSimulationStageList& outList) {
    outList.stages.clear();

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonStr);
    } catch (const nlohmann::json::parse_error& e) {
        UZ_LOG(VFX, Error, "VfxSimulationStageAsset: JSON parse error: {}", e.what());
        return false;
    }

    // Accept either a bare array or an object with a "stages" array.
    const nlohmann::json* stagesArr = nullptr;
    if (root.is_array()) {
        stagesArr = &root;
    } else if (root.is_object() && root.contains("stages") &&
                root["stages"].is_array()) {
        stagesArr = &root["stages"];
    } else {
        UZ_LOG(VFX, Error, "VfxSimulationStageAsset: root must be an array or "
                            "an object with a 'stages' array");
        return false;
    }

    outList.stages.reserve(stagesArr->size());
    for (const auto& s : *stagesArr) {
        if (!s.is_object()) {
            UZ_LOG(VFX, Warning,
                   "VfxSimulationStage: non-object entry skipped");
            continue;
        }
        VfxSimulationStage stage;
        stage.name   = s.value("name",   std::string{});
        stage.csPath = s.value("cs",     std::string{});
        stage.iterationCount = s.value("iterations", 1u);
        if (stage.iterationCount == 0u) {
            UZ_LOG(VFX, Warning,
                   "VfxSimulationStage[{}]: iterations clamped from 0 to 1",
                   stage.name);
            stage.iterationCount = 1u;
        }
        stage.readSet  = ReadStringArray(s, "reads");
        stage.writeSet = ReadStringArray(s, "writes");

        if (stage.name.empty()) {
            UZ_LOG(VFX, Warning,
                   "VfxSimulationStage: entry with empty name skipped");
            continue;
        }
        if (stage.csPath.empty()) {
            UZ_LOG(VFX, Warning,
                   "VfxSimulationStage[{}]: empty 'cs' path skipped",
                   stage.name);
            continue;
        }
        outList.stages.push_back(std::move(stage));
    }
    return true;
}

std::string VfxSimulationStageAsset::SaveToJson(const VfxSimulationStageList& list) {
    nlohmann::json root = nlohmann::json::array();
    for (const auto& s : list.stages) {
        nlohmann::json entry;
        entry["name"] = s.name;
        entry["cs"]   = s.csPath;
        entry["iterations"] = s.iterationCount;
        entry["reads"]  = s.readSet;
        entry["writes"] = s.writeSet;
        root.push_back(std::move(entry));
    }
    return root.dump(2);
}

} // namespace uz
