#include "vfx_grid_di.h"

#include <algorithm>
#include <cmath>

namespace uz {

namespace {

/// Clamp/wrap an integer coordinate according to the grid's addressing mode.
int32_t FoldIndex(int32_t index, int32_t extent, VfxGridAddressing mode) noexcept {
    if (extent <= 0) return 0;
    if (mode == VfxGridAddressing::Clamp) {
        if (index < 0)        return 0;
        if (index >= extent)  return extent - 1;
        return index;
    }
    // Wrap (modulo).
    int32_t r = index % extent;
    if (r < 0) r += extent;
    return r;
}

/// Clamp/wrap a floating-point coordinate for bilinear / trilinear sampling.
float FoldCoord(float coord, int32_t extent, VfxGridAddressing mode) noexcept {
    if (extent <= 0) return 0.0f;
    if (mode == VfxGridAddressing::Clamp) {
        if (coord < 0.0f)               return 0.0f;
        if (coord >= static_cast<float>(extent)) {
            return static_cast<float>(extent) - 1.0f;
        }
        return coord;
    }
    // Wrap: bring into [0, extent) before sampling.
    const float fextent = static_cast<float>(extent);
    float folded = std::fmod(coord, fextent);
    if (folded < 0.0f) folded += fextent;
    return folded;
}

Vec4 VecScale4(const Vec4& v, float s) noexcept {
    return Vec4{v.x * s, v.y * s, v.z * s, v.w * s};
}
Vec4 VecAdd4(const Vec4& a, const Vec4& b) noexcept {
    return Vec4{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// VfxGrid2D
// ─────────────────────────────────────────────────────────────────────────────

void VfxGrid2D::Resize(uint32_t newWidth, uint32_t newHeight) {
    width  = newWidth;
    height = newHeight;
    cells.assign(static_cast<size_t>(width) * height, Vec4{});
}

void VfxGrid2D::Clear(const Vec4& value) noexcept {
    std::fill(cells.begin(), cells.end(), value);
}

Vec4 VfxGrid2D::Read(int32_t x, int32_t y) const noexcept {
    if (width == 0 || height == 0) return Vec4{};
    const int32_t fx = FoldIndex(x, static_cast<int32_t>(width),  addressing);
    const int32_t fy = FoldIndex(y, static_cast<int32_t>(height), addressing);
    return cells[static_cast<size_t>(fy) * width + fx];
}

void VfxGrid2D::Write(int32_t x, int32_t y, const Vec4& value) noexcept {
    if (width == 0 || height == 0) return;
    const int32_t fx = FoldIndex(x, static_cast<int32_t>(width),  addressing);
    const int32_t fy = FoldIndex(y, static_cast<int32_t>(height), addressing);
    cells[static_cast<size_t>(fy) * width + fx] = value;
}

Vec4 VfxGrid2D::SampleBilinear(const Vec2& coord) const noexcept {
    if (width == 0 || height == 0) return Vec4{};

    // Continuous coords are in cell space ([0..width] x [0..height]) so we
    // subtract 0.5 to land on cell centres (HLSL's Texture2D sample
    // convention). Fold according to addressing mode first.
    const float cx = FoldCoord(coord.x, static_cast<int32_t>(width),  addressing) - 0.5f;
    const float cy = FoldCoord(coord.y, static_cast<int32_t>(height), addressing) - 0.5f;

    const int32_t x0 = static_cast<int32_t>(std::floor(cx));
    const int32_t y0 = static_cast<int32_t>(std::floor(cy));
    const float fx = cx - static_cast<float>(x0);
    const float fy = cy - static_cast<float>(y0);

    const Vec4 c00 = Read(x0,     y0);
    const Vec4 c10 = Read(x0 + 1, y0);
    const Vec4 c01 = Read(x0,     y0 + 1);
    const Vec4 c11 = Read(x0 + 1, y0 + 1);

    const Vec4 cx0 = VecAdd4(VecScale4(c00, 1.0f - fx), VecScale4(c10, fx));
    const Vec4 cx1 = VecAdd4(VecScale4(c01, 1.0f - fx), VecScale4(c11, fx));
    return VecAdd4(VecScale4(cx0, 1.0f - fy), VecScale4(cx1, fy));
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxGrid3D
// ─────────────────────────────────────────────────────────────────────────────

void VfxGrid3D::Resize(uint32_t newWidth, uint32_t newHeight, uint32_t newDepth) {
    width  = newWidth;
    height = newHeight;
    depth  = newDepth;
    cells.assign(static_cast<size_t>(width) * height * depth, Vec4{});
}

void VfxGrid3D::Clear(const Vec4& value) noexcept {
    std::fill(cells.begin(), cells.end(), value);
}

Vec4 VfxGrid3D::Read(int32_t x, int32_t y, int32_t z) const noexcept {
    if (width == 0 || height == 0 || depth == 0) return Vec4{};
    const int32_t fx = FoldIndex(x, static_cast<int32_t>(width),  addressing);
    const int32_t fy = FoldIndex(y, static_cast<int32_t>(height), addressing);
    const int32_t fz = FoldIndex(z, static_cast<int32_t>(depth),  addressing);
    const size_t index = (static_cast<size_t>(fz) * height + fy) * width + fx;
    return cells[index];
}

void VfxGrid3D::Write(int32_t x, int32_t y, int32_t z, const Vec4& value) noexcept {
    if (width == 0 || height == 0 || depth == 0) return;
    const int32_t fx = FoldIndex(x, static_cast<int32_t>(width),  addressing);
    const int32_t fy = FoldIndex(y, static_cast<int32_t>(height), addressing);
    const int32_t fz = FoldIndex(z, static_cast<int32_t>(depth),  addressing);
    const size_t index = (static_cast<size_t>(fz) * height + fy) * width + fx;
    cells[index] = value;
}

Vec4 VfxGrid3D::SampleTrilinear(const Vec3& coord) const noexcept {
    if (width == 0 || height == 0 || depth == 0) return Vec4{};

    const float cx = FoldCoord(coord.x, static_cast<int32_t>(width),  addressing) - 0.5f;
    const float cy = FoldCoord(coord.y, static_cast<int32_t>(height), addressing) - 0.5f;
    const float cz = FoldCoord(coord.z, static_cast<int32_t>(depth),  addressing) - 0.5f;

    const int32_t x0 = static_cast<int32_t>(std::floor(cx));
    const int32_t y0 = static_cast<int32_t>(std::floor(cy));
    const int32_t z0 = static_cast<int32_t>(std::floor(cz));
    const float fx = cx - static_cast<float>(x0);
    const float fy = cy - static_cast<float>(y0);
    const float fz = cz - static_cast<float>(z0);

    const Vec4 c000 = Read(x0,     y0,     z0);
    const Vec4 c100 = Read(x0 + 1, y0,     z0);
    const Vec4 c010 = Read(x0,     y0 + 1, z0);
    const Vec4 c110 = Read(x0 + 1, y0 + 1, z0);
    const Vec4 c001 = Read(x0,     y0,     z0 + 1);
    const Vec4 c101 = Read(x0 + 1, y0,     z0 + 1);
    const Vec4 c011 = Read(x0,     y0 + 1, z0 + 1);
    const Vec4 c111 = Read(x0 + 1, y0 + 1, z0 + 1);

    // Bilinear on the z0 slice.
    const Vec4 cx00 = VecAdd4(VecScale4(c000, 1.0f - fx), VecScale4(c100, fx));
    const Vec4 cx10 = VecAdd4(VecScale4(c010, 1.0f - fx), VecScale4(c110, fx));
    const Vec4 cy0  = VecAdd4(VecScale4(cx00, 1.0f - fy), VecScale4(cx10, fy));

    // Bilinear on the z1 slice.
    const Vec4 cx01 = VecAdd4(VecScale4(c001, 1.0f - fx), VecScale4(c101, fx));
    const Vec4 cx11 = VecAdd4(VecScale4(c011, 1.0f - fx), VecScale4(c111, fx));
    const Vec4 cy1  = VecAdd4(VecScale4(cx01, 1.0f - fy), VecScale4(cx11, fy));

    // Linear blend between the two slices.
    return VecAdd4(VecScale4(cy0, 1.0f - fz), VecScale4(cy1, fz));
}

} // namespace uz
