/// VFX Preset Serializer — Implementation
///
/// Hand-rolled JSON serializer / deserializer for VFXEmitterConfig.
/// No third-party JSON library is used. The serializer writes one key/value
/// pair per line using fprintf-style formatting, and the loader scans for
/// `"key": value` patterns using a simple character-level state machine.

#include "vfx_preset_serializer.h"
#include "vfx_viewport_vfx.h"
#include "uzengine.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Save
// ─────────────────────────────────────────────────────────────────────────────

bool VFXPresetSerializer::Save(const VFXEmitterConfig& c,
                                float spawnRate,
                                const std::string& name,
                                const std::string& filePath) {
    // Ensure parent directory exists before opening.
    {
        std::filesystem::path p(filePath);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
            if (ec) {
                UZ_LOG(App, Warning, "VFXPresetSerializer::Save — failed to create directory '{}': {}",
                       p.parent_path().string().c_str(), ec.message().c_str());
                return false;
            }
        }
    }

    std::ofstream f(filePath);
    if (!f.is_open()) {
        UZ_LOG(App, Warning, "VFXPresetSerializer::Save — cannot open '{}' for writing",
               filePath.c_str());
        return false;
    }

    // Helper lambda — writes a float with enough decimal places to round-trip.
    auto wf = [&](const char* key, float v, bool last = false) {
        f << "  \"" << key << "\": ";
        // Use %.8g for compact representation that still round-trips exactly.
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.8g", static_cast<double>(v));
        f << buf;
        if (!last) f << ",\n";
    };

    auto wu = [&](const char* key, uint32_t v, bool last = false) {
        f << "  \"" << key << "\": " << v;
        if (!last) f << ",\n";
    };

    auto ws = [&](const char* key, const std::string& v, bool last = false) {
        f << "  \"" << key << "\": \"" << v << "\"";
        if (!last) f << ",\n";
    };

    f << "{\n";

    // Header
    wu("version",   1);
    ws("name",      name);
    wf("spawnRate", spawnRate);

    // Position
    wf("posX", c.position[0]);
    wf("posY", c.position[1]);
    wf("posZ", c.position[2]);

    wf("spawnRadius", c.spawnRadius);

    // Spawn velocity
    wf("velX", c.spawnVelocity[0]);
    wf("velY", c.spawnVelocity[1]);
    wf("velZ", c.spawnVelocity[2]);

    wf("velocityVariation", c.velocityVariation);
    wf("spreadAngle",       c.spreadAngle);

    wf("minLifetime", c.minLifetime);
    wf("maxLifetime", c.maxLifetime);
    wf("initialSize", c.initialSize);

    // Initial color
    wf("initColorR", c.initialColor[0]);
    wf("initColorG", c.initialColor[1]);
    wf("initColorB", c.initialColor[2]);
    wf("initColorA", c.initialColor[3]);

    // Gravity
    wf("gravX", c.gravity[0]);
    wf("gravY", c.gravity[1]);
    wf("gravZ", c.gravity[2]);

    wf("damping",           c.damping);
    wf("softFadeDistance",  c.softFadeDistance);
    wf("emissiveIntensity", c.emissiveIntensity);

    // Size over lifetime
    wf("sizeOverLifeStart",    c.sizeOverLifeStart);
    wf("sizeOverLifePeak",     c.sizeOverLifePeak);
    wf("sizeOverLifeEnd",      c.sizeOverLifeEnd);
    wf("sizeOverLifePeakTime", c.sizeOverLifePeakTime);

    // Color over lifetime — start
    wf("colorLifeStartR", c.colorLifeStartR);
    wf("colorLifeStartG", c.colorLifeStartG);
    wf("colorLifeStartB", c.colorLifeStartB);
    wf("colorLifeStartA", c.colorLifeStartA);

    // Color over lifetime — end
    wf("colorLifeEndR", c.colorLifeEndR);
    wf("colorLifeEndG", c.colorLifeEndG);
    wf("colorLifeEndB", c.colorLifeEndB);
    wf("colorLifeEndA", c.colorLifeEndA);

    // Billboard alignment / stretch
    wu("alignmentMode", c.alignmentMode);
    wf("stretchFactor", c.stretchFactor);

    // Spawn shape
    wu("spawnShape",     c.spawnShape);
    wu("spawnOnSurface", c.spawnOnSurface);
    wf("shapeParam1",    c.shapeParam1);
    wf("shapeParam2",    c.shapeParam2);
    wf("shapeParam3",    c.shapeParam3);

    wu("blendMode", c.blendMode);

    // Curl noise
    wf("curlStrength",  c.curlStrength);
    wf("curlFrequency", c.curlFrequency);
    wf("curlSpeed",     c.curlSpeed);

    // Vortex force
    wf("vortexOriginX", c.vortexAxisOrigin[0]);
    wf("vortexOriginY", c.vortexAxisOrigin[1]);
    wf("vortexOriginZ", c.vortexAxisOrigin[2]);
    wf("vortexDirX",    c.vortexAxisDir[0]);
    wf("vortexDirY",    c.vortexAxisDir[1]);
    wf("vortexDirZ",    c.vortexAxisDir[2]);
    wf("vortexTangential",  c.vortexTangentialStrength);
    wf("vortexRadial",      c.vortexRadialStrength);
    wf("vortexMaxRadius",   c.vortexMaxRadius);

    // Wind
    wf("windDirX",     c.windDir[0]);
    wf("windDirY",     c.windDir[1]);
    wf("windDirZ",     c.windDir[2]);
    wf("windStrength", c.windStrength);

    // Lighting / collision (last fields — no trailing comma)
    wu("litMode",         c.litMode);
    wu("groundCollision", c.groundCollision);
    wf("restitution",     c.restitution, /*last=*/true);

    f << "\n}\n";
    f.flush();

    if (!f.good()) {
        UZ_LOG(App, Warning, "VFXPresetSerializer::Save — I/O error while writing '{}'",
               filePath.c_str());
        return false;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Load — hand-rolled key/value scanner
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Scans forward in `src` starting at `pos` past any whitespace and
/// structural JSON characters that are not part of a value (',', '{', '}').
static void SkipWhitespace(const std::string& src, size_t& pos) {
    while (pos < src.size() && (std::isspace(static_cast<unsigned char>(src[pos])) ||
                                 src[pos] == ',' || src[pos] == '{' || src[pos] == '}')) {
        ++pos;
    }
}

/// Reads a JSON string value (content between surrounding double-quotes).
/// `pos` must point at the opening `"`. Advances `pos` past the closing `"`.
/// Returns false if the opening `"` is not found.
static bool ReadStringValue(const std::string& src, size_t pos, std::string& out) {
    if (pos >= src.size() || src[pos] != '"') return false;
    ++pos; // skip opening "
    size_t start = pos;
    while (pos < src.size() && src[pos] != '"') ++pos;
    out = src.substr(start, pos - start);
    return true;
}

/// Reads a numeric (float or int) token from `src` at `pos`.
/// Advances `pos` past the number. Returns false if no digit/sign found.
static bool ReadNumberToken(const std::string& src, size_t pos, std::string& out) {
    // Skip leading whitespace (NOT via SkipWhitespace — we want to land on the digit)
    while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos;
    if (pos >= src.size()) return false;
    size_t start = pos;
    // Allow leading sign
    if (src[pos] == '-' || src[pos] == '+') ++pos;
    while (pos < src.size() &&
           (std::isdigit(static_cast<unsigned char>(src[pos])) ||
            src[pos] == '.' || src[pos] == 'e' || src[pos] == 'E' ||
            src[pos] == '+' || src[pos] == '-')) {
        ++pos;
    }
    if (pos == start) return false;
    out = src.substr(start, pos - start);
    return true;
}

/// Finds the first occurrence of `"key": ` in `src` and returns the position
/// immediately after the `:` and any following whitespace. Returns
/// `std::string::npos` if not found.
static size_t FindKeyValue(const std::string& src, const char* key) {
    // We search for: `"key":` (JSON key), allowing optional space before `:`.
    std::string needle = std::string("\"") + key + "\"";
    size_t pos = src.find(needle);
    if (pos == std::string::npos) return std::string::npos;

    // Advance past the key and find ':'
    pos += needle.size();
    while (pos < src.size() && src[pos] != ':') ++pos;
    if (pos >= src.size()) return std::string::npos;
    ++pos; // skip ':'
    // Skip whitespace after ':'
    while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos;
    return pos;
}

static float ParseFloat(const std::string& src, const char* key, float defaultVal) {
    size_t pos = FindKeyValue(src, key);
    if (pos == std::string::npos) return defaultVal;
    std::string tok;
    if (!ReadNumberToken(src, pos, tok)) return defaultVal;
    try { return std::stof(tok); } catch (...) { return defaultVal; }
}

static uint32_t ParseUint(const std::string& src, const char* key, uint32_t defaultVal) {
    size_t pos = FindKeyValue(src, key);
    if (pos == std::string::npos) return defaultVal;
    std::string tok;
    if (!ReadNumberToken(src, pos, tok)) return defaultVal;
    try { return static_cast<uint32_t>(std::stoul(tok)); } catch (...) { return defaultVal; }
}

static bool ParseString(const std::string& src, const char* key, std::string& out) {
    size_t pos = FindKeyValue(src, key);
    if (pos == std::string::npos) return false;
    // pos should point at '"'
    return ReadStringValue(src, pos, out);
}

} // anonymous namespace

