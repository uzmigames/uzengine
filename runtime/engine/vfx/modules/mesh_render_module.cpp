/// VFX Mesh Render Module -- implementation.
///
/// Provides the HLSL instance-transform snippet consumed by the mesh instance
/// compute shader, enum string conversion, JSON deserialization, and the
/// IVFXModule parameter list used by the editor and serializer.

#include "mesh_render_module.h"

#include "engine/logging/log.h"

#include <json.hpp>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// MeshRotationMode string conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Maps MeshRotationMode values to their canonical string representations.
const char* MeshRotationModeToString(MeshRotationMode mode) noexcept
{
    switch (mode) {
    case MeshRotationMode::None:            return "None";
    case MeshRotationMode::VelocityAligned: return "VelocityAligned";
    case MeshRotationMode::Random:          return "Random";
    default:                                return "Unknown";
    }
}

/// Parses a MeshRotationMode from its canonical string. Returns None and logs a
/// warning for unrecognised values rather than throwing, so corrupted assets
/// degrade gracefully.
MeshRotationMode MeshRotationModeFromString(std::string_view str)
{
    if (str == "None")            return MeshRotationMode::None;
    if (str == "VelocityAligned") return MeshRotationMode::VelocityAligned;
    if (str == "Random")          return MeshRotationMode::Random;

    UZ_LOG(VFX, Warning,
           "MeshRenderModule: unrecognised rotation_mode '{}' -- defaulting to None",
           str);
    return MeshRotationMode::None;
}

// ─────────────────────────────────────────────────────────────────────────────
// HLSL snippet -- per-particle instance transform writer
// ─────────────────────────────────────────────────────────────────────────────

/// HLSL snippet for VelocityAligned rotation mode.
///
/// Builds a look-at rotation matrix aligning the mesh's +Z axis with the
/// particle's velocity vector. Falls back to a 90-degree pitch rotation when
/// the velocity is near-zero to avoid a degenerate cross product.
static constexpr const char* kMeshRenderShaderCode_VelocityAligned = R"hlsl(
// --- MeshRenderModule (VelocityAligned) ---
{
    float  age      = Ages[particleIdx];
    float  lifespan = max(Lifespans[particleIdx], 0.001f);

    // Skip dead particles -- no instance transform emitted.
    if (age >= lifespan) return;

    float3 pos = Positions[particleIdx];
    float3 vel = Velocities[particleIdx];

    // Build orthonormal basis: forward = normalize(vel), up = world Y,
    // right = cross(forward, up). When velocity is near-zero the mesh
    // would snap erratically, so we fall back to a static forward.
    float3 forward = float3(0.0f, 0.0f, 1.0f);
    float  speed   = length(vel);
    if (speed > 1e-4f) {
        forward = vel / speed;
    }

    // Avoid degenerate cross when forward is parallel to world Y.
    float3 worldUp = abs(forward.y) < 0.999f
                   ? float3(0.0f, 1.0f, 0.0f)
                   : float3(1.0f, 0.0f, 0.0f);

    float3 right = normalize(cross(worldUp, forward));
    float3 up    = cross(forward, right);

    // Compose TRS matrix: scale then orient then translate.
    // Column-major layout to match HLSL float4x4.
    float sx = cbMeshScaleX;
    float sy = cbMeshScaleY;
    float sz = cbMeshScaleZ;

    float4x4 transform = float4x4(
        right.x * sx,   up.x * sy,   forward.x * sz,  pos.x,
        right.y * sx,   up.y * sy,   forward.y * sz,  pos.y,
        right.z * sx,   up.z * sy,   forward.z * sz,  pos.z,
        0.0f,           0.0f,        0.0f,             1.0f
    );

    // Atomically claim a slot in the instance transform buffer.
    uint instanceSlot = 0;
    InterlockedAdd(MeshInstanceCount[0], 1, instanceSlot);
    MeshInstanceTransforms[instanceSlot] = transform;
}
)hlsl";

