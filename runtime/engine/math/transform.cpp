/// Transform implementation — TRS decomposition and composition.

#include "transform.h"

namespace uz {

/// Constructs a `Transform` by decomposing a 4x4 matrix into TRS components.
///
/// Delegates to `XMMatrixDecompose`, which extracts the scale vector,
/// rotation quaternion, and translation vector via QR-like decomposition.
/// The result is exact for pure TRS matrices.  Matrices that contain shear
/// or perspective cannot be exactly represented as a `Transform` — the
/// decomposition will approximate them.
///
/// # Arguments
/// - `m`: the matrix to decompose, typically a world or local-to-parent matrix.
Transform::Transform(const Mat4& m) {
    // Decompose TRS from matrix
    XMVECTOR scale_v;
    XMVECTOR rotation_v;
    XMVECTOR translation_v;

    XMMATRIX xm = m.toXM();
    XMMatrixDecompose(&scale_v, &rotation_v, &translation_v, xm);

    position = Vec3(translation_v);
    rotation = Quat(rotation_v);
    scale = Vec3(scale_v);
}

Mat4 Transform::toMatrix() const {
    Mat4 result = Mat4::Identity();
    result = result * Mat4::Scaling(scale);
    result = result * rotation.toMatrix();
    result = result * Mat4::Translation(position);
    return result;
}

Transform Transform::FromMatrix(const Mat4& m) {
    return Transform(m);
}

/// Computes the local-to-world transform by composing this (child) transform
/// with a parent transform.
///
/// The composition follows standard TRS hierarchy rules:
///   - `world.scale    = child.scale * parent.scale`       (per-axis multiply)
///   - `world.rotation = child.rotation * parent.rotation` (quaternion concatenation)
///   - `world.position = child.rotation * (parent.scale * child.position) + parent.position`
///
/// The position is first scaled by the parent's scale, then rotated by the
/// child's orientation, then offset by the parent's world position.  This
/// matches the behaviour of standard scene-graph composition (SRT order).
///
/// # Arguments
/// - `parent`: the parent world-space transform.
///
/// # Returns
/// The world-space transform of this node.
Transform Transform::compose(const Transform& parent) const {
    // Local to world: world = local * parent
    Transform world;
    world.scale = scale * parent.scale;
    world.rotation = rotation * parent.rotation;
    world.position = (rotation * (parent.scale * position)) + parent.position;
    return world;
}

/// Computes the analytical inverse of this TRS transform.
///
/// For a transform T with components (P, R, S), the inverse is:
///   - `inv.scale    = 1 / S`            (per-axis reciprocal)
///   - `inv.rotation = R^{-1}`           (conjugate for unit quaternions)
///   - `inv.position = -(R^{-1} * (S^{-1} * P))`
///
/// This is exact (no floating-point decomposition) and cheaper than
/// inverting the equivalent 4x4 matrix.  Assumes a well-formed TRS with
/// non-zero scale on all axes.
///
/// # Returns
/// A `Transform` T_inv such that `T.compose(T_inv)` yields identity.
Transform Transform::inverse() const {
    Transform inv;
    inv.scale = {1 / scale.x, 1 / scale.y, 1 / scale.z};
    inv.rotation = rotation.inverse();
    inv.position = -(inv.rotation * (inv.scale * position));
    return inv;
}

Transform Transform::translate(const Vec3& delta) const {
    return {position + delta, rotation, scale};
}

Transform Transform::rotate(const Quat& rot) const {
    return {position, rotation * rot, scale};
}

Transform Transform::scaleBy(const Vec3& s) const {
    return {position, rotation, scale * s};
}

} // namespace uz
