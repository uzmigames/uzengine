/// DecalAtlas -- shelf-packed texture atlas for decal sub-textures.
///
/// Manages UV allocation for up to three independent atlases (albedo, normal,
/// roughness). The atlas itself is purely CPU-side geometry: it tracks which
/// rectangular regions are taken and returns UV coordinates. It does NOT hold
/// any pixel data or GPU resources.
///
/// # Algorithm
/// Uses shelf (next-fit decreasing height) packing. Shelves are horizontal
/// rows that grow downward. Each new entry is placed on the first shelf whose
/// remaining width and height can accommodate it; otherwise a new shelf is
/// opened at the bottom.
///
/// # Thread Safety
/// NOT thread-safe. Callers must synchronize externally if accessed from
/// multiple threads (typical usage is single-threaded asset loading).
///
/// # Example
/// ```cpp
/// DecalAtlas atlas(2048);
/// auto entry = atlas.AddEntry(256, 256);
/// if (entry) {
///     // entry->uvMin, entry->uvMax are ready for the GPU
/// }
/// float occupancy = atlas.GetOccupancy();  // 0.0156...
/// ```

#pragma once

#include "engine/math/vector2.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace uz {

// ---------------------------------------------------------------------------
// AtlasEntry -- UV rectangle returned by a successful allocation
// ---------------------------------------------------------------------------

/// Describes the UV-space rectangle of a single decal texture within the atlas.
///
/// UVs are in [0, 1] range, suitable for direct use in shader sampling.
/// The original pixel dimensions are preserved for display or LOD decisions.
struct AtlasEntry {
    /// Top-left UV coordinate in the atlas (inclusive).
    Vec2 uvMin;

    /// Bottom-right UV coordinate in the atlas (exclusive).
    Vec2 uvMax;

    /// Original texture width in pixels.
    uint32_t width = 0;

    /// Original texture height in pixels.
    uint32_t height = 0;
};

// ---------------------------------------------------------------------------
// DecalAtlas -- shelf-packed texture atlas (CPU-side geometry only)
// ---------------------------------------------------------------------------

/// Builds a square texture atlas from individual decal textures using
/// shelf-packing. Supports independent instances for albedo, normal, and
/// roughness channels.
///
/// The atlas is purely a bookkeeping structure -- it tracks allocated
/// rectangles and returns UV coordinates but does not manage pixel data
/// or GPU textures.
///
/// # Parameters
/// - `atlasSize`: Side length of the square atlas in pixels. Defaults to 2048.
///   Must be > 0. Typical values: 1024, 2048, 4096.
///
/// # Constraints
/// - Non-copyable (stateful allocator -- copying would duplicate bookkeeping).
/// - Movable (ownership transfer is safe).
class DecalAtlas {
public:
    /// Constructs an empty atlas with the given side length.
    ///
    /// # Parameters
    /// - `atlasSize`: Square atlas dimension in pixels. Must be > 0.
    explicit DecalAtlas(uint32_t atlasSize = 2048) noexcept;

    ~DecalAtlas() = default;

    // -- Non-copyable, movable ------------------------------------------------
    DecalAtlas(const DecalAtlas&) = delete;
    DecalAtlas& operator=(const DecalAtlas&) = delete;
    DecalAtlas(DecalAtlas&& other) noexcept;
    DecalAtlas& operator=(DecalAtlas&& other) noexcept;

    /// Allocates space for a texture of the given dimensions.
    ///
    /// Tries to fit the entry on an existing shelf first (first-fit).
    /// If no shelf has enough room, opens a new shelf at the bottom.
    /// If the atlas is full, returns `std::nullopt`.
    ///
    /// # Parameters
    /// - `width`: Texture width in pixels. Must be > 0 and <= atlasSize.
    /// - `height`: Texture height in pixels. Must be > 0 and <= atlasSize.
    ///
    /// # Returns
    /// - `AtlasEntry` with UV coordinates on success.
    /// - `std::nullopt` if the entry cannot fit (atlas full or dimensions
    ///   exceed atlas size).
    ///
    /// # Side Effects
    /// Logs `VFX::Warning` when allocation fails due to space exhaustion.
    [[nodiscard]] std::optional<AtlasEntry> AddEntry(uint32_t width, uint32_t height) noexcept;

    /// Clears all entries and shelves, resetting the atlas to empty state.
    ///
    /// Does not change the atlas size. After reset, the atlas has zero
    /// entries and zero used area.
    void Reset() noexcept;

    /// Returns the atlas dimension (side length) in pixels.
    [[nodiscard]] uint32_t GetAtlasSize() const noexcept { return atlasSize_; }

    /// Returns the total number of entries successfully allocated.
    [[nodiscard]] uint32_t GetEntryCount() const noexcept { return entryCount_; }

    /// Returns the total number of pixels allocated (sum of w*h for all entries).
    ///
    /// This counts the actual entry pixels, not the shelf waste. Useful for
    /// tracking how much texture data is packed.
    [[nodiscard]] uint32_t GetUsedArea() const noexcept { return usedPixels_; }

    /// Returns the occupancy ratio: usedArea / (atlasSize * atlasSize).
    ///
    /// # Returns
    /// Value in [0.0, 1.0]. Returns 0.0 if atlasSize is 0.
    [[nodiscard]] float GetOccupancy() const noexcept;

    /// Checks whether an entry of the given size could fit without allocating.
    ///
    /// Performs the same search as AddEntry() but does not modify state.
    /// Useful for pre-flight checks before committing to a decal texture load.
    ///
    /// # Parameters
    /// - `width`: Texture width in pixels.
    /// - `height`: Texture height in pixels.
    ///
    /// # Returns
    /// `true` if AddEntry(width, height) would succeed right now.
    [[nodiscard]] bool HasSpace(uint32_t width, uint32_t height) const noexcept;

private:
    /// A horizontal shelf (row) in the atlas.
    struct Shelf {
        uint32_t y;       ///< Y position of this shelf's top edge.
        uint32_t height;  ///< Height of the tallest entry placed on this shelf.
        uint32_t xCursor; ///< Next available X position (pixels from left).
    };

    /// Converts pixel coordinates to UV space [0, 1].
    [[nodiscard]] AtlasEntry makeEntry(uint32_t x, uint32_t y,
                                       uint32_t width, uint32_t height) const noexcept;

    /// Tries to find an existing shelf that can fit the entry.
    ///
    /// # Returns
    /// Index into shelves_, or -1 if no shelf fits.
    [[nodiscard]] int findFittingShelf(uint32_t width, uint32_t height) const noexcept;

    std::vector<Shelf> shelves_;
    uint32_t atlasSize_;
    uint32_t nextShelfY_ = 0;
    uint32_t usedPixels_ = 0;
    uint32_t entryCount_ = 0;
};

}  // namespace uz
