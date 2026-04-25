/// VFX Spline Modules — implementation.
///
/// Each module surfaces a `GetShaderCode` HLSL snippet that the module
/// compositor stitches into the spawn / update CS. The snippets reference
/// three CB fields (`cbSplineSrvIndex`, `cbSplinePointCount`,
/// `cbSplineTotalLength`) that the inline-sim CB plumbing fills from the
/// emitter's `VfxSplineBuffer`. Until that wiring lands (tracked by
/// `phase16_vfx-spline-data-interface-hardware-validation`) the snippets
/// are dormant data — they compile only when the host CS adds the CB
/// fields, which `frame_vfx_inline.cpp` guards behind the presence of a
/// spline module on the emitter.
///
/// All three modules use `#include "spline_di.hlsli"` for the
/// `SplineSampleAtU` / `SplineSampleArclen` / `SplineNearest` helpers.

#include "spline_modules.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// JSON parse helpers — kept private; signatures mirror the public
// `MeshRenderModule::ParseConfig` style.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

float JFloat(const nlohmann::json& j, const char* key, float fallback) noexcept {
    if (j.contains(key) && j.at(key).is_number()) {
        return j.at(key).get<float>();
    }
    return fallback;
}

bool JBool(const nlohmann::json& j, const char* key, bool fallback) noexcept {
    if (j.contains(key) && j.at(key).is_boolean()) {
        return j.at(key).get<bool>();
    }
    return fallback;
}

} // anonymous namespace

// =============================================================================
// SpawnAlongSplineModule
// =============================================================================

/// HLSL snippet executed inside the spawn CS for each newly-allocated
/// particle. Reads the bindless `VfxSplineBuffer` and writes the
/// initial position via `SplineSampleAtU`.
///
/// The snippet expects the spawn CS to declare:
/// - `uint  cbSplineSrvIndex`     — CB field
/// - `uint  cbSplinePointCount`   — CB field
/// - `float cbSplineTotalLength`  — CB field (unused here; included for
///   the parametric `u` -> arc-length conversion when stratified mode
///   spaces particles by arc-length).
/// - `cbSplineSpawnStratified` — `0` / `1`
/// - `cbSplineSpawnJitterRadius` — float
/// - `cbSplineSpawnTangentOffset` — float
/// - `spawnIndex` — local index of the particle within this dispatch
/// - `spawnCount` — total particles spawned in this dispatch
/// - `Positions[particleIdx]` — write target (existing convention)
///
/// The compositor injects this snippet right after the standard spawn-
/// position assignment so the spline override takes effect.
static constexpr const char* kSpawnAlongSplineSnippet = R"hlsl(
// --- SpawnAlongSplineModule ---
{
    if (cbSplinePointCount >= 2u) {
        float u;
        if (cbSplineSpawnStratified != 0u) {
            // Uniform arc-length distribution. spawnIndex / max(1,spawnCount)
            // gives [0,1) sweep; +0.5/spawnCount centres particles in the
            // bucket so the first and last particles aren't on the endpoints.
            const float denom = max(1.0, float(spawnCount));
            u = (float(spawnIndex) + 0.5) / denom;
        } else {
            // Hash the particle index to a pseudo-random u. Deterministic
            // per particle so screenshot tests round-trip.
            uint h = particleIdx * 0x9E3779B1u;
            h ^= h >> 16; h *= 0x85EBCA6Bu;
            h ^= h >> 13; h *= 0xC2B2AE35u;
            h ^= h >> 16;
            u = float(h & 0xFFFFFFu) / float(0xFFFFFF);
        }

        VfxSplinePoint sp = SplineSampleAtU(cbSplineSrvIndex,
                                             cbSplinePointCount, u);
        float3 pos = sp.position + sp.tangent * cbSplineSpawnTangentOffset;

        if (cbSplineSpawnJitterRadius > 0.0) {
            // Build an arbitrary basis orthogonal to the tangent.
            float3 worldUp = abs(sp.tangent.z) < 0.9
                           ? float3(0, 0, 1) : float3(1, 0, 0);
            float3 right = normalize(cross(sp.tangent, worldUp));
            float3 up    = cross(sp.tangent, right);

            // Two more hashes for offset within the perpendicular disc.
            uint h1 = (particleIdx * 0xCC9E2D51u) ^ 0x1B873593u;
            h1 ^= h1 >> 13;
            float angle  = float(h1 & 0xFFFFu) / 65535.0 * 6.28318530;
            float radius = float((h1 >> 16) & 0xFFFFu) / 65535.0
                          * cbSplineSpawnJitterRadius;
            pos += right * (cos(angle) * radius)
                +  up    * (sin(angle) * radius);
        }

        Positions[particleIdx].xyz = pos;
        // Hand a default forward velocity along the tangent so the
        // particle has motion if no other module overrides it.
        Velocities[particleIdx].xyz = sp.tangent;
    }
}
)hlsl";

std::string_view SpawnAlongSplineModule::GetShaderCode() const {
    return kSpawnAlongSplineSnippet;
}

std::vector<ParameterDef> SpawnAlongSplineModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(3);
    {
        ParameterDef p;
        p.name        = "stratified";
        p.type        = ParameterType::Bool;
        p.defaultBool = config_.stratified;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "jitter_radius";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.jitterRadius;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "tangent_offset";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.tangentOffset;
        params.push_back(p);
    }
    return params;
}

