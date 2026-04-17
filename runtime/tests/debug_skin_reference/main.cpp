/// TEMPORARY DEBUGGING TEST — debug_skin_reference
///
/// Standalone executable that runs **two independent skinning paths** on the
/// same Mushroom_Idle clip at t=0.5s and writes the resulting deformed meshes
/// as .obj files for visual + numeric comparison.
///
///   1. REFERENCE path — uses ufbx's runtime evaluator directly:
///        palette[i] = node_to_world_at_t * cluster->geometry_to_bone
///        skinned    = sum(weight * palette[i] * vertex_in_geometry_space)
///      then applies the cyclic axis remap (x,y,z)→(z,x,y) to the final
///      position so the reference output sits in UzEngine's Z-up vertex space.
///
///   2. UZENGINE path — replicates the production pipeline exactly:
///        - same bind decomposition (DecomposeUfbxMatrix with cyclic remap)
///        - same TRS-walk palette compose (ComputeInverseBindMatrices +
///          UpdatePaletteFromPose)
///        - same row-vector LBS (SkinCpu)
///
/// Both paths consume the *same* unique-corner vertex list (no meshopt
/// reordering) so the dump files have identical vertex ordering and can be
/// compared one-to-one.
///
/// Usage:
///   debug_skin_reference.exe <Mushroom_Mesh.fbx> <Mushroom_Idle.fbx> <out_dir> [time_seconds]
///
/// Output:
///   <out_dir>/ref.obj  — reference path
///   <out_dir>/uz.obj   — UzEngine path
///   <out_dir>/bind.obj — un-deformed bind pose (sanity check)
///   <out_dir>/stats.txt — per-vertex deltas + summary statistics
///
/// This file is allowed to live under runtime/tests/ as a debugging aid;
/// it does NOT participate in the unit-test glob (it lives in a sub-directory)
/// and it does NOT modify any production code.

#include <ufbx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Small linear-algebra primitives (kept intentionally local — we want this
// test to be independent of the engine's Mat4 / Quat / Vec3 implementations).
// ─────────────────────────────────────────────────────────────────────────────

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

struct Quat {
    double x = 0, y = 0, z = 0, w = 1;
};

/// 4x4 row-major matrix.  Element (row r, col c) is m[r*4 + c].
/// All multiplies use the row-vector convention `out = v * M` to match the
/// engine (`Mat4 * Mat4` and `MulRowPoint` in skeletal_mesh_instance.cpp).
struct Mat4 {
    double m[16] = {1, 0, 0, 0,
                    0, 1, 0, 0,
                    0, 0, 1, 0,
                    0, 0, 0, 1};
    double& at(int r, int c)             { return m[r * 4 + c]; }
    double  at(int r, int c) const       { return m[r * 4 + c]; }
};

Mat4 MatIdentity() { return Mat4{}; }

Mat4 MatZero() {
    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = 0.0;
    return r;
}

/// Row-vector matrix multiply: result = a * b (apply a then b to a row vector).
Mat4 MatMul(const Mat4& a, const Mat4& b) {
    Mat4 r = MatZero();
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                r.at(i, j) += a.at(i, k) * b.at(k, j);
    return r;
}

/// Transform a point (homogeneous w=1) by a row-major matrix using the
/// row-vector convention: out_row = (px, py, pz, 1) * M.
Vec3 MatTransformRowPoint(const Vec3& p, const Mat4& m) {
    return Vec3{
        p.x * m.at(0, 0) + p.y * m.at(1, 0) + p.z * m.at(2, 0) + m.at(3, 0),
        p.x * m.at(0, 1) + p.y * m.at(1, 1) + p.z * m.at(2, 1) + m.at(3, 1),
        p.x * m.at(0, 2) + p.y * m.at(1, 2) + p.z * m.at(2, 2) + m.at(3, 2)};
}

/// In-place scaled add: dst += s * src (used to blend palette matrices).
void MatAddScaled(Mat4& dst, const Mat4& src, double s) {
    for (int k = 0; k < 16; ++k) dst.m[k] += src.m[k] * s;
}

/// 4x4 matrix inverse using cofactor expansion. Used for bind-matrix inversion.
Mat4 MatInverse(const Mat4& M) {
    const double* m = M.m;
    double inv[16];

    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9] *m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9] *m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9] *m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9] *m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6] *m[15] - m[1]*m[7] *m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6] *m[15] + m[0]*m[7] *m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5] *m[15] - m[0]*m[7] *m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5] *m[14] + m[0]*m[6] *m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6] *m[11] + m[1]*m[7] *m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9] *m[2]*m[7]  + m[9] *m[3]*m[6];
    inv[7]  =  m[0]*m[6] *m[11] - m[0]*m[7] *m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8] *m[2]*m[7]  - m[8] *m[3]*m[6];
    inv[11] = -m[0]*m[5] *m[11] + m[0]*m[7] *m[9]  + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8] *m[1]*m[7]  + m[8] *m[3]*m[5];
    inv[15] =  m[0]*m[5] *m[10] - m[0]*m[6] *m[9]  - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8] *m[1]*m[6]  - m[8] *m[2]*m[5];

    double det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (std::fabs(det) < 1e-20) return MatIdentity();
    double invDet = 1.0 / det;

    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * invDet;
    return r;
}

