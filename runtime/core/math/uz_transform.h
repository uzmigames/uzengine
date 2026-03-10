#pragma once

/// UzEngine Transform — Position + Rotation + Scale (TRS).

#include "uz_quat.h"

namespace uz {

struct Transform {
    Vec3 position;
    Quat rotation;
    Vec3 scale;

    Transform()
        : position(Vec3::Zero())
        , rotation(Quat::Identity())
        , scale(Vec3::One()) {}

    Transform(Vec3 pos, Quat rot, Vec3 scl)
        : position(pos), rotation(rot), scale(scl) {}

    Mat4 ToMatrix() const {
        Mat4 s = Mat4::Scaling(scale);
        Mat4 r = rotation.ToMatrix();
        Mat4 t = Mat4::Translation(position);
        return s * r * t; // Scale → Rotate → Translate (column-major LH)
    }

    Vec3 TransformPoint(Vec3 p) const {
        Vec3 scaled = p * scale;
        Vec3 rotated = rotation.RotateVector(scaled);
        return rotated + position;
    }

    Vec3 TransformDirection(Vec3 d) const {
        return rotation.RotateVector(d);
    }

    Transform Inverse() const {
        Quat invRot = rotation.Inverse();
        Vec3 invScale(1.0f / scale.X(), 1.0f / scale.Y(), 1.0f / scale.Z());
        Vec3 invPos = invRot.RotateVector(-position * invScale);
        return Transform(invPos, invRot, invScale);
    }

    static Transform Identity() { return Transform(); }

    static Transform Lerp(const Transform& a, const Transform& b, float t) {
        return Transform(
            Vec3::Lerp(a.position, b.position, t),
            Quat::Slerp(a.rotation, b.rotation, t),
            Vec3::Lerp(a.scale, b.scale, t)
        );
    }
};

} // namespace uz
