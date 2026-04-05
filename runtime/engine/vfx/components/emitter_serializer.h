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
#include <nlohmann/json.hpp>
#include <string>

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
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace uz