/// Build an SRT row-vector matrix matching the engine's `Mat4::TRS`:
///   final = Scaling(s) * Rotation(q) * Translation(t)
/// In row-vector convention this is `v_out = v * S * R * T`.
Mat4 MatFromTRS(const Vec3& t, const Quat& q, const Vec3& s) {
    // Rotation matrix (row-vector form, matches DirectXMath / engine quat.toMatrix).
    const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    Mat4 R = MatIdentity();
    R.at(0, 0) = 1.0 - 2.0 * (yy + zz);
    R.at(0, 1) = 2.0 * (xy + wz);
    R.at(0, 2) = 2.0 * (xz - wy);
    R.at(1, 0) = 2.0 * (xy - wz);
    R.at(1, 1) = 1.0 - 2.0 * (xx + zz);
    R.at(1, 2) = 2.0 * (yz + wx);
    R.at(2, 0) = 2.0 * (xz + wy);
    R.at(2, 1) = 2.0 * (yz - wx);
    R.at(2, 2) = 1.0 - 2.0 * (xx + zz) - 0.0; // overwritten below
    R.at(2, 2) = 1.0 - 2.0 * (xx + yy);

    Mat4 S = MatIdentity();
    S.at(0, 0) = s.x; S.at(1, 1) = s.y; S.at(2, 2) = s.z;

    Mat4 T = MatIdentity();
    T.at(3, 0) = t.x; T.at(3, 1) = t.y; T.at(3, 2) = t.z;

    return MatMul(MatMul(S, R), T);
}

/// Quaternion multiply (Hamilton product). Used to compose bone rotations in
/// the engine's TRS hierarchy walk.
Quat QuatMul(const Quat& a, const Quat& b) {
    return Quat{
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z};
}

Quat QuatInverse(const Quat& q) {
    // Unit quaternion → conjugate is the inverse.
    return Quat{-q.x, -q.y, -q.z, q.w};
}

