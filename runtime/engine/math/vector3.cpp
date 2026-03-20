/// Vec3 implementation — SIMD-accelerated via DirectXMath.

#include "vector3.h"
#include "vector2.h"


namespace uz {
using namespace DirectX;

float Vec3::dot(const Vec3& other) const {
    XMVECTOR v1 = toXM();
    XMVECTOR v2 = other.toXM();
    XMVECTOR result = XMVector3Dot(v1, v2);
    return XMVectorGetX(result);
}

Vec3 Vec3::cross(const Vec3& other) const {
    XMVECTOR v1 = toXM();
    XMVECTOR v2 = other.toXM();
    XMVECTOR result = XMVector3Cross(v1, v2);
    return Vec3(result);
}

float Vec3::length() const {
    XMVECTOR v = toXM();
    XMVECTOR result = XMVector3Length(v);
    return XMVectorGetX(result);
}

Vec3 Vec3::normalize() const {
    XMVECTOR v = toXM();
    XMVECTOR result = XMVector3Normalize(v);
    return Vec3(result);
}

Vec3 Vec3::max(const Vec3& other) const {
    return Vec3(XMVectorMax(toXM(), other.toXM()));
}

Vec3 Vec3::min(const Vec3& other) const {
    return Vec3(XMVectorMin(toXM(), other.toXM()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Geometric Methods
// ─────────────────────────────────────────────────────────────────────────────

float Vec3::distance_to(const Vec3& other) const {
    return (*this - other).length();
}

float Vec3::angle_to(const Vec3& other) const {
    XMVECTOR v1 = toXM();
    XMVECTOR v2 = other.toXM();
    XMVECTOR angle = XMVector3AngleBetweenVectors(v1, v2);
    return XMVectorGetX(angle);
}

float Vec3::signed_angle_to(const Vec3& other, const Vec3& axis) const {
    float unsigned_angle = angle_to(other);
    Vec3 c = cross(other);
    // Sign determined by whether cross product aligns with axis
    float sign_val = c.dot(axis);
    return sign_val < 0.0f ? -unsigned_angle : unsigned_angle;
}

Vec3 Vec3::rotated(const Vec3& axis, float angle) const {
    XMVECTOR ax = axis.toXM();
    XMMATRIX rot = XMMatrixRotationAxis(ax, angle);
    XMVECTOR v = toXM();
    XMVECTOR result = XMVector3TransformNormal(v, rot);
    return Vec3(result);
}

Vec3 Vec3::slerp(const Vec3& other, float t) const {
    float len_from = length();
    float len_to = other.length();

    // Degenerate: one or both vectors are zero
    if (len_from < Math::EPSILON || len_to < Math::EPSILON) {
        return Lerp(*this, other, t);
    }

    Vec3 from_n = *this / len_from;
    Vec3 to_n = other / len_to;
    float cos_angle = Math::clamp(from_n.dot(to_n), -1.0f, 1.0f);

    // Nearly parallel — fall back to lerp to avoid division by zero in sin
    if (std::abs(cos_angle) > 1.0f - Math::EPSILON) {
        return Lerp(*this, other, t);
    }

    float angle = std::acos(cos_angle);
    float sin_angle = std::sin(angle);
    float scale_from = std::sin((1.0f - t) * angle) / sin_angle;
    float scale_to = std::sin(t * angle) / sin_angle;

    // Interpolate length too
    float result_len = Math::lerp(len_from, len_to, t);
    return (from_n * scale_from + to_n * scale_to) * result_len;
}

Vec3 Vec3::cubic_interpolate(const Vec3& b, const Vec3& pre_a, const Vec3& post_b, float t) const {
    return {
        Math::cubic_interpolate(pre_a.x, x, b.x, post_b.x, t),
        Math::cubic_interpolate(pre_a.y, y, b.y, post_b.y, t),
        Math::cubic_interpolate(pre_a.z, z, b.z, post_b.z, t)
    };
}

Vec3 Vec3::move_toward(const Vec3& target, float delta) const {
    Vec3 diff = target - *this;
    float dist_sq = diff.lengthSq();
    if (dist_sq <= delta * delta || dist_sq < Math::EPSILON) {
        return target;
    }
    return *this + diff / std::sqrt(dist_sq) * delta;
}

Vec3 Vec3::limit_length(float max_len) const {
    float len_sq = lengthSq();
    if (len_sq > max_len * max_len && len_sq > 0.0f) {
        return *this * (max_len / std::sqrt(len_sq));
    }
    return *this;
}

Vec3 Vec3::get_any_perpendicular() const {
    // Choose the axis least aligned with this vector to avoid near-zero cross products
    Vec3 abs_v = abs();
    Vec3 other = (abs_v.x < abs_v.y)
        ? ((abs_v.x < abs_v.z) ? Vec3::UnitX() : Vec3::UnitZ())
        : ((abs_v.y < abs_v.z) ? Vec3::UnitY() : Vec3::UnitZ());
    return cross(other).normalize();
}

Vec2 Vec3::octahedron_encode() const {
    // Map to octahedron: project onto L1 unit sphere
    float l1_norm = std::abs(x) + std::abs(y) + std::abs(z);
    float ox = x / l1_norm;
    float oy = y / l1_norm;

    // Wrap lower hemisphere
    if (z < 0.0f) {
        float new_ox = (1.0f - std::abs(oy)) * (ox >= 0.0f ? 1.0f : -1.0f);
        float new_oy = (1.0f - std::abs(ox)) * (oy >= 0.0f ? 1.0f : -1.0f);
        ox = new_ox;
        oy = new_oy;
    }

    // Remap from [-1,1] to [0,1]
    return {ox * 0.5f + 0.5f, oy * 0.5f + 0.5f};
}

Vec3 Vec3::octahedron_decode(const Vec2& oct) {
    // Remap from [0,1] to [-1,1]
    float ox = oct.x * 2.0f - 1.0f;
    float oy = oct.y * 2.0f - 1.0f;

    float oz = 1.0f - std::abs(ox) - std::abs(oy);

    // Unwrap lower hemisphere
    if (oz < 0.0f) {
        float new_ox = (1.0f - std::abs(oy)) * (ox >= 0.0f ? 1.0f : -1.0f);
        float new_oy = (1.0f - std::abs(ox)) * (oy >= 0.0f ? 1.0f : -1.0f);
        ox = new_ox;
        oy = new_oy;
    }

    return Vec3(ox, oy, oz).normalize();
}

} // namespace uz
