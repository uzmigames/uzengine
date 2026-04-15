#include "vfx_effect_type.h"

#include "engine/logging/log.h"
#include "file_system.h"
#include "vfs.h"

#include <json.hpp>

#include <algorithm>
#include <climits>
#include <fstream>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

namespace {

constexpr int32_t kUnsetLodBias = INT32_MIN;

/// Parses an optional platform override block. Missing fields stay unset.
VfxEffectTypePlatformOverride ParsePlatformOverride(const nlohmann::json& obj) {
    VfxEffectTypePlatformOverride ov;
    if (!obj.is_object()) return ov;
    if (obj.contains("max_concurrent_instances") &&
        obj["max_concurrent_instances"].is_number_integer()) {
        ov.maxConcurrentInstances = obj["max_concurrent_instances"].get<int32_t>();
    }
    if (obj.contains("cull_distance_multiplier") &&
        obj["cull_distance_multiplier"].is_number()) {
        ov.cullDistanceMultiplier = obj["cull_distance_multiplier"].get<float>();
    }
    if (obj.contains("lod_bias") && obj["lod_bias"].is_number_integer()) {
        ov.lodBias = obj["lod_bias"].get<int32_t>();
    }
    return ov;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// VfxEffectType::Resolve
// ─────────────────────────────────────────────────────────────────────────────

VfxEffectType::Resolved VfxEffectType::Resolve(const std::string& platform) const {
    Resolved r;
    r.maxConcurrentInstances = maxConcurrentInstances;
    r.cullDistanceMultiplier = cullDistanceMultiplier;
    r.lodBias                = lodBias;

    if (platform.empty()) return r;

    const auto it = platformOverrides.find(platform);
    if (it == platformOverrides.end()) return r;

    const auto& ov = it->second;
    if (ov.maxConcurrentInstances >= 0) {
        r.maxConcurrentInstances = static_cast<uint32_t>(ov.maxConcurrentInstances);
    }
    if (ov.cullDistanceMultiplier >= 0.0f) {
        r.cullDistanceMultiplier = ov.cullDistanceMultiplier;
    }
    if (ov.lodBias != kUnsetLodBias) {
        r.lodBias = ov.lodBias;
    }
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Load / LoadFromString / SaveToFile
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<VfxEffectType> VfxEffectType::LoadFromString(const std::string& jsonStr) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonStr);
    } catch (const nlohmann::json::parse_error& e) {
        UZ_LOG(VFX, Error, "VfxEffectType: JSON parse error: {}", e.what());
        return nullptr;
    }
    if (!root.is_object()) {
        UZ_LOG(VFX, Error, "VfxEffectType: root must be an object");
        return nullptr;
    }
    if (root.value("format", std::string{}) != "uzvfxtype") {
        UZ_LOG(VFX, Error, "VfxEffectType: unexpected or missing 'format'");
        return nullptr;
    }

    auto asset = std::make_unique<VfxEffectType>();
    asset->name                   = root.value("name", std::string{});
    asset->maxConcurrentInstances = root.value("max_concurrent_instances", 0u);
    asset->cullDistanceMultiplier = root.value("cull_distance_multiplier", 1.0f);
    asset->lodBias                = root.value("lod_bias", 0);
    asset->priority               = root.value("priority", 0);

    if (root.contains("platform_overrides") && root["platform_overrides"].is_object()) {
        for (auto it = root["platform_overrides"].begin();
             it != root["platform_overrides"].end(); ++it) {
            if (it.key().empty()) {
                UZ_LOG(VFX, Warning,
                       "VfxEffectType[{}]: empty platform slug in overrides, skipping",
                       asset->name);
                continue;
            }
            asset->platformOverrides[it.key()] = ParsePlatformOverride(it.value());
        }
    }
    return asset;
}

std::unique_ptr<VfxEffectType> VfxEffectType::Load(const std::string& path, IVFS* vfs) {
    std::string content;
    if (vfs) {
        const auto bytes = vfs->Read(path);
        content.assign(bytes.begin(), bytes.end());
    } else {
        content = FileReadAllText(path.c_str());
    }
    if (content.empty()) {
        UZ_LOG(VFX, Error, "VfxEffectType: failed to read '{}'", path);
        return nullptr;
    }
    return LoadFromString(content);
}

