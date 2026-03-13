/// Vec4 implementation — SIMD-accelerated via DirectXMath.

#include "vector4.h"

namespace uz {

float Vec4::dot(const Vec4& other) const {
    XMVECTOR v1 = toXM();
    XMVECTOR v2 = other.toXM();
    XMVECTOR result = XMVector4Dot(v1, v2);
    return XMVectorGetX(result);
}

float Vec4::length() const {
    XMVECTOR v = toXM();
    XMVECTOR result = XMVector4Length(v);
    return XMVectorGetX(result);
}

Vec4 Vec4::normalize() const {
    XMVECTOR v = toXM();
    XMVECTOR result = XMVector4Normalize(v);
    return Vec4(result);
}

} // namespace uz