/// HLSL snippet for Random rotation mode.
///
/// Derives a deterministic rotation from the particle index using a simple
/// integer hash. Produces three Euler-like angles, then builds separate
/// rotation matrices around X, Y, Z and concatenates them. This is cheaper
/// than quaternion-to-matrix conversion in a tight loop.
static constexpr const char* kMeshRenderShaderCode_Random = R"hlsl(
// --- MeshRenderModule (Random) ---
{
    float  age      = Ages[particleIdx];
    float  lifespan = max(Lifespans[particleIdx], 0.001f);

    if (age >= lifespan) return;

    float3 pos = Positions[particleIdx];

    // Deterministic pseudo-random angles from particle index.
    // Wang hash: well-distributed, low cost, no external state.
    uint seed = particleIdx;
    seed  = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed  = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed  = seed ^ (seed >> 15u);

    float angleX = float(seed & 0xFFFFu) / 65535.0f * 6.28318530f;
    seed  = seed >> 16u;
    float angleY = float(seed & 0xFFFFu) / 65535.0f * 6.28318530f;

    // Second hash round for Z angle.
    seed  = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    float angleZ = float(seed & 0xFFFFu) / 65535.0f * 6.28318530f;

    float cx = cos(angleX); float sx_r = sin(angleX);
    float cy = cos(angleY); float sy_r = sin(angleY);
    float cz = cos(angleZ); float sz_r = sin(angleZ);

    // Rotation = Rz * Ry * Rx (intrinsic XYZ Euler).
    float3x3 rot = float3x3(
        cy*cz,              cy*sz_r,             -sy_r,
        sx_r*sy_r*cz - cx*sz_r,  sx_r*sy_r*sz_r + cx*cz,  sx_r*cy,
        cx*sy_r*cz + sx_r*sz_r,  cx*sy_r*sz_r - sx_r*cz,  cx*cy
    );

    float scx = cbMeshScaleX;
    float scy = cbMeshScaleY;
    float scz = cbMeshScaleZ;

    float4x4 transform = float4x4(
        rot[0][0]*scx,  rot[0][1]*scy,  rot[0][2]*scz,  pos.x,
        rot[1][0]*scx,  rot[1][1]*scy,  rot[1][2]*scz,  pos.y,
        rot[2][0]*scx,  rot[2][1]*scy,  rot[2][2]*scz,  pos.z,
        0.0f,           0.0f,           0.0f,            1.0f
    );

    uint instanceSlot = 0;
    InterlockedAdd(MeshInstanceCount[0], 1, instanceSlot);
    MeshInstanceTransforms[instanceSlot] = transform;
}
)hlsl";

/// HLSL snippet for None rotation mode.
///
/// Emits an identity-rotation TRS matrix using only position and scale.
/// All instances face the same world direction -- useful for axis-symmetric
/// meshes like spherical sparks or circular coins.
static constexpr const char* kMeshRenderShaderCode_None = R"hlsl(
// --- MeshRenderModule (None) ---
{
    float  age      = Ages[particleIdx];
    float  lifespan = max(Lifespans[particleIdx], 0.001f);

    if (age >= lifespan) return;

    float3 pos = Positions[particleIdx];

    float sx = cbMeshScaleX;
    float sy = cbMeshScaleY;
    float sz = cbMeshScaleZ;

    float4x4 transform = float4x4(
        sx,   0.0f, 0.0f, pos.x,
        0.0f, sy,   0.0f, pos.y,
        0.0f, 0.0f, sz,   pos.z,
        0.0f, 0.0f, 0.0f, 1.0f
    );

    uint instanceSlot = 0;
    InterlockedAdd(MeshInstanceCount[0], 1, instanceSlot);
    MeshInstanceTransforms[instanceSlot] = transform;
}
)hlsl";

// ─────────────────────────────────────────────────────────────────────────────
// MeshRenderModule -- constructor
// ─────────────────────────────────────────────────────────────────────────────

/// Default-constructs with `MeshRenderModuleConfig` defaults -- required for
/// `ModuleRegistry::Register<MeshRenderModule>()`.
MeshRenderModule::MeshRenderModule()
    : MeshRenderModule(MeshRenderModuleConfig{})
{}