bool VfxEffectType::SaveToFile(const std::string& path) const {
    nlohmann::json root;
    root["format"]  = "uzvfxtype";
    root["version"] = 1;
    if (!name.empty()) root["name"] = name;
    root["max_concurrent_instances"] = maxConcurrentInstances;
    root["cull_distance_multiplier"] = cullDistanceMultiplier;
    root["lod_bias"]                 = lodBias;
    root["priority"]                 = priority;

    if (!platformOverrides.empty()) {
        nlohmann::json overrides = nlohmann::json::object();
        for (const auto& [plat, ov] : platformOverrides) {
            nlohmann::json entry;
            if (ov.maxConcurrentInstances >= 0)
                entry["max_concurrent_instances"] = ov.maxConcurrentInstances;
            if (ov.cullDistanceMultiplier >= 0.0f)
                entry["cull_distance_multiplier"] = ov.cullDistanceMultiplier;
            if (ov.lodBias != kUnsetLodBias)
                entry["lod_bias"] = ov.lodBias;
            overrides[plat] = std::move(entry);
        }
        root["platform_overrides"] = std::move(overrides);
    }

    std::ofstream ofs(path);
    if (!ofs) {
        UZ_LOG(VFX, Error, "VfxEffectType: failed to open '{}' for writing", path);
        return false;
    }
    ofs << root.dump(2);
    return static_cast<bool>(ofs);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxEffectTypeInstanceTracker
// ─────────────────────────────────────────────────────────────────────────────

VfxEffectTypeInstanceTracker::AcquireResult
VfxEffectTypeInstanceTracker::TryAcquire(const VfxEffectType& type,
                                          uint64_t userHandle,
                                          int32_t instancePriority,
                                          const std::string& platform) {
    AcquireResult result;

    const VfxEffectType::Resolved resolved = type.Resolve(platform);
    auto& state = m_byType[type.name];

    // Assign ageTick BEFORE any early-out so we can cheaply diagnose races.
    const uint64_t newAge = m_nextAge++;

    // Admit without eviction when under the cap (0 = no cap).
    if (resolved.maxConcurrentInstances == 0u ||
        state.entries.size() < resolved.maxConcurrentInstances) {
        state.entries.push_back({userHandle, instancePriority, newAge});
        result.status = AcquireResult::Status::Admitted;
        return result;
    }

    // Cap hit — find the lowest-priority / oldest existing instance.
    // Eviction policy: evict the entry with the LOWEST priority; tie-break
    // on OLDEST ageTick. If every existing entry has higher priority than
    // the new one, reject.
    auto victim = state.entries.begin();
    for (auto it = state.entries.begin() + 1; it != state.entries.end(); ++it) {
        if (it->priority < victim->priority ||
            (it->priority == victim->priority && it->ageTick < victim->ageTick)) {
            victim = it;
        }
    }

    if (victim->priority > instancePriority) {
        // Every live instance strictly outranks the newcomer.
        result.status = AcquireResult::Status::Rejected;
        // Roll back the age we pre-assigned so counters stay dense.
        --m_nextAge;
        return result;
    }

    result.status  = AcquireResult::Status::AdmittedWithEviction;
    result.evicted = *victim;
    *victim = {userHandle, instancePriority, newAge};
    return result;
}

void VfxEffectTypeInstanceTracker::Release(const std::string& typeName,
                                            uint64_t userHandle) {
    const auto tIt = m_byType.find(typeName);
    if (tIt == m_byType.end()) return;
    auto& entries = tIt->second.entries;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                  [userHandle](const InstanceEntry& e) {
                                      return e.userHandle == userHandle;
                                  }),
                  entries.end());
}

uint32_t VfxEffectTypeInstanceTracker::GetActiveCount(const std::string& typeName) const {
    const auto it = m_byType.find(typeName);
    if (it == m_byType.end()) return 0u;
    return static_cast<uint32_t>(it->second.entries.size());
}

} // namespace uz