SpawnAlongSplineConfig SpawnAlongSplineModule::ParseConfig(const nlohmann::json& j) {
    SpawnAlongSplineConfig cfg;
    cfg.stratified    = JBool (j, "stratified",      cfg.stratified);
    cfg.jitterRadius  = JFloat(j, "jitter_radius",   cfg.jitterRadius);
    cfg.tangentOffset = JFloat(j, "tangent_offset",  cfg.tangentOffset);
    return cfg;
}

void SpawnAlongSplineModule::ApplyJsonConfig(const nlohmann::json& config) {
    if (!config.is_object()) {
        UZ_LOG(VFX, Warning,
               "SpawnAlongSplineModule::ApplyJsonConfig: config is not an object");
        return;
    }
    config_ = ParseConfig(config);
}

// =============================================================================
// ForceTowardSplineModule
// =============================================================================

static constexpr const char* kForceTowardSplineSnippet = R"hlsl(
// --- ForceTowardSplineModule ---
{
    if (cbSplinePointCount >= 2u && cbSplineForceStiffness > 0.0) {
        float3 worldPos = Positions[particleIdx].xyz;
        VfxSplinePoint near = SplineNearest(cbSplineSrvIndex,
                                             cbSplinePointCount, worldPos);
        float3 toSpline = near.position - worldPos;
        float  dist     = length(toSpline);

        bool inRange = (cbSplineForceAttractDistance <= 0.0)
                    || (dist <= cbSplineForceAttractDistance);

        if (inRange && dist > 1e-4) {
            float3 force = toSpline * cbSplineForceStiffness;
            float  mag   = length(force);
            if (mag > cbSplineForceMaxAccel) {
                force = force * (cbSplineForceMaxAccel / mag);
            }
            Velocities[particleIdx].xyz += force * dt;
        }
    }
}
)hlsl";

std::string_view ForceTowardSplineModule::GetShaderCode() const {
    return kForceTowardSplineSnippet;
}

std::vector<ParameterDef> ForceTowardSplineModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(3);
    {
        ParameterDef p;
        p.name         = "stiffness";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.stiffness;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "max_acceleration";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.maxAcceleration;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "attract_distance";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.attractDistance;
        params.push_back(p);
    }
    return params;
}

ForceTowardSplineConfig ForceTowardSplineModule::ParseConfig(const nlohmann::json& j) {
    ForceTowardSplineConfig cfg;
    cfg.stiffness       = JFloat(j, "stiffness",         cfg.stiffness);
    cfg.maxAcceleration = JFloat(j, "max_acceleration",  cfg.maxAcceleration);
    cfg.attractDistance = JFloat(j, "attract_distance",  cfg.attractDistance);
    return cfg;
}

void ForceTowardSplineModule::ApplyJsonConfig(const nlohmann::json& config) {
    if (!config.is_object()) {
        UZ_LOG(VFX, Warning,
               "ForceTowardSplineModule::ApplyJsonConfig: config is not an object");
        return;
    }
    config_ = ParseConfig(config);
}

// =============================================================================
// FollowSplineModule
// =============================================================================

/// Snippet writes position from a per-particle u that advances by
/// lifetime fraction. Expects:
/// - `Ages[particleIdx]` and `Lifespans[particleIdx]` (existing).
/// - `cbSplineFollowSpeed`, `cbSplineFollowLoop`, `cbSplineFollowStartU`.
static constexpr const char* kFollowSplineSnippet = R"hlsl(
// --- FollowSplineModule ---
{
    if (cbSplinePointCount >= 2u) {
        float age      = Ages[particleIdx];
        float lifespan = max(Lifespans[particleIdx], 0.0001);
        float lifeFrac = saturate(age / lifespan);

        float u = cbSplineFollowStartU + lifeFrac * cbSplineFollowSpeed;
        if (cbSplineFollowLoop != 0u) {
            u = u - floor(u); // wrap to [0,1)
        } else {
            u = saturate(u);
        }

        VfxSplinePoint sp = SplineSampleAtU(cbSplineSrvIndex,
                                             cbSplinePointCount, u);
        Positions[particleIdx].xyz = sp.position;
        // Aim the velocity along the path direction so downstream
        // velocity-aligned modules (mesh rotation, stretch billboards)
        // pick up the correct heading for free.
        Velocities[particleIdx].xyz = sp.tangent
                                    * cbSplineFollowSpeed
                                    * cbSplineTotalLength
                                    / max(lifespan, 0.0001);
    }
}
)hlsl";

std::string_view FollowSplineModule::GetShaderCode() const {
    return kFollowSplineSnippet;
}

std::vector<ParameterDef> FollowSplineModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(3);
    {
        ParameterDef p;
        p.name         = "speed";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.speed;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name        = "loop";
        p.type        = ParameterType::Bool;
        p.defaultBool = config_.loop;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "start_u";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.startU;
        params.push_back(p);
    }
    return params;
}

FollowSplineConfig FollowSplineModule::ParseConfig(const nlohmann::json& j) {
    FollowSplineConfig cfg;
    cfg.speed  = JFloat(j, "speed",   cfg.speed);
    cfg.loop   = JBool (j, "loop",    cfg.loop);
    cfg.startU = JFloat(j, "start_u", cfg.startU);
    return cfg;
}

void FollowSplineModule::ApplyJsonConfig(const nlohmann::json& config) {
    if (!config.is_object()) {
        UZ_LOG(VFX, Warning,
               "FollowSplineModule::ApplyJsonConfig: config is not an object");
        return;
    }
    config_ = ParseConfig(config);
}

} // namespace uz