/// Stores the provided configuration. Both mesh_path and material_path are
/// intentionally not validated here -- the asset system handles missing assets
/// gracefully with placeholder meshes at load time.
MeshRenderModule::MeshRenderModule(const MeshRenderModuleConfig& config)
    : config_(config)
{
}

// ─────────────────────────────────────────────────────────────────────────────
// IVFXModule interface
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the module's stable serialisation name.
const char* MeshRenderModule::GetName() const noexcept
{
    return "mesh_render";
}

/// Selects the HLSL snippet matching the configured rotation mode.
/// The snippet is a static string literal -- no allocation occurs per call.
std::string_view MeshRenderModule::GetShaderCode() const
{
    switch (config_.rotationMode) {
    case MeshRotationMode::VelocityAligned:
        return kMeshRenderShaderCode_VelocityAligned;
    case MeshRotationMode::Random:
        return kMeshRenderShaderCode_Random;
    case MeshRotationMode::None:
    default:
        return kMeshRenderShaderCode_None;
    }
}

/// Declares scale and orientation parameters for editor UI.
///
/// `rotation_mode` is stored as an Int (enum ordinal) so it appears in the
/// constant buffer alongside the float scales without a separate branch in
/// the constant buffer layout.
std::vector<ParameterDef> MeshRenderModule::GetParameters() const
{
    std::vector<ParameterDef> params;
    params.reserve(5);

    {
        ParameterDef p;
        p.name         = "scale_x";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.meshScale.x;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "scale_y";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.meshScale.y;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name         = "scale_z";
        p.type         = ParameterType::Float;
        p.defaultFloat = config_.meshScale.z;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name        = "cast_shadow";
        p.type        = ParameterType::Bool;
        p.defaultBool = config_.castShadow;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name       = "rotation_mode";
        p.type       = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(config_.rotationMode);
        params.push_back(p);
    }

    return params;
}

/// No-op. Transform generation happens in the instance CS dispatch, not here.
void MeshRenderModule::Execute(const ExecuteContext& /*ctx*/)
{
    // The GPU does all the work. CPU bookkeeping (e.g., updating the instance
    // buffer descriptor index) is handled by the owning emitter before dispatch.
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration accessors
// ─────────────────────────────────────────────────────────────────────────────

const MeshRenderModuleConfig& MeshRenderModule::GetConfig() const noexcept
{
    return config_;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON deserialization
// ─────────────────────────────────────────────────────────────────────────────

/// Parses a MeshRenderModuleConfig from a JSON object.
///
/// We use value_or-style access on each field so missing fields transparently
/// retain their defaults. This tolerates forward-compatibility: a newer config
/// with extra fields simply skips them, and an older config without a field
/// gets the sensible default.
MeshRenderModuleConfig MeshRenderModule::ParseConfig(const nlohmann::json& j)
{
    MeshRenderModuleConfig cfg;

    if (j.contains("mesh_path") && j.at("mesh_path").is_string()) {
        cfg.meshPath = j.at("mesh_path").get<std::string>();
    }

    if (j.contains("material_path") && j.at("material_path").is_string()) {
        cfg.materialPath = j.at("material_path").get<std::string>();
    }

    if (j.contains("mesh_scale") && j.at("mesh_scale").is_array()) {
        const auto& scale = j.at("mesh_scale");
        if (scale.size() >= 3) {
            cfg.meshScale.x = scale[0].get<float>();
            cfg.meshScale.y = scale[1].get<float>();
            cfg.meshScale.z = scale[2].get<float>();
        }
    }

    if (j.contains("cast_shadow") && j.at("cast_shadow").is_boolean()) {
        cfg.castShadow = j.at("cast_shadow").get<bool>();
    }

    if (j.contains("rotation_mode") && j.at("rotation_mode").is_string()) {
        cfg.rotationMode = MeshRotationModeFromString(
            j.at("rotation_mode").get<std::string>());
    }

    return cfg;
}

} // namespace uz