Quat QuatNormalize(const Quat& q) {
    const double n2 = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (n2 < 1e-20) return Quat{0, 0, 0, 1};
    const double inv = 1.0 / std::sqrt(n2);
    return Quat{q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

/// Slerp between two quaternions (shortest-arc selection via dot sign).
Quat QuatSlerp(const Quat& a, const Quat& b, double t) {
    Quat bb = b;
    double dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    if (dot < 0.0) {
        bb = Quat{-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    if (dot > 0.9995) {
        Quat r{a.x + t*(bb.x - a.x), a.y + t*(bb.y - a.y),
               a.z + t*(bb.z - a.z), a.w + t*(bb.w - a.w)};
        return QuatNormalize(r);
    }
    const double theta_0 = std::acos(dot);
    const double theta   = theta_0 * t;
    const double sin_t   = std::sin(theta);
    const double sin_t0  = std::sin(theta_0);
    const double s0 = std::cos(theta) - dot * sin_t / sin_t0;
    const double s1 = sin_t / sin_t0;
    return Quat{a.x*s0 + bb.x*s1, a.y*s0 + bb.y*s1,
                a.z*s0 + bb.z*s1, a.w*s0 + bb.w*s1};
}

/// Rotate a vector by a quaternion: v' = q * v * q^-1, optimized form.
Vec3 QuatRotate(const Quat& q, const Vec3& v) {
    const double xx = q.x*v.x, yy = q.y*v.y, zz = q.z*v.z;
    const double t0 = 2.0 * (q.y * v.z - q.z * v.y);
    const double t1 = 2.0 * (q.z * v.x - q.x * v.z);
    const double t2 = 2.0 * (q.x * v.y - q.y * v.x);
    return Vec3{
        v.x + q.w * t0 + (q.y * t2 - q.z * t1),
        v.y + q.w * t1 + (q.z * t0 - q.x * t2),
        v.z + q.w * t2 + (q.x * t1 - q.y * t0)};
    (void)xx; (void)yy; (void)zz;
}

// ─────────────────────────────────────────────────────────────────────────────
// ufbx_matrix → row-major Mat4 conversion.
//
// ufbx uses a 4x3 column-major affine matrix where:
//     out.x = m00*px + m01*py + m02*pz + m03
//     out.y = m10*px + m11*py + m12*pz + m13
//     out.z = m20*px + m21*py + m22*pz + m23
// In other words ufbx applies M to v as `out = M * v_h` (column-vector style).
// Our local Mat4 uses row-vector style `out = v * M`.  These two are related
// by transpose: row-major Mat4 with R = transpose(ufbx) makes them equivalent.
// ─────────────────────────────────────────────────────────────────────────────

Mat4 UfbxMatrixToRow(const ufbx_matrix& u) {
    Mat4 r = MatZero();
    // col 0 = (m00, m10, m20)  — becomes row 0 of the row-vector matrix
    r.at(0, 0) = u.m00; r.at(0, 1) = u.m10; r.at(0, 2) = u.m20; r.at(0, 3) = 0.0;
    r.at(1, 0) = u.m01; r.at(1, 1) = u.m11; r.at(1, 2) = u.m21; r.at(1, 3) = 0.0;
    r.at(2, 0) = u.m02; r.at(2, 1) = u.m12; r.at(2, 2) = u.m22; r.at(2, 3) = 0.0;
    r.at(3, 0) = u.m03; r.at(3, 1) = u.m13; r.at(3, 2) = u.m23; r.at(3, 3) = 1.0;
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cyclic axis remap (FBX.X→UE.Y, FBX.Y→UE.Z, FBX.Z→UE.X)
//   v_uz = (v_fbx.z, v_fbx.x, v_fbx.y)
// Same remap is hard-coded in fbx_importer.cpp:1217-1219 for vertex positions
// and in DecomposeUfbxMatrix for bone TRS values.
// ─────────────────────────────────────────────────────────────────────────────

Vec3 CyclicRemap(const Vec3& v) {
    return Vec3{v.z, v.x, v.y};
}

Quat CyclicRemapQuat(const Quat& q) {
    return Quat{q.z, q.x, q.y, q.w};
}

// ─────────────────────────────────────────────────────────────────────────────
// SkinVertex — local representation of a single vertex with skinning data.
// Stored in *raw FBX geometry space* (no axis remap applied yet).
// ─────────────────────────────────────────────────────────────────────────────

struct SkinVertex {
    Vec3 position;                   // raw FBX geometry-space position
    std::array<int, 4> boneIdx{};    // bone indices (into our skeleton)
    std::array<double, 4> weight{};  // normalized weights, sum ≈ 1.0
};

// ─────────────────────────────────────────────────────────────────────────────
// Bone — per-bone metadata extracted from the mushroom mesh FBX.
// ─────────────────────────────────────────────────────────────────────────────

struct Bone {
    std::string name;
    int parentIdx = -1;             // index into bones[] or -1 for root

    // Reference-path data (raw ufbx, no axis remap):
    ufbx_matrix geometry_to_bone{}; // cluster->geometry_to_bone (correct skinning input)
    ufbx_matrix bind_to_world{};    // cluster->bind_to_world (engine's "bind" matrix)
    ufbx_node*  meshNode = nullptr; // bone node in the mushroom mesh scene

    // Engine-path data (with cyclic remap applied):
    Vec3 bindLocalPos{};
    Quat bindLocalRot{};
    Vec3 bindLocalScale{1, 1, 1};
    Mat4 inverseBindWorldEng = MatIdentity(); // ComputeInverseBindMatrices result
};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize n = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(data.data()), n);
    return data;
}

ufbx_load_opts MushroomLoadOpts() {
    // Identical to the production importer (fbx_importer.cpp:976-979).
    ufbx_load_opts opts = {};
    opts.target_unit_meters     = 0.01f;
    opts.space_conversion       = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.generate_missing_normals = true;
    return opts;
}

void WriteObj(const std::string& path, const std::vector<Vec3>& positions,
              const std::vector<uint32_t>& indices) {
    std::ofstream out(path);
    out.setf(std::ios::fixed);
    out.precision(4);
    for (const auto& p : positions) {
        out << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
    }
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        out << "f " << (indices[i] + 1) << ' '
                    << (indices[i + 1] + 1) << ' '
                    << (indices[i + 2] + 1) << '\n';
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Replicates fbx_importer.cpp:280-303 (ComputeLocalFromWorldTRS).
// Extracts a bone's parent-relative TRS from its world-space TRS.
// ─────────────────────────────────────────────────────────────────────────────

struct EngTRS { Vec3 t; Quat r; Vec3 s; };

EngTRS ComputeLocalFromWorld(const EngTRS& world, const EngTRS& parent) {
    EngTRS l;
    l.s = Vec3{world.s.x / parent.s.x, world.s.y / parent.s.y, world.s.z / parent.s.z};
    const Quat parentInv = QuatInverse(parent.r);
    l.r = QuatNormalize(QuatMul(parentInv, world.r));
    const Vec3 delta{world.t.x - parent.t.x, world.t.y - parent.t.y, world.t.z - parent.t.z};
    const Vec3 invRotDelta = QuatRotate(parentInv, delta);
    l.t = Vec3{invRotDelta.x / parent.s.x,
               invRotDelta.y / parent.s.y,
               invRotDelta.z / parent.s.z};
    return l;
}

// ─────────────────────────────────────────────────────────────────────────────
// Replicates BoneTransform::ComposeWith (bone_transform.h:117-123)
// and the engine's Mat4::TRS for the Mat4 flatten step.
// ─────────────────────────────────────────────────────────────────────────────

EngTRS ComposeWith(const EngTRS& local, const EngTRS& parent) {
    EngTRS r;
    r.s = Vec3{parent.s.x * local.s.x, parent.s.y * local.s.y, parent.s.z * local.s.z};
    r.r = QuatNormalize(QuatMul(parent.r, local.r));
    const Vec3 scaledLocal{parent.s.x * local.t.x,
                           parent.s.y * local.t.y,
                           parent.s.z * local.t.z};
    const Vec3 rot = QuatRotate(parent.r, scaledLocal);
    r.t = Vec3{parent.t.x + rot.x, parent.t.y + rot.y, parent.t.z + rot.z};
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Decompose ufbx_matrix and apply cyclic remap (mirrors DecomposeUfbxMatrix
// in fbx_importer.cpp:240-268).
// ─────────────────────────────────────────────────────────────────────────────

EngTRS DecomposeUfbxRemapped(const ufbx_matrix& m) {
    ufbx_transform t = ufbx_matrix_to_transform(&m);
    EngTRS out;
    out.t = CyclicRemap(Vec3{t.translation.x, t.translation.y, t.translation.z});
    out.r = CyclicRemapQuat(Quat{t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w});
    out.s = CyclicRemap(Vec3{t.scale.x, t.scale.y, t.scale.z});
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// PackBoneWeights replication (fbx_importer.cpp:796-862).
// ─────────────────────────────────────────────────────────────────────────────

void PackWeights(const ufbx_skin_deformer* skin,
                 uint32_t vertIdx,
                 const std::vector<int>& clusterToBone,
                 std::array<int, 4>& outIdx,
                 std::array<double, 4>& outWeight) {
    outIdx = {0, 0, 0, 0};
    outWeight = {0.0, 0.0, 0.0, 0.0};
    if (vertIdx >= skin->vertices.count) return;
    const ufbx_skin_vertex& sv = skin->vertices.data[vertIdx];
    const uint32_t n = std::min<uint32_t>(sv.num_weights, 4);
    if (n == 0) return;

    double rawW[4] = {0, 0, 0, 0};
    int idx[4] = {0, 0, 0, 0};
    double sum = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
        const ufbx_skin_weight& sw = skin->weights.data[sv.weight_begin + i];
        const uint32_t c = sw.cluster_index;
        if (c < clusterToBone.size()) idx[i] = clusterToBone[c];
        const double w = static_cast<double>(sw.weight);
        rawW[i] = (w > 0.0) ? w : 0.0;
        sum += rawW[i];
    }
    if (sum > 1e-9) {
        const double inv = 1.0 / sum;
        for (int i = 0; i < 4; ++i) {
            outWeight[i] = rawW[i] * inv;
            outIdx[i]    = idx[i];
        }
    }
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
            "Usage: %s <Mushroom_Mesh.fbx> <Mushroom_Idle.fbx> <out_dir> [time_seconds=0.5]\n",
            argv[0]);
        return 1;
    }

    const std::string meshPath  = argv[1];
    const std::string animPath  = argv[2];
    const std::string outDir    = argv[3];
    const double sampleTime     = (argc >= 5) ? std::atof(argv[4]) : 0.5;

    std::filesystem::create_directories(outDir);

    // ── Load mushroom mesh FBX ────────────────────────────────────────────
    const auto meshBytes = ReadFile(meshPath);
    if (meshBytes.empty()) {
        std::fprintf(stderr, "Failed to read mesh FBX: %s\n", meshPath.c_str());
        return 2;
    }

    ufbx_load_opts meshOpts = MushroomLoadOpts();
    ufbx_error err = {};
    ufbx_scene* meshScene = ufbx_load_memory(meshBytes.data(), meshBytes.size(), &meshOpts, &err);
    if (!meshScene) {
        std::fprintf(stderr, "ufbx_load_memory(mesh) failed: %.*s\n",
                     int(err.description.length), err.description.data);
        return 3;
    }
    struct MeshSceneGuard { ufbx_scene* s; ~MeshSceneGuard() { if (s) ufbx_free_scene(s); } } meshGuard{meshScene};

    if (meshScene->meshes.count == 0) {
        std::fprintf(stderr, "No meshes in mesh FBX\n");
        return 4;
    }
    const ufbx_mesh* mesh = meshScene->meshes.data[0];
    if (mesh->skin_deformers.count == 0) {
        std::fprintf(stderr, "Mesh has no skin deformers\n");
        return 5;
    }
    const ufbx_skin_deformer* skin = mesh->skin_deformers.data[0];

    std::printf("Mesh: %u vertices, %u faces, %zu skin clusters\n",
                (unsigned)mesh->num_vertices, (unsigned)mesh->num_faces,
                skin->clusters.count);

    // ── Build the bone list (bones in the skin) ───────────────────────────
    std::vector<Bone> bones;
    std::unordered_map<ufbx_node*, int> nodeToBone;

    for (size_t ci = 0; ci < skin->clusters.count; ++ci) {
        const ufbx_skin_cluster* c = skin->clusters.data[ci];
        if (!c || !c->bone_node) continue;
        if (nodeToBone.count(c->bone_node)) continue;
        Bone b;
        b.name             = std::string(c->bone_node->name.data, c->bone_node->name.length);
        b.meshNode         = c->bone_node;
        b.geometry_to_bone = c->geometry_to_bone;
        b.bind_to_world    = c->bind_to_world;
        nodeToBone[c->bone_node] = static_cast<int>(bones.size());
        bones.push_back(b);
    }

    // Resolve parent indices by walking node->parent chain to first bone in our set.
    for (auto& b : bones) {
        ufbx_node* walk = b.meshNode->parent;
        while (walk) {
            auto it = nodeToBone.find(walk);
            if (it != nodeToBone.end()) { b.parentIdx = it->second; break; }
            walk = walk->parent;
        }
    }

    // Topological sort (parents before children). Compute depth, sort by depth.
    {
        std::vector<int> depth(bones.size(), 0);
        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t i = 0; i < bones.size(); ++i) {
                if (bones[i].parentIdx >= 0) {
                    const int d = depth[bones[i].parentIdx] + 1;
                    if (depth[i] < d) { depth[i] = d; changed = true; }
                }
            }
        }
        std::vector<int> order(bones.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            if (depth[a] != depth[b]) return depth[a] < depth[b];
            return a < b;
        });

        std::vector<Bone> sorted;
        sorted.reserve(bones.size());
        std::vector<int> remap(bones.size(), -1);
        for (size_t i = 0; i < order.size(); ++i) {
            remap[order[i]] = static_cast<int>(i);
        }
        for (size_t i = 0; i < order.size(); ++i) {
            Bone b = bones[order[i]];
            if (b.parentIdx >= 0) b.parentIdx = remap[b.parentIdx];
            sorted.push_back(std::move(b));
        }
        bones = std::move(sorted);
        // Rebuild nodeToBone to point at the new sorted indices.
        nodeToBone.clear();
        for (size_t i = 0; i < bones.size(); ++i) nodeToBone[bones[i].meshNode] = static_cast<int>(i);
    }

    // Cluster index → bone index map (for PackBoneWeights).
    std::vector<int> clusterToBone(skin->clusters.count, 0);
    for (size_t ci = 0; ci < skin->clusters.count; ++ci) {
        const ufbx_skin_cluster* c = skin->clusters.data[ci];
        if (!c || !c->bone_node) continue;
        auto it = nodeToBone.find(c->bone_node);
        if (it != nodeToBone.end()) clusterToBone[ci] = it->second;
    }

    std::printf("Skeleton: %zu bones (root='%s')\n",
                bones.size(),
                bones.empty() ? "(none)" : bones.front().name.c_str());

    // ── Engine-path bind decomposition + ComputeInverseBindMatrices ───────
    // Step 1: world bind from cluster->bind_to_world (with cyclic remap)
    std::vector<EngTRS> worldBindEng(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        worldBindEng[i] = DecomposeUfbxRemapped(bones[i].bind_to_world);
    }
    // Step 2: world → local relative to parent
    std::vector<EngTRS> localBindEng(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].parentIdx < 0) {
            localBindEng[i] = worldBindEng[i];
        } else {
            localBindEng[i] = ComputeLocalFromWorld(worldBindEng[i], worldBindEng[bones[i].parentIdx]);
        }
        bones[i].bindLocalPos   = localBindEng[i].t;
        bones[i].bindLocalRot   = localBindEng[i].r;
        bones[i].bindLocalScale = localBindEng[i].s;
    }
    // Step 3: re-walk via ComposeWith to get world TRS, flatten + invert (matches
    // Skeleton::ComputeInverseBindMatrices in skeleton.cpp:67-110).
    {
        std::vector<EngTRS> worldTRS(bones.size());
        for (size_t i = 0; i < bones.size(); ++i) {
            const EngTRS local{bones[i].bindLocalPos, bones[i].bindLocalRot, bones[i].bindLocalScale};
            if (bones[i].parentIdx < 0) {
                worldTRS[i] = local;
            } else {
                worldTRS[i] = ComposeWith(local, worldTRS[bones[i].parentIdx]);
            }
            const Mat4 worldMat = MatFromTRS(worldTRS[i].t, worldTRS[i].r, worldTRS[i].s);
            bones[i].inverseBindWorldEng = MatInverse(worldMat);
        }
    }

    // ── Build the unique-corner vertex list (no meshopt reorder) ──────────
    // Each triangulated face corner produces one SkinVertex. Triangles are
    // recorded in CCW order (matching fbx_importer.cpp:1265-1267 reversal).
    std::vector<SkinVertex> verts;
    std::vector<uint32_t>   indices;
    std::vector<uint32_t>   triBuf(256);

    for (size_t fi = 0; fi < mesh->num_faces; ++fi) {
        const ufbx_face& face = mesh->faces.data[fi];
        const size_t maxTri   = static_cast<size_t>(face.num_indices - 2) * 3;
        if (triBuf.size() < maxTri) triBuf.resize(maxTri);
        const uint32_t numTri = ufbx_triangulate_face(triBuf.data(), triBuf.size(), mesh, face);
        const uint32_t numIdx = numTri * 3;

        const uint32_t baseVert = static_cast<uint32_t>(verts.size());
        for (uint32_t ti = 0; ti < numIdx; ++ti) {
            const uint32_t cornerIdx = triBuf[ti];
            const ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, cornerIdx);

            SkinVertex v;
            v.position = Vec3{p.x, p.y, p.z};
            const uint32_t meshVertIdx = mesh->vertex_indices.data[cornerIdx];
            PackWeights(skin, meshVertIdx, clusterToBone, v.boneIdx, v.weight);

            const uint32_t out = static_cast<uint32_t>(verts.size());
            verts.push_back(v);
            indices.push_back(out);
            (void)baseVert;
        }
        // Reverse winding (CW → CCW) on the triangles we just appended.
        for (uint32_t t = 0; t < numTri; ++t) {
            const size_t i = indices.size() - numIdx + size_t(t) * 3;
            std::swap(indices[i + 1], indices[i + 2]);
        }
    }

    std::printf("Built %zu unique-corner vertices, %zu indices\n", verts.size(), indices.size());

    // Bind-pose dump (no skinning at all, just remapped raw vertices) ──────
    {
        std::vector<Vec3> positions;
        positions.reserve(verts.size());
        for (const auto& v : verts) positions.push_back(CyclicRemap(v.position));
        WriteObj((std::filesystem::path(outDir) / "bind.obj").string(), positions, indices);
    }

    // ── Load animation FBX and find Mushroom_Idle stack ───────────────────
    const auto animBytes = ReadFile(animPath);
    if (animBytes.empty()) {
        std::fprintf(stderr, "Failed to read anim FBX: %s\n", animPath.c_str());
        return 6;
    }

    ufbx_load_opts animOpts = MushroomLoadOpts();
    ufbx_scene* animScene = ufbx_load_memory(animBytes.data(), animBytes.size(), &animOpts, &err);
    if (!animScene) {
        std::fprintf(stderr, "ufbx_load_memory(anim) failed: %.*s\n",
                     int(err.description.length), err.description.data);
        return 7;
    }
    struct AnimSceneGuard { ufbx_scene* s; ~AnimSceneGuard() { if (s) ufbx_free_scene(s); } } animGuard{animScene};

    if (animScene->anim_stacks.count == 0) {
        std::fprintf(stderr, "Anim FBX has no anim_stacks\n");
        return 8;
    }
    const ufbx_anim_stack* stack = animScene->anim_stacks.data[0];
    std::printf("Anim: stack='%s', range=[%.3f, %.3f]\n",
                std::string(stack->name.data, stack->name.length).c_str(),
                stack->time_begin, stack->time_end);

    // For each engine bone, find the corresponding bone node in animScene by name.
    std::vector<ufbx_node*> animBoneNodes(bones.size(), nullptr);
    for (size_t b = 0; b < bones.size(); ++b) {
        for (size_t n = 0; n < animScene->nodes.count; ++n) {
            ufbx_node* node = animScene->nodes.data[n];
            if (!node) continue;
            if (std::string(node->name.data, node->name.length) == bones[b].name) {
                animBoneNodes[b] = node;
                break;
            }
        }
    }

    const double t = stack->time_begin + sampleTime;
    std::printf("Sampling at t=%.3f (offset %.3f from clip start)\n", t, sampleTime);

    // ── Debug dump: first bone node_to_world in BOTH scenes (bind/rest) ──
    // Helps spot orientation differences between mesh FBX and anim FBX —
    // a Y-up animation against a Z-up rest mesh produces a 90-degree rotation
    // bug that the engine's cyclic remap silently amplifies.
    if (!bones.empty() && bones.front().meshNode) {
        const ufbx_node* mn = bones.front().meshNode;
        const ufbx_node* an = animBoneNodes.front();
        std::printf("First bone '%s'\n", bones.front().name.c_str());
        std::printf("  mesh.node_to_world.cols[3] (raw FBX):  (%.3f, %.3f, %.3f)\n",
                    mn->node_to_world.m03, mn->node_to_world.m13, mn->node_to_world.m23);
        if (an) {
            std::printf("  anim.node_to_world.cols[3] (rest):    (%.3f, %.3f, %.3f)\n",
                        an->node_to_world.m03, an->node_to_world.m13, an->node_to_world.m23);
        }
        std::printf("  cluster->bind_to_world.cols[3]:        (%.3f, %.3f, %.3f)\n",
                    bones.front().bind_to_world.m03,
                    bones.front().bind_to_world.m13,
                    bones.front().bind_to_world.m23);
        std::printf("  cluster->geometry_to_bone.cols[3]:     (%.3f, %.3f, %.3f)\n",
                    bones.front().geometry_to_bone.m03,
                    bones.front().geometry_to_bone.m13,
                    bones.front().geometry_to_bone.m23);
        std::printf("  mesh scene root unit meters: %.4f, axes target=%d, anim scene unit meters: %.4f, axes target=%d\n",
                    meshScene->settings.unit_meters, (int)meshScene->settings.axes.up,
                    animScene->settings.unit_meters, (int)animScene->settings.axes.up);
    }

    // ── Evaluate the anim scene at time t ─────────────────────────────────
    ufbx_evaluate_opts evalOpts = {};
    evalOpts.evaluate_skinning   = false;
    evalOpts.evaluate_caches     = false;
    evalOpts.load_external_files = false;
    ufbx_scene* evScene = ufbx_evaluate_scene(animScene, stack->anim, t, &evalOpts, &err);
    if (!evScene) {
        std::fprintf(stderr, "ufbx_evaluate_scene failed: %.*s\n",
                     int(err.description.length), err.description.data);
        return 9;
    }
    struct EvSceneGuard { ufbx_scene* s; ~EvSceneGuard() { if (s) ufbx_free_scene(s); } } evGuard{evScene};

    // ── Reference path: build palette = node_to_world * geometry_to_bone ──
    // This is the canonical ufbx skinning recipe — `geometry_to_bone` already
    // accounts for the rest pose AND the geometry-to-node sub-transform of the
    // mesh node, which `bind_to_world` does NOT include.
    std::vector<Mat4> palettesRefRow(bones.size(), MatIdentity());
    for (size_t b = 0; b < bones.size(); ++b) {
        ufbx_node* anim = animBoneNodes[b];
        if (!anim) {
            // Bone not in anim scene → static, palette = identity (already set).
            continue;
        }
        ufbx_node* evNode = (anim->typed_id < evScene->nodes.count)
            ? evScene->nodes.data[anim->typed_id] : nullptr;
        if (!evNode) continue;

        ufbx_matrix paletteUfbx = ufbx_matrix_mul(&evNode->node_to_world, &bones[b].geometry_to_bone);
        palettesRefRow[b] = UfbxMatrixToRow(paletteUfbx);
    }

    // ── Engine path: sample animation curves into a Pose, walk TRS hierarchy,
    //    palette = inverseBind * worldMat (as in UpdatePaletteFromPose) ────
    // For the engine path we evaluate each animated bone the same way the
    // production importer would (DecomposeUfbxRemapped applied to the
    // evaluated node_to_world), then do the world→local conversion against
    // the parent's world matrix at time t (NOT against the parent in the bind
    // pose — that mirrors fbx_importer.cpp:697-714).
    std::vector<EngTRS> worldEngAtT(bones.size());
    for (size_t b = 0; b < bones.size(); ++b) {
        ufbx_node* anim = animBoneNodes[b];
        if (!anim) {
            // No anim node → static at bind pose
            worldEngAtT[b] = EngTRS{
                Vec3{bones[b].bindLocalPos.x, bones[b].bindLocalPos.y, bones[b].bindLocalPos.z},
                bones[b].bindLocalRot,
                bones[b].bindLocalScale};
            continue;
        }
        ufbx_node* evNode = (anim->typed_id < evScene->nodes.count)
            ? evScene->nodes.data[anim->typed_id] : nullptr;
        if (!evNode) {
            worldEngAtT[b] = EngTRS{bones[b].bindLocalPos, bones[b].bindLocalRot, bones[b].bindLocalScale};
            continue;
        }
        worldEngAtT[b] = DecomposeUfbxRemapped(evNode->node_to_world);
    }
    std::vector<EngTRS> localEngAtT(bones.size());
    for (size_t b = 0; b < bones.size(); ++b) {
        if (bones[b].parentIdx < 0 || !animBoneNodes[b]) {
            localEngAtT[b] = worldEngAtT[b];
        } else {
            localEngAtT[b] = ComputeLocalFromWorld(worldEngAtT[b], worldEngAtT[bones[b].parentIdx]);
        }
    }
    // UpdatePaletteFromPose: walk via ComposeWith → Mat4::TRS → palette[i] = inverseBind * world.
    std::vector<EngTRS> worldComposed(bones.size());
    std::vector<Mat4>   palettesEng(bones.size(), MatIdentity());
    for (size_t b = 0; b < bones.size(); ++b) {
        if (bones[b].parentIdx < 0) {
            worldComposed[b] = localEngAtT[b];
        } else {
            worldComposed[b] = ComposeWith(localEngAtT[b], worldComposed[bones[b].parentIdx]);
        }
        const Mat4 worldMat = MatFromTRS(worldComposed[b].t, worldComposed[b].r, worldComposed[b].s);
        // Engine: palette = inverseBindWorld * worldMatrix (row-vector mul order).
        palettesEng[b] = MatMul(bones[b].inverseBindWorldEng, worldMat);
    }

    // ── Skin both paths ───────────────────────────────────────────────────
    std::vector<Vec3> posRef(verts.size());
    std::vector<Vec3> posUz(verts.size());

    for (size_t i = 0; i < verts.size(); ++i) {
        const SkinVertex& v = verts[i];
        const double wsum = v.weight[0] + v.weight[1] + v.weight[2] + v.weight[3];

        // Reference: skin in raw FBX geometry space, then cyclic-remap.
        if (wsum < 1e-6) {
            posRef[i] = CyclicRemap(v.position);
        } else {
            Mat4 blended = MatZero();
            for (int k = 0; k < 4; ++k) {
                if (v.weight[k] > 0.0) {
                    MatAddScaled(blended, palettesRefRow[v.boneIdx[k]], v.weight[k]);
                }
            }
            const Vec3 skinned = MatTransformRowPoint(v.position, blended);
            posRef[i] = CyclicRemap(skinned);
        }

        // UzEngine: vertex is already in remapped Z-up space; palette also remapped.
        const Vec3 vremap = CyclicRemap(v.position);
        if (wsum < 1e-6) {
            posUz[i] = vremap;
        } else {
            Mat4 blended = MatZero();
            for (int k = 0; k < 4; ++k) {
                if (v.weight[k] > 0.0) {
                    MatAddScaled(blended, palettesEng[v.boneIdx[k]], v.weight[k]);
                }
            }
            posUz[i] = MatTransformRowPoint(vremap, blended);
        }
    }

    WriteObj((std::filesystem::path(outDir) / "ref.obj").string(), posRef, indices);
    WriteObj((std::filesystem::path(outDir) / "uz.obj").string(),  posUz,  indices);

    // ── Stats ─────────────────────────────────────────────────────────────
    std::vector<double> deltas(verts.size());
    double maxD = 0, sumD = 0;
    size_t within1cm = 0;
    for (size_t i = 0; i < verts.size(); ++i) {
        const double dx = posUz[i].x - posRef[i].x;
        const double dy = posUz[i].y - posRef[i].y;
        const double dz = posUz[i].z - posRef[i].z;
        const double d  = std::sqrt(dx*dx + dy*dy + dz*dz);
        deltas[i] = d;
        sumD += d;
        if (d > maxD) maxD = d;
        if (d <= 1.0) ++within1cm; // positions are in cm — 1 unit = 1 cm
    }
    const double meanD = sumD / std::max<size_t>(1, verts.size());
    const double pct1cm = 100.0 * within1cm / std::max<size_t>(1, verts.size());

    // Per-bone palette frobenius diff (how different are the actual matrices?)
    double maxPaletteDiff = 0.0;
    int    maxPaletteBone = -1;
    for (size_t b = 0; b < bones.size(); ++b) {
        double f = 0.0;
        for (int k = 0; k < 16; ++k) {
            const double d = palettesEng[b].m[k] - palettesRefRow[b].m[k];
            f += d * d;
        }
        f = std::sqrt(f);
        if (f > maxPaletteDiff) { maxPaletteDiff = f; maxPaletteBone = (int)b; }
    }

    std::ofstream stats((std::filesystem::path(outDir) / "stats.txt").string());
    stats.setf(std::ios::fixed);
    stats.precision(4);
    stats << "debug_skin_reference — Mushroom_Idle vs UzEngine pipeline\n";
    stats << "==========================================================\n";
    stats << "Mesh:        " << meshPath << '\n';
    stats << "Animation:   " << animPath << '\n';
    stats << "Sample t:    " << sampleTime << " s (absolute t=" << t << ")\n";
    stats << "Bones:       " << bones.size() << '\n';
    stats << "Vertices:    " << verts.size() << '\n';
    stats << '\n';
    stats << "Position deltas (cm)\n";
    stats << "  max:         " << maxD << '\n';
    stats << "  mean:        " << meanD << '\n';
    stats << "  within 1cm:  " << within1cm << " / " << verts.size()
          << "  (" << pct1cm << "%)\n";
    stats << '\n';
    stats << "Palette Frobenius diff (per-bone)\n";
    stats << "  max: " << maxPaletteDiff;
    if (maxPaletteBone >= 0) {
        stats << "  at bone[" << maxPaletteBone << "] = '"
              << bones[maxPaletteBone].name << "'";
    }
    stats << '\n';
    stats << '\n';
    stats << "5 representative vertices (ref vs uz)\n";
    const size_t step = std::max<size_t>(1, verts.size() / 6);
    for (int s = 0; s < 5; ++s) {
        const size_t i = std::min(verts.size() - 1, size_t(s) * step + step / 2);
        stats << "  v[" << i << "]:  ref=("
              << posRef[i].x << ", " << posRef[i].y << ", " << posRef[i].z << ")  "
              << "uz=(" << posUz[i].x << ", " << posUz[i].y << ", " << posUz[i].z << ")  "
              << "delta=" << deltas[i] << " cm\n";
    }

    // Dump per-bone palette translation for the first few bones to make
    // hand-debugging the divergence cheap.
    stats << '\n';
    stats << "First 6 bone palette translation rows (m[3,0..2])\n";
    for (size_t b = 0; b < std::min<size_t>(6, bones.size()); ++b) {
        stats << "  bone[" << b << "] '" << bones[b].name << "'  parent="
              << bones[b].parentIdx << '\n';
        stats << "    ref: ("
              << palettesRefRow[b].at(3, 0) << ", "
              << palettesRefRow[b].at(3, 1) << ", "
              << palettesRefRow[b].at(3, 2) << ")\n";
        stats << "    uz : ("
              << palettesEng[b].at(3, 0) << ", "
              << palettesEng[b].at(3, 1) << ", "
              << palettesEng[b].at(3, 2) << ")\n";
    }

    std::printf("Done. Wrote %s/{ref,uz,bind}.obj + stats.txt\n", outDir.c_str());
    std::printf("max delta = %.4f cm, mean = %.4f cm, within 1cm = %.2f%%\n",
                maxD, meanD, pct1cm);
    return 0;
}
