/// DecalAtlas -- shelf-packing implementation for decal texture atlas.

#include "decal_atlas.h"

#include "engine/assertions/engine_assert.h"
#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Construction / Move
// ---------------------------------------------------------------------------

DecalAtlas::DecalAtlas(uint32_t atlasSize) noexcept
    : atlasSize_(atlasSize)
{
    UZ_CHECKF(atlasSize > 0, "DecalAtlas: atlasSize must be > 0");
    // Reserve a reasonable number of shelves to avoid early reallocations.
    // A 2048-atlas with 64px average shelf height needs ~32 shelves.
    shelves_.reserve(32);
}

DecalAtlas::DecalAtlas(DecalAtlas&& other) noexcept
    : shelves_(std::move(other.shelves_))
    , atlasSize_(other.atlasSize_)
    , nextShelfY_(other.nextShelfY_)
    , usedPixels_(other.usedPixels_)
    , entryCount_(other.entryCount_)
{
    other.nextShelfY_ = 0;
    other.usedPixels_ = 0;
    other.entryCount_ = 0;
}

DecalAtlas& DecalAtlas::operator=(DecalAtlas&& other) noexcept
{
    if (this != &other) {
        shelves_ = std::move(other.shelves_);
        atlasSize_ = other.atlasSize_;
        nextShelfY_ = other.nextShelfY_;
        usedPixels_ = other.usedPixels_;
        entryCount_ = other.entryCount_;

        other.nextShelfY_ = 0;
        other.usedPixels_ = 0;
        other.entryCount_ = 0;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// AddEntry -- shelf-packing allocation
// ---------------------------------------------------------------------------

std::optional<AtlasEntry> DecalAtlas::AddEntry(uint32_t width, uint32_t height) noexcept
{
    // Reject zero-size or oversized entries immediately.
    if (width == 0 || height == 0 || width > atlasSize_ || height > atlasSize_) {
        UZ_LOG(VFX, Warning,
               "DecalAtlas::AddEntry: invalid dimensions {}x{} for atlas size {}",
               width, height, atlasSize_);
        return std::nullopt;
    }

    // Step 1: Try to fit on an existing shelf.
    const int shelfIdx = findFittingShelf(width, height);
    if (shelfIdx >= 0) {
        auto& shelf = shelves_[static_cast<size_t>(shelfIdx)];
        const auto entry = makeEntry(shelf.xCursor, shelf.y, width, height);
        shelf.xCursor += width;
        usedPixels_ += width * height;
        ++entryCount_;
        return entry;
    }

    // Step 2: Open a new shelf if vertical space remains.
    if (nextShelfY_ + height <= atlasSize_) {
        shelves_.push_back(Shelf{nextShelfY_, height, width});
        const auto entry = makeEntry(0, nextShelfY_, width, height);
        nextShelfY_ += height;
        usedPixels_ += width * height;
        ++entryCount_;
        return entry;
    }

    // No space left.
    UZ_LOG(VFX, Warning,
           "DecalAtlas::AddEntry: no space for {}x{} (occupancy={:.1f}%, entries={})",
           width, height, GetOccupancy() * 100.0f, entryCount_);
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void DecalAtlas::Reset() noexcept
{
    shelves_.clear();
    nextShelfY_ = 0;
    usedPixels_ = 0;
    entryCount_ = 0;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

float DecalAtlas::GetOccupancy() const noexcept
{
    const uint64_t totalPixels = static_cast<uint64_t>(atlasSize_) * atlasSize_;
    if (totalPixels == 0) return 0.0f;
    return static_cast<float>(usedPixels_) / static_cast<float>(totalPixels);
}

bool DecalAtlas::HasSpace(uint32_t width, uint32_t height) const noexcept
{
    if (width == 0 || height == 0 || width > atlasSize_ || height > atlasSize_) {
        return false;
    }

    // Check existing shelves.
    if (findFittingShelf(width, height) >= 0) {
        return true;
    }

    // Check if a new shelf would fit.
    return (nextShelfY_ + height <= atlasSize_);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

AtlasEntry DecalAtlas::makeEntry(uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height) const noexcept
{
    const float invSize = 1.0f / static_cast<float>(atlasSize_);
    return AtlasEntry{
        Vec2{static_cast<float>(x) * invSize, static_cast<float>(y) * invSize},
        Vec2{static_cast<float>(x + width) * invSize, static_cast<float>(y + height) * invSize},
        width,
        height
    };
}

int DecalAtlas::findFittingShelf(uint32_t width, uint32_t height) const noexcept
{
    for (size_t i = 0; i < shelves_.size(); ++i) {
        const auto& shelf = shelves_[i];
        // Entry must fit within shelf height and remaining width.
        if (height <= shelf.height && shelf.xCursor + width <= atlasSize_) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace uz
