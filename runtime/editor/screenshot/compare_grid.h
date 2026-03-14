/// UzEngine Compare Grid — CPU Image Compositing for Visual Comparison
///
/// Composes multiple screenshots into a single labeled grid image for visual
/// regression testing. Instead of sending N separate images to an LLM or human
/// reviewer, compose them into one annotated grid with metadata labels.
///
/// 100% CPU-side — no GPU resources required. Uses stb_image for PNG loading
/// and stb_image_write for PNG output.
///
/// # Usage
/// ```cpp
/// uz::CompareGrid grid;
/// grid.SetCellSize(384, 384);
/// grid.AddCellFromPNG("dx12_sm66.png", "DX12 SM6.6", "Frame 42");
/// grid.AddCellFromPNG("dx11_sm50.png", "DX11 SM5.0", "Frame 42");
/// grid.SaveToPNG("compare_grid.png");
/// ```
///
/// # Layout
/// Cells are arranged in a roughly square grid (cols = ceil(sqrt(N))).
/// Each cell contains the scaled image with a label bar below it.

#pragma once

#include "log.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

UZ_DECLARE_LOG_CATEGORY(CompareGrid);

/// Metadata for a single cell in the comparison grid.
///
/// The pixel data is NOT owned — caller must keep it alive until after
/// `Compose()` or `SaveToPNG()` completes.
struct GridCellInfo {
    std::string label;               ///< Primary label (e.g., "DX12 SM6.6")
    std::string sublabel;            ///< Secondary label (e.g., "Frame 42")
    const uint8_t* pixels = nullptr; ///< RGBA8 raw pixel data (not owned)
    uint32_t srcWidth = 0;           ///< Source image width in pixels
    uint32_t srcHeight = 0;          ///< Source image height in pixels
};

/// Composes multiple images into a single labeled grid for visual comparison.
///
/// Images are scaled to a uniform cell size using bilinear interpolation,
/// arranged in a grid, and annotated with text labels using a built-in 8x8
/// bitmap font. The output is a single RGBA8 image.
///
/// # Thread Safety
/// NOT thread-safe. Use from a single thread only.
class CompareGrid {
public:
    CompareGrid() = default;
    ~CompareGrid() = default;

    // Non-copyable, movable
    CompareGrid(const CompareGrid&) = delete;
    CompareGrid& operator=(const CompareGrid&) = delete;
    CompareGrid(CompareGrid&&) = default;
    CompareGrid& operator=(CompareGrid&&) = default;

    /// Sets the target cell size for each image in the grid.
    ///
    /// Images larger than this are bilinear-downscaled. Images smaller are
    /// centered on a dark gray background.
    ///
    /// # Arguments
    /// * `w` — Cell width in pixels (default 256).
    /// * `h` — Cell height in pixels (default 256).
    void SetCellSize(uint32_t w, uint32_t h);

    /// Sets the padding between cells in pixels.
    ///
    /// # Arguments
    /// * `px` — Padding in pixels (default 4).
    void SetPadding(uint32_t px);

    /// Sets the height reserved for the label area below each cell.
    ///
    /// The label area displays the primary label and optional sublabel
    /// using the built-in bitmap font.
    ///
    /// # Arguments
    /// * `px` — Label area height in pixels (default 20).
    void SetLabelHeight(uint32_t px);

    /// Forces a specific number of columns in the grid layout.
    ///
    /// By default (0), columns = ceil(sqrt(N)). Set to a non-zero value
    /// to override. Useful for horizontal layouts (e.g., 3 columns for
    /// side-by-side comparison).
    ///
    /// # Arguments
    /// * `cols` — Number of columns (0 = auto).
    void SetColumns(uint32_t cols);

    /// Adds a cell with pre-loaded pixel data.
    ///
    /// The pixel data pointer must remain valid until `Compose()` or
    /// `SaveToPNG()` completes.
    ///
    /// # Arguments
    /// * `info` — Cell metadata including pixel pointer and dimensions.
    void AddCell(const GridCellInfo& info);

    /// Loads a PNG file and adds it as a cell.
    ///
    /// Uses stb_image to decode the PNG. The loaded pixel data is stored
    /// internally and freed when the CompareGrid is destroyed or cleared.
    ///
    /// # Arguments
    /// * `path` — Path to the PNG file.
    /// * `label` — Primary label for this cell.
    /// * `sublabel` — Secondary label for this cell.
    ///
    /// # Returns
    /// true if the PNG was loaded successfully, false on I/O or decode error.
    bool AddCellFromPNG(const std::string& path, const std::string& label, const std::string& sublabel);

    /// Composites all cells into a single RGBA8 image.
    ///
    /// Arranges cells in a grid with cols = ceil(sqrt(N)), applies bilinear
    /// scaling, draws labels, and writes the result to `outPixels`.
    ///
    /// # Arguments
    /// * `outPixels` — Output buffer, resized to fit the composed image.
    /// * `outWidth` — Set to the total image width in pixels.
    /// * `outHeight` — Set to the total image height in pixels.
    ///
    /// # Returns
    /// true on success, false if no cells have been added.
    [[nodiscard]] bool Compose(std::vector<uint8_t>& outPixels, uint32_t& outWidth, uint32_t& outHeight) const;

    /// Composites the grid and saves it directly to a PNG file.
    ///
    /// Convenience method that calls `Compose()` then writes the result.
    ///
    /// # Arguments
    /// * `path` — Output PNG file path.
    ///
    /// # Returns
    /// true if compose and write both succeeded.
    [[nodiscard]] bool SaveToPNG(const std::string& path) const;

