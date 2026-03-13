/// Vec2 implementation — geometric and utility methods.

#include "vector2.h"

#include <cmath>

namespace uz {

Vec2 Vec2::normalize() const {
    float len = length();
    if (len > 0) {
        return {x / len, y / len};
    }
    return *this;
}

float Vec2::distance_to(const Vec2& other) const {
    return (*this - other).length();
}

float Vec2::angle_to(const Vec2& other) const {
    return std::atan2(cross(other), dot(other));
}

float Vec2::angle() const {
    return std::atan2(y, x);
}

Vec2 Vec2::rotated(float angle) const {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return {x * c - y * s, x * s + y * c};
}

Vec2 Vec2::slerp(const Vec2& other, float t) const {
    float len_from = length();
    float len_to = other.length();

    // Degenerate cases — fall back to lerp.
    if (len_from < Math::EPSILON || len_to < Math::EPSILON) {
        return Vec2::Lerp(*this, other, t);
    }

    Vec2 unit_from = *this / len_from;
    Vec2 unit_to = other / len_to;

    float d = Math::clamp(unit_from.dot(unit_to), -1.0f, 1.0f);
    float theta = std::acos(d) * t;

    // Interpolate length linearly.
    float interp_len = Math::lerp(len_from, len_to, t);

    // If vectors are nearly parallel, use lerp to avoid division by zero.
    if (std::abs(d) > 1.0f - Math::EPSILON) {
        return Vec2::Lerp(*this, other, t);
    }

    Vec2 perpendicular = (unit_to - unit_from * d).normalize();
    return (unit_from * std::cos(theta) + perpendicular * std::sin(theta)) * interp_len;
}

Vec2 Vec2::cubic_interpolate(const Vec2& b, const Vec2& pre_a, const Vec2& post_b, float t) const {
    return {
        Math::cubic_interpolate(pre_a.x, x, b.x, post_b.x, t),
        Math::cubic_interpolate(pre_a.y, y, b.y, post_b.y, t)
    };
}

Vec2 Vec2::slide(const Vec2& normal) const {
    return *this - normal * dot(normal);
}

Vec2 Vec2::bounce(const Vec2& normal) const {
    return -reflect(normal);
}

Vec2 Vec2::reflect(const Vec2& normal) const {
    return 2.0f * normal * dot(normal) - *this;
}

Vec2 Vec2::move_toward(const Vec2& target, float delta) const {
    Vec2 diff = target - *this;
    float dist = diff.length();
    if (dist <= delta || dist < Math::EPSILON) {
        return target;
    }
    return *this + diff / dist * delta;
}

Vec2 Vec2::direction_to(const Vec2& other) const {
    return (other - *this).normalize();
}

Vec2 Vec2::limit_length(float max_len) const {
    float len_sq = lengthSq();
    if (len_sq > max_len * max_len && len_sq > 0.0f) {
        float len = std::sqrt(len_sq);
        return *this * (max_len / len);
    }
    return *this;
}

} // namespace uz