bool VFXPresetSerializer::Load(const std::string& filePath,
                                VFXEmitterConfig& c,
                                float& outSpawnRate,
                                std::string& outName) {
    std::ifstream f(filePath);
    if (!f.is_open()) {
        UZ_LOG(App, Warning, "VFXPresetSerializer::Load — cannot open '{}'",
               filePath.c_str());
        return false;
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string src = ss.str();

    if (src.empty()) {
        UZ_LOG(App, Warning, "VFXPresetSerializer::Load — '{}' is empty", filePath.c_str());
        return false;
    }

    // Version check — warn but continue loading.
    const uint32_t version = ParseUint(src, "version", 0);
    if (version != 1) {
        UZ_LOG(App, Warning,
               "VFXPresetSerializer::Load — '{}' has unexpected version {}, expected 1. Continuing.",
               filePath.c_str(), version);
    }

    // Name
    if (!ParseString(src, "name", outName)) {
        // Fall back to filename stem if no name key found.
        outName = std::filesystem::path(filePath).stem().string();
    }

    // Spawn rate
    outSpawnRate = ParseFloat(src, "spawnRate", 500.0f);

    // Reset config to defaults so missing keys leave valid values.
    c = VFXEmitterConfig{};

    // Position
    c.position[0] = ParseFloat(src, "posX", c.position[0]);
    c.position[1] = ParseFloat(src, "posY", c.position[1]);
    c.position[2] = ParseFloat(src, "posZ", c.position[2]);

    c.spawnRadius = ParseFloat(src, "spawnRadius", c.spawnRadius);

    // Spawn velocity
    c.spawnVelocity[0] = ParseFloat(src, "velX", c.spawnVelocity[0]);
    c.spawnVelocity[1] = ParseFloat(src, "velY", c.spawnVelocity[1]);
    c.spawnVelocity[2] = ParseFloat(src, "velZ", c.spawnVelocity[2]);

    c.velocityVariation = ParseFloat(src, "velocityVariation", c.velocityVariation);
    c.spreadAngle       = ParseFloat(src, "spreadAngle",       c.spreadAngle);

    c.minLifetime = ParseFloat(src, "minLifetime", c.minLifetime);
    c.maxLifetime = ParseFloat(src, "maxLifetime", c.maxLifetime);
    c.initialSize = ParseFloat(src, "initialSize", c.initialSize);

    // Initial color
    c.initialColor[0] = ParseFloat(src, "initColorR", c.initialColor[0]);
    c.initialColor[1] = ParseFloat(src, "initColorG", c.initialColor[1]);
    c.initialColor[2] = ParseFloat(src, "initColorB", c.initialColor[2]);
    c.initialColor[3] = ParseFloat(src, "initColorA", c.initialColor[3]);

    // Gravity
    c.gravity[0] = ParseFloat(src, "gravX", c.gravity[0]);
    c.gravity[1] = ParseFloat(src, "gravY", c.gravity[1]);
    c.gravity[2] = ParseFloat(src, "gravZ", c.gravity[2]);

    c.damping           = ParseFloat(src, "damping",           c.damping);
    c.softFadeDistance  = ParseFloat(src, "softFadeDistance",  c.softFadeDistance);
    c.emissiveIntensity = ParseFloat(src, "emissiveIntensity", c.emissiveIntensity);

    // Size over lifetime
    c.sizeOverLifeStart    = ParseFloat(src, "sizeOverLifeStart",    c.sizeOverLifeStart);
    c.sizeOverLifePeak     = ParseFloat(src, "sizeOverLifePeak",     c.sizeOverLifePeak);
    c.sizeOverLifeEnd      = ParseFloat(src, "sizeOverLifeEnd",      c.sizeOverLifeEnd);
    c.sizeOverLifePeakTime = ParseFloat(src, "sizeOverLifePeakTime", c.sizeOverLifePeakTime);

    // Color over lifetime — start
    c.colorLifeStartR = ParseFloat(src, "colorLifeStartR", c.colorLifeStartR);
    c.colorLifeStartG = ParseFloat(src, "colorLifeStartG", c.colorLifeStartG);
    c.colorLifeStartB = ParseFloat(src, "colorLifeStartB", c.colorLifeStartB);
    c.colorLifeStartA = ParseFloat(src, "colorLifeStartA", c.colorLifeStartA);

    // Color over lifetime — end
    c.colorLifeEndR = ParseFloat(src, "colorLifeEndR", c.colorLifeEndR);
    c.colorLifeEndG = ParseFloat(src, "colorLifeEndG", c.colorLifeEndG);
    c.colorLifeEndB = ParseFloat(src, "colorLifeEndB", c.colorLifeEndB);
    c.colorLifeEndA = ParseFloat(src, "colorLifeEndA", c.colorLifeEndA);

    // Billboard
    c.alignmentMode = ParseUint(src, "alignmentMode", c.alignmentMode);
    c.stretchFactor = ParseFloat(src, "stretchFactor", c.stretchFactor);

    // Spawn shape
    c.spawnShape     = ParseUint(src, "spawnShape",     c.spawnShape);
    c.spawnOnSurface = ParseUint(src, "spawnOnSurface", c.spawnOnSurface);
    c.shapeParam1    = ParseFloat(src, "shapeParam1",   c.shapeParam1);
    c.shapeParam2    = ParseFloat(src, "shapeParam2",   c.shapeParam2);
    c.shapeParam3    = ParseFloat(src, "shapeParam3",   c.shapeParam3);

    c.blendMode = ParseUint(src, "blendMode", c.blendMode);

    // Curl noise
    c.curlStrength  = ParseFloat(src, "curlStrength",  c.curlStrength);
    c.curlFrequency = ParseFloat(src, "curlFrequency", c.curlFrequency);
    c.curlSpeed     = ParseFloat(src, "curlSpeed",     c.curlSpeed);

    // Vortex force
    c.vortexAxisOrigin[0]        = ParseFloat(src, "vortexOriginX",   c.vortexAxisOrigin[0]);
    c.vortexAxisOrigin[1]        = ParseFloat(src, "vortexOriginY",   c.vortexAxisOrigin[1]);
    c.vortexAxisOrigin[2]        = ParseFloat(src, "vortexOriginZ",   c.vortexAxisOrigin[2]);
    c.vortexAxisDir[0]           = ParseFloat(src, "vortexDirX",      c.vortexAxisDir[0]);
    c.vortexAxisDir[1]           = ParseFloat(src, "vortexDirY",      c.vortexAxisDir[1]);
    c.vortexAxisDir[2]           = ParseFloat(src, "vortexDirZ",      c.vortexAxisDir[2]);
    c.vortexTangentialStrength   = ParseFloat(src, "vortexTangential",c.vortexTangentialStrength);
    c.vortexRadialStrength       = ParseFloat(src, "vortexRadial",    c.vortexRadialStrength);
    c.vortexMaxRadius            = ParseFloat(src, "vortexMaxRadius", c.vortexMaxRadius);

    // Wind
    c.windDir[0]    = ParseFloat(src, "windDirX",     c.windDir[0]);
    c.windDir[1]    = ParseFloat(src, "windDirY",     c.windDir[1]);
    c.windDir[2]    = ParseFloat(src, "windDirZ",     c.windDir[2]);
    c.windStrength  = ParseFloat(src, "windStrength", c.windStrength);

    // Lighting / collision
    c.litMode         = ParseUint(src,  "litMode",         c.litMode);
    c.groundCollision = ParseUint(src,  "groundCollision", c.groundCollision);
    c.restitution     = ParseFloat(src, "restitution",     c.restitution);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ScanDirectory
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::string> VFXPresetSerializer::ScanDirectory(const std::string& dir) {
    std::vector<std::string> result;

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) {
        // Directory does not exist — return empty list silently.
        return result;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break; // Iterator error — stop silently.
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        result.push_back(std::filesystem::absolute(entry.path()).string());
    }

    // Sort alphabetically by filename stem (case-insensitive on Windows).
    std::sort(result.begin(), result.end(),
              [](const std::string& a, const std::string& b) {
                  const std::string sa = std::filesystem::path(a).stem().string();
                  const std::string sb = std::filesystem::path(b).stem().string();
                  // Lowercase compare so "Fire" and "fire" are adjacent.
                  std::string la = sa, lb = sb;
                  std::transform(la.begin(), la.end(), la.begin(),
                                 [](unsigned char c) { return std::tolower(c); });
                  std::transform(lb.begin(), lb.end(), lb.begin(),
                                 [](unsigned char c) { return std::tolower(c); });
                  return la < lb;
              });

    return result;
}

} // namespace uz