    /// Returns the number of cells added so far.
    [[nodiscard]] uint32_t GetCellCount() const { return static_cast<uint32_t>(m_cells.size()); }

private:
    /// Owned pixel data from AddCellFromPNG (stb_image allocations).
    struct OwnedImage {
        uint8_t* data = nullptr;
        ~OwnedImage() { if (data) { free(data); } }
        OwnedImage() = default;
        OwnedImage(uint8_t* d) : data(d) {}
        OwnedImage(const OwnedImage&) = delete;
        OwnedImage& operator=(const OwnedImage&) = delete;
        OwnedImage(OwnedImage&& o) noexcept : data(o.data) { o.data = nullptr; }
        OwnedImage& operator=(OwnedImage&& o) noexcept { if (data) free(data); data = o.data; o.data = nullptr; return *this; }
    };

    std::vector<GridCellInfo> m_cells;
    std::vector<OwnedImage> m_ownedImages;

    uint32_t m_cellWidth = 256;
    uint32_t m_cellHeight = 256;
    uint32_t m_padding = 4;
    uint32_t m_labelHeight = 20;
    uint32_t m_forceColumns = 0; ///< 0 = auto (ceil(sqrt(N))), >0 = forced column count.
};

/// Result of a pixel-by-pixel image diff comparison.
struct ImageDiffResult {
    uint32_t totalPixels = 0;         ///< Total number of pixels compared.
    uint32_t diffPixels = 0;          ///< Number of pixels that differ beyond threshold.
    float diffPercentage = 0.f;       ///< Percentage of differing pixels (0-100).
    float maxChannelDiff = 0.f;       ///< Maximum per-channel difference (0-255).
    float avgChannelDiff = 0.f;       ///< Average per-channel difference across all pixels.
    float psnr = 0.f;                 ///< Peak Signal-to-Noise Ratio in dB (higher = more similar).
    bool identical = true;            ///< True if all pixels are exactly identical.
};

/// Compares two images pixel-by-pixel and generates a visual diff heatmap.
///
/// The diff heatmap uses a color scheme:
/// - **Black** = identical pixels
/// - **Red intensity** = magnitude of difference (brighter = larger diff)
/// - **White** = maximum difference (255 per channel)
///
/// Can also produce a 3-panel comparison grid: [Image A] [Image B] [Diff Heatmap]
/// with labels and diff statistics burned into the image.
///
/// Both input images must be RGBA8 format and the same dimensions.
/// If dimensions differ, the smaller image is padded with magenta (#FF00FF)
/// to highlight the size mismatch.
///
/// # Thread Safety
/// All methods are stateless — safe to call from any thread.
class ImageDiff {
public:
    /// Per-channel threshold below which pixels are considered identical.
    /// Default 2 accounts for minor floating-point rounding in different RHI backends.
    static constexpr uint8_t kDefaultThreshold = 2;

    /// Computes the diff between two RGBA8 images.
    ///
    /// # Arguments
    /// * `pixelsA` — First image RGBA8 data.
    /// * `widthA`, `heightA` — Dimensions of first image.
    /// * `pixelsB` — Second image RGBA8 data.
    /// * `widthB`, `heightB` — Dimensions of second image.
    /// * `threshold` — Per-channel tolerance for considering pixels "identical".
    ///
    /// # Returns
    /// Diff statistics (pixel count, percentage, PSNR, etc.).
    [[nodiscard]] static ImageDiffResult Compare(
        const uint8_t* pixelsA, uint32_t widthA, uint32_t heightA,
        const uint8_t* pixelsB, uint32_t widthB, uint32_t heightB,
        uint8_t threshold = kDefaultThreshold);

    /// Generates a diff heatmap image (RGBA8).
    ///
    /// Black = identical, red = different (intensity = magnitude),
    /// magenta = size mismatch (one image is smaller).
    ///
    /// # Arguments
    /// * `pixelsA` — First image RGBA8 data.
    /// * `widthA`, `heightA` — Dimensions of first image.
    /// * `pixelsB` — Second image RGBA8 data.
    /// * `widthB`, `heightB` — Dimensions of second image.
    /// * `outPixels` — Output heatmap buffer (resized to max(wA,wB) x max(hA,hB)).
    /// * `outWidth`, `outHeight` — Output dimensions.
    /// * `amplify` — Multiplier for diff brightness (default 4x to make subtle diffs visible).
    static void GenerateHeatmap(
        const uint8_t* pixelsA, uint32_t widthA, uint32_t heightA,
        const uint8_t* pixelsB, uint32_t widthB, uint32_t heightB,
        std::vector<uint8_t>& outPixels, uint32_t& outWidth, uint32_t& outHeight,
        uint32_t amplify = 4);

    /// Produces a 3-panel comparison grid: [Image A | Image B | Diff Heatmap]
    /// with labels and diff statistics, saved as a single PNG.
    ///
    /// # Arguments
    /// * `pathA` — Path to first PNG.
    /// * `labelA` — Label for first image (e.g., "DX12 SM6.6").
    /// * `pathB` — Path to second PNG.
    /// * `labelB` — Label for second image (e.g., "DX11 SM5.0").
    /// * `outputPath` — Output PNG path for the 3-panel grid.
    /// * `cellSize` — Size of each panel in the grid (default 384).
    /// * `threshold` — Per-channel diff threshold (default 2).
    ///
    /// # Returns
    /// true on success, false on load/write error.
    [[nodiscard]] static bool DiffToPNG(
        const std::string& pathA, const std::string& labelA,
        const std::string& pathB, const std::string& labelB,
        const std::string& outputPath,
        uint32_t cellSize = 384,
        uint8_t threshold = kDefaultThreshold);
};

} // namespace uz
