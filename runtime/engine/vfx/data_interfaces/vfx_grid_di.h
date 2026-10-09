#pragma once

/// Generic Grid2D / Grid3D data interface for VFX simulation shaders.
///
/// Enables cellular-automata effects, density fields, heat maps, and
/// simple fluid simulation primitives. Niagara exposes this through
/// `UNiagaraDataInterfaceGrid2DCollection` and `Grid3DCollection`
/// respectively; UzEngine's CPU-side mirror lives here.
///
/// # Design
///
/// A grid is a dense rectangular (Grid2D) or volumetric (Grid3D) array
/// of `Vec4` cells. The GPU upload (deferred to the binding follow-up)
/// stores each grid as a bindless `RWTexture2D<float4>` /
/// `RWTexture3D<float4>`; the CPU API shipped here is the golden
/// reference + a sandbox for CPU-preview tooling.
///
/// # Addressing mode
///
/// `VfxGridAddressing::Clamp` (default) clamps out-of-range coordinates
/// to the edge cell. `Wrap` tiles the grid. The two HLSL sampler stubs
/// we eventually ship pick the matching sampler state at bind time.
///
/// # Example
///
/// ```cpp
/// VfxGrid2D heat{32, 32};
/// heat.Write({5, 5}, Vec4{1.0f, 0.0f, 0.0f, 1.0f});
/// const Vec4 smooth = heat.SampleBilinear({5.5f, 5.5f});
/// ```
///
/// @ref UE5 UNiagaraDataInterfaceGrid2DCollection + Grid3DCollection

#include "vector2.h"
#include "vector3.h"
#include "vector4.h"

#include <cstdint>
#include <vector>

namespace uz {

/// Boundary handling for out-of-range grid reads / writes.
enum class VfxGridAddressing : uint8_t {
    Clamp = 0,   ///< Out-of-range coords clamp to the edge cell.
    Wrap  = 1,   ///< Out-of-range coords wrap modulo grid size.
};

// ─────────────────────────────────────────────────────────────────────────────
// VfxGrid2D — dense Vec4-per-cell 2D grid.
// ─────────────────────────────────────────────────────────────────────────────

struct VfxGrid2D {
    uint32_t width  = 0;
    uint32_t height = 0;
    VfxGridAddressing addressing = VfxGridAddressing::Clamp;
    std::vector<Vec4> cells;

    VfxGrid2D() = default;
    VfxGrid2D(uint32_t w, uint32_t h) { Resize(w, h); }

    /// Allocates or reallocates the backing storage. All cells are zeroed.
    void Resize(uint32_t newWidth, uint32_t newHeight);

    /// Clears every cell to the supplied value.
    void Clear(const Vec4& value = {}) noexcept;

    /// Read / write at integer cell coordinates. Out-of-range coords
    /// obey `addressing`.
    [[nodiscard]] Vec4 Read(int32_t x, int32_t y) const noexcept;
    void Write(int32_t x, int32_t y, const Vec4& value) noexcept;

    /// Bilinear sample at continuous coords in `[0, width] x [0, height]`.
    /// Addressing drives how out-of-range inputs are folded.
    [[nodiscard]] Vec4 SampleBilinear(const Vec2& coord) const noexcept;

    /// Total cell count (width * height). Zero when the grid is unsized.
    [[nodiscard]] uint32_t GetCellCount() const noexcept { return width * height; }
};

// ─────────────────────────────────────────────────────────────────────────────
// VfxGrid3D — dense Vec4-per-cell 3D grid.
// ─────────────────────────────────────────────────────────────────────────────

struct VfxGrid3D {
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t depth  = 0;
    VfxGridAddressing addressing = VfxGridAddressing::Clamp;
    std::vector<Vec4> cells;

    VfxGrid3D() = default;
    VfxGrid3D(uint32_t w, uint32_t h, uint32_t d) { Resize(w, h, d); }

    void Resize(uint32_t newWidth, uint32_t newHeight, uint32_t newDepth);
    void Clear(const Vec4& value = {}) noexcept;

    [[nodiscard]] Vec4 Read(int32_t x, int32_t y, int32_t z) const noexcept;
    void Write(int32_t x, int32_t y, int32_t z, const Vec4& value) noexcept;

    /// Trilinear sample — equivalent to the `SampleLevel(linearClamp, uvw, 0)`
    /// HLSL call on a `Texture3D<float4>` bound with matching addressing.
    [[nodiscard]] Vec4 SampleTrilinear(const Vec3& coord) const noexcept;

    [[nodiscard]] uint32_t GetCellCount() const noexcept {
        return width * height * depth;
    }
};

} // namespace uz
