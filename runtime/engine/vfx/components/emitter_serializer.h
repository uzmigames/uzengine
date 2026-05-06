/// VFX Emitter Serialization — JSON save/load for ParticleEmitter config.
///
/// Serializes the emitter's configurable fields (not runtime state) to
/// a JSON string. Compatible with VFS for .uzfx asset loading.
///
/// Usage:
/// ```cpp
/// std::string json = SerializeEmitter(emitter);
/// DeserializeEmitter(json, emitter);
/// ```

#pragma once

#include "particle_emitter.h"
#include <json.hpp>
#include <algorithm>
#include <string>
#include <vector>

namespace uz {

/// Serializes a ParticleEmitter's config fields to a JSON string.
inline std::string SerializeEmitter(const ParticleEmitter& e) {
    nlohmann::json j;
    j["maxParticles"]     = e.maxParticles;
    j["emissionRate"]     = e.emissionRate;
    j["lifetime"]         = e.lifetime;
    j["lifetimeVariation"] = e.lifetimeVariation;
    j["spawnVelocity"]    = {e.spawnVelocity.x, e.spawnVelocity.y, e.spawnVelocity.z};
    j["velocityVariation"] = e.velocityVariation;
    j["spreadAngle"]      = e.spreadAngle;
    j["gravity"]          = {e.gravity.x, e.gravity.y, e.gravity.z};
    j["damping"]          = e.damping;
    j["spawnRadius"]      = e.spawnRadius;
    j["initialSize"]      = e.initialSize;
    j["initialColor"]     = {e.initialColor.x, e.initialColor.y, e.initialColor.z, e.initialColor.w};
    j["spawnShape"]       = e.spawnShape;
    j["emissiveIntensity"] = e.emissiveIntensity;
    j["blendMode"]        = e.blendMode;
    j["softFadeDistance"]  = e.softParticleFadeRange;
    j["facingMode"]       = e.facingMode;
    j["stretchFactor"]    = e.stretchFactor;
    j["curlStrength"]     = e.curlStrength;
    j["curlFrequency"]    = e.curlFrequency;
    j["curlSpeed"]        = e.curlSpeed;
    j["groundCollision"]  = e.groundCollision;
    j["restitution"]      = e.restitution;
    j["dragCoefficient"]  = e.dragCoefficient;
    j["orbitEnabled"]     = e.orbitEnabled;
    j["orbitRadius"]      = e.orbitRadius;
    j["orbitSpeed"]       = e.orbitSpeed;
    j["windStrength"]     = e.windStrength;
    j["sizeOverLifeStart"]= e.sizeOverLifeStart;
    j["sizeOverLifeEnd"]  = e.sizeOverLifeEnd;
    j["colorLifeStartR"]  = e.colorLifeStartR;
    j["colorLifeStartG"]  = e.colorLifeStartG;
    j["colorLifeStartB"]  = e.colorLifeStartB;
    j["colorLifeStartA"]  = e.colorLifeStartA;
    j["colorLifeEndR"]    = e.colorLifeEndR;
    j["colorLifeEndG"]    = e.colorLifeEndG;
    j["colorLifeEndB"]    = e.colorLifeEndB;
    j["colorLifeEndA"]    = e.colorLifeEndA;
    // Asset tag metadata (see docs/engine/25-vfx/19-asset-tagging.md). Only
    // emit when present so legacy .uzfx files stay diff-minimal.
    if (!e.tags.empty()) {
        j["tags"] = e.tags;
    }
    // Niagara-parity time-stamped spawn bursts (phase 11.8). Only emit
    // when the list is non-empty so legacy assets stay byte-identical.
    if (!e.bursts.empty()) {
        nlohmann::json burstsArr = nlohmann::json::array();
        for (const auto& b : e.bursts) {
            nlohmann::json bj;
            bj["t"] = b.time;
            bj["n"] = b.count;
            burstsArr.push_back(std::move(bj));
        }
        j["bursts"] = std::move(burstsArr);
    }
    // Niagara-parity spawn rate curve (phase 11.7). Only emit when the
    // curve has keys — legacy assets without a curve keep the constant
    // `emissionRate` path.
    if (e.spawnRateCurve.GetKeyCount() > 0) {
        nlohmann::json curveObj;
        nlohmann::json keysArr = nlohmann::json::array();
        for (const auto& k : e.spawnRateCurve.GetFloatKeys()) {
            nlohmann::json kObj;
            kObj["t"] = k.time;
            kObj["v"] = k.value;
            keysArr.push_back(std::move(kObj));
        }
        curveObj["keys"] = std::move(keysArr);
        curveObj["interp"] =
            (e.spawnRateCurve.GetInterpolation() == InterpolationMode::Cubic)
                ? "Cubic"
                : (e.spawnRateCurve.GetInterpolation() == InterpolationMode::Smoothstep)
                    ? "Smoothstep"
                    : "Linear";
        j["spawnRateCurve"] = std::move(curveObj);
    }
    // Niagara-parity 7-stage stack (phase 11.1.0). Only emit when the
    // stack is non-empty — legacy assets stay byte-identical.
    // @ref docs/engine/25-vfx/44-niagara-stack-model.md
    if (!e.stack.IsEmpty()) {
        nlohmann::json stagesArr = nlohmann::json::array();
        for (const auto& entry : e.stack.Entries()) {
            if (entry.modules.empty()) continue;
            nlohmann::json stageObj;
            stageObj["stage"] = VfxStageName(entry.stage);
            nlohmann::json modArr = nlohmann::json::array();
            for (const auto& mod : entry.modules) {
                nlohmann::json mj;
                mj["type"]       = mod.moduleTypeName;
                mj["instanceId"] = mod.instanceId;
                mj["enabled"]    = mod.enabled;
                if (mod.overrideFlag != VfxOverrideFlag::None) {
                    mj["override"] = VfxOverrideFlagName(mod.overrideFlag);
                }
                if (!mod.configJson.empty()) {
                    // configJson is raw JSON text; embed as a parsed
                    // object when valid, otherwise as a string so bad
                    // text doesn't break the round-trip.
                    try {
                        mj["config"] = nlohmann::json::parse(mod.configJson);
                    } catch (...) {
                        mj["config"] = mod.configJson;
                    }
                }
                modArr.push_back(std::move(mj));
            }
            stageObj["modules"] = std::move(modArr);
            stagesArr.push_back(std::move(stageObj));
        }
        j["stages"] = std::move(stagesArr);
    }
    if (!e.attachBoneNameDefault.empty()) {
        j["attachBoneName"] = e.attachBoneNameDefault;
    }
    if (!e.userAttributes.empty()) {
        auto& arr = j["userAttributes"];
        arr = nlohmann::json::array();
        for (const auto& a : e.userAttributes) {
            nlohmann::json entry;
            entry["name"] = a.name;
            entry["type"] = (a.type == UserAttributeType::Vec4) ? "vec4" : "float";
            if (a.type == UserAttributeType::Vec4) {
                entry["default"] = {a.defaultVec4.x, a.defaultVec4.y,
                                     a.defaultVec4.z, a.defaultVec4.w};
            } else {
                entry["default"] = a.defaultFloat;
            }
            arr.push_back(std::move(entry));
        }
    }
    return j.dump(2);
}

/// Deserializes a JSON string into a ParticleEmitter's config fields.
/// Fields not present in the JSON retain their default values.
inline bool DeserializeEmitter(const std::string& jsonStr, ParticleEmitter& e) {
    try {
        auto j = nlohmann::json::parse(jsonStr);
        auto get = [&](const char* key, auto& val) {
            if (j.contains(key)) val = j[key].get<std::remove_reference_t<decltype(val)>>();
        };
        get("attachBoneName", e.attachBoneNameDefault);
        get("maxParticles", e.maxParticles);
        get("emissionRate", e.emissionRate);
        get("lifetime", e.lifetime);
        get("lifetimeVariation", e.lifetimeVariation);
        get("velocityVariation", e.velocityVariation);
        get("spreadAngle", e.spreadAngle);
        get("damping", e.damping);
        get("spawnRadius", e.spawnRadius);
        get("initialSize", e.initialSize);
        get("spawnShape", e.spawnShape);
        get("emissiveIntensity", e.emissiveIntensity);
        get("blendMode", e.blendMode);
        get("softFadeDistance", e.softParticleFadeRange);
        get("facingMode", e.facingMode);
        get("stretchFactor", e.stretchFactor);
        get("curlStrength", e.curlStrength);
        get("curlFrequency", e.curlFrequency);
        get("curlSpeed", e.curlSpeed);
        get("groundCollision", e.groundCollision);
        get("restitution", e.restitution);
        get("dragCoefficient", e.dragCoefficient);
        get("orbitEnabled", e.orbitEnabled);
        get("orbitRadius", e.orbitRadius);
        get("orbitSpeed", e.orbitSpeed);
        get("windStrength", e.windStrength);
        get("sizeOverLifeStart", e.sizeOverLifeStart);
        get("sizeOverLifeEnd", e.sizeOverLifeEnd);
        get("colorLifeStartR", e.colorLifeStartR);
        get("colorLifeStartG", e.colorLifeStartG);
        get("colorLifeStartB", e.colorLifeStartB);
        get("colorLifeStartA", e.colorLifeStartA);
        get("colorLifeEndR", e.colorLifeEndR);
        get("colorLifeEndG", e.colorLifeEndG);
        get("colorLifeEndB", e.colorLifeEndB);
        get("colorLifeEndA", e.colorLifeEndA);

        if (j.contains("spawnVelocity") && j["spawnVelocity"].is_array()) {
            auto& sv = j["spawnVelocity"];
            e.spawnVelocity = {sv[0].get<float>(), sv[1].get<float>(), sv[2].get<float>()};
        }
        if (j.contains("gravity") && j["gravity"].is_array()) {
            auto& g = j["gravity"];
            e.gravity = {g[0].get<float>(), g[1].get<float>(), g[2].get<float>()};
        }
        if (j.contains("initialColor") && j["initialColor"].is_array()) {
            auto& c = j["initialColor"];
            e.initialColor = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
        }
        // Tag metadata — tolerates missing + non-string entries (which are skipped).
        if (j.contains("tags") && j["tags"].is_array()) {
            e.tags.clear();
            for (auto& entry : j["tags"]) {
                if (entry.is_string()) {
                    e.tags.push_back(entry.get<std::string>());
                }
            }
        }

        // Spawn bursts (phase 11.8). Missing key = empty list =
        // no bursts fire. Malformed entries are silently dropped.
        if (j.contains("bursts") && j["bursts"].is_array()) {
            e.bursts.clear();
            e.bursts.reserve(j["bursts"].size());
            for (const auto& bj : j["bursts"]) {
                if (!bj.is_object()) continue;
                VfxBurstEvent b{};
                if (bj.contains("t") && bj["t"].is_number()) {
                    b.time = bj["t"].get<float>();
                }
                if (bj.contains("n") && bj["n"].is_number()) {
                    b.count = bj["n"].get<uint32_t>();
                }
                e.bursts.push_back(b);
            }
            // Ensure the ordering invariant even if the asset is out of order.
            std::sort(e.bursts.begin(), e.bursts.end(),
                       [](const VfxBurstEvent& a, const VfxBurstEvent& b) {
                           return a.time < b.time;
                       });
            e.burstCursor = 0u;
        }

        // Spawn rate curve (phase 11.7). Missing key → legacy
        // constant-rate behaviour. Malformed keys are silently dropped.
        if (j.contains("spawnRateCurve") && j["spawnRateCurve"].is_object()) {
            const auto& curve = j["spawnRateCurve"];
            std::vector<Keyframe> keys;
            if (curve.contains("keys") && curve["keys"].is_array()) {
                for (const auto& kj : curve["keys"]) {
                    if (!kj.is_object()) continue;
                    Keyframe k{};
                    if (kj.contains("t") && kj["t"].is_number()) {
                        k.time = kj["t"].get<float>();
                    }
                    if (kj.contains("v") && kj["v"].is_number()) {
                        k.value = kj["v"].get<float>();
                    }
                    keys.push_back(k);
                }
            }
            // Sort by time — SetKeys requires sorted input.
            std::sort(keys.begin(), keys.end(),
                       [](const Keyframe& a, const Keyframe& b) {
                           return a.time < b.time;
                       });
            e.spawnRateCurve.SetKeys(std::move(keys));
            if (curve.contains("interp") && curve["interp"].is_string()) {
                const std::string s = curve["interp"].get<std::string>();
                if (s == "Cubic") {
                    e.spawnRateCurve.SetInterpolation(InterpolationMode::Cubic);
                } else if (s == "Smoothstep") {
                    e.spawnRateCurve.SetInterpolation(InterpolationMode::Smoothstep);
                } else {
                    e.spawnRateCurve.SetInterpolation(InterpolationMode::Linear);
                }
            }
        }

        // Niagara-parity stack (phase 11.1.0). Missing `stages` means
        // legacy asset — leave `e.stack` empty so the inline-config
        // path continues to drive behaviour.
        if (j.contains("stages") && j["stages"].is_array()) {
            e.stack.Clear();
            for (const auto& stageObj : j["stages"]) {
                if (!stageObj.is_object()) continue;
                VfxStage stage = VfxStage::ParticleUpdate;
                if (stageObj.contains("stage") && stageObj["stage"].is_string()) {
                    (void)VfxStageFromName(stageObj["stage"].get<std::string>().c_str(), stage);
                }
                VfxStackEntry& entry = e.stack.GetEntry(stage);
                if (stageObj.contains("modules") && stageObj["modules"].is_array()) {
                    for (const auto& modJson : stageObj["modules"]) {
                        if (!modJson.is_object()) continue;
                        VfxModuleInstance mod;
                        if (modJson.contains("type") && modJson["type"].is_string()) {
                            mod.moduleTypeName = modJson["type"].get<std::string>();
                        }
                        if (mod.moduleTypeName.empty()) continue;
                        if (modJson.contains("instanceId") && modJson["instanceId"].is_string()) {
                            mod.instanceId = modJson["instanceId"].get<std::string>();
                        }
                        if (modJson.contains("enabled") && modJson["enabled"].is_boolean()) {
                            mod.enabled = modJson["enabled"].get<bool>();
                        }
                        if (modJson.contains("override") && modJson["override"].is_string()) {
                            (void)VfxOverrideFlagFromName(
                                modJson["override"].get<std::string>().c_str(),
                                mod.overrideFlag);
                        }
                        if (modJson.contains("config")) {
                            if (modJson["config"].is_string()) {
                                mod.configJson = modJson["config"].get<std::string>();
                            } else {
                                mod.configJson = modJson["config"].dump();
                            }
                        }
                        entry.Add(std::move(mod));
                    }
                }
            }
        }

        if (j.contains("userAttributes") && j["userAttributes"].is_array()) {
            e.userAttributes.clear();
            for (const auto& entry : j["userAttributes"]) {
                UserAttributeDef def;
                if (entry.contains("name") && entry["name"].is_string()) {
                    def.name = entry["name"].get<std::string>();
                }
                const std::string typeStr = entry.value("type", std::string{"float"});
                if (typeStr == "vec4") def.type = UserAttributeType::Vec4;
                else                   def.type = UserAttributeType::Float;
                if (entry.contains("default")) {
                    const auto& dv = entry["default"];
                    if (def.type == UserAttributeType::Vec4 && dv.is_array() && dv.size() >= 4) {
                        def.defaultVec4 = Vec4{
                            dv[0].get<float>(), dv[1].get<float>(),
                            dv[2].get<float>(), dv[3].get<float>()};
                    } else if (def.type == UserAttributeType::Float && dv.is_number()) {
                        def.defaultFloat = dv.get<float>();
                    }
                }
                (void)e.RegisterUserAttribute(def);
            }
        }

        return true;
    } catch (...) {
        return false;
    }
}

} // namespace uz
