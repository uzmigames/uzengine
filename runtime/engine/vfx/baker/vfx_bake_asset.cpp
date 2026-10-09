#include "vfx_bake_asset.h"

#include "engine/logging/log.h"
#include "file_system.h"
#include "vfs.h"

#include <json.hpp>

#include <fstream>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

bool VfxBakeAsset::IsWellFormed() const noexcept {
    if (frameCount == 0u) return false;
    if (rows == 0u || cols == 0u) return false;
    if (rows * cols != frameCount) return false;
    if (cellWidth == 0u || cellHeight == 0u) return false;
    if (atlasPath.empty()) return false;
    return true;
}

std::unique_ptr<VfxBakeAsset> VfxBakeAsset::LoadFromString(const std::string& jsonStr) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonStr);
    } catch (const nlohmann::json::parse_error& e) {
        UZ_LOG(VFX, Error, "VfxBakeAsset: JSON parse error: {}", e.what());
        return nullptr;
    }
    if (!root.is_object()) {
        UZ_LOG(VFX, Error, "VfxBakeAsset: root must be an object");
        return nullptr;
    }
    if (root.value("format", std::string{}) != "uzvfxbake") {
        UZ_LOG(VFX, Error, "VfxBakeAsset: unexpected or missing 'format'");
        return nullptr;
    }

    auto asset = std::make_unique<VfxBakeAsset>();
    asset->sourcePath      = root.value("source",      std::string{});
    asset->atlasPath       = root.value("atlas_path",  std::string{});
    asset->frameCount      = root.value("frame_count", 0u);
    asset->rows            = root.value("rows",        0u);
    asset->cols            = root.value("cols",        0u);
    asset->fps             = root.value("fps",         30.0f);
    asset->durationSeconds = root.value("duration_s",  0.0f);
    asset->cellWidth       = root.value("cell_width",  0u);
    asset->cellHeight      = root.value("cell_height", 0u);

    // Derive duration from framework when the field is omitted but fps
    // and frameCount are present — saves authors one field.
    if (asset->durationSeconds <= 0.0f && asset->fps > 0.0f && asset->frameCount > 0u) {
        asset->durationSeconds = static_cast<float>(asset->frameCount) / asset->fps;
    }

    // Well-formed check is a warning, not a fatal — external tools might
    // reference bakes that are still being produced.
    if (!asset->IsWellFormed()) {
        UZ_LOG(VFX, Warning,
               "VfxBakeAsset: loaded metadata is not well-formed "
               "(frames={}, rows={}, cols={}, cell={}x{}, atlas='{}')",
               asset->frameCount, asset->rows, asset->cols,
               asset->cellWidth, asset->cellHeight, asset->atlasPath);
    }
    return asset;
}

std::unique_ptr<VfxBakeAsset> VfxBakeAsset::Load(const std::string& path, IVFS* vfs) {
    std::string content;
    if (vfs) {
        const auto bytes = vfs->Read(path);
        content.assign(bytes.begin(), bytes.end());
    } else {
        content = FileReadAllText(path.c_str());
    }
    if (content.empty()) {
        UZ_LOG(VFX, Error, "VfxBakeAsset: failed to read '{}'", path);
        return nullptr;
    }
    return LoadFromString(content);
}

bool VfxBakeAsset::SaveToFile(const std::string& path) const {
    nlohmann::json root;
    root["format"]      = "uzvfxbake";
    root["version"]     = 1;
    if (!sourcePath.empty()) root["source"] = sourcePath;
    root["atlas_path"]  = atlasPath;
    root["frame_count"] = frameCount;
    root["rows"]        = rows;
    root["cols"]        = cols;
    root["fps"]         = fps;
    root["duration_s"]  = durationSeconds;
    root["cell_width"]  = cellWidth;
    root["cell_height"] = cellHeight;

    std::ofstream ofs(path);
    if (!ofs) {
        UZ_LOG(VFX, Error, "VfxBakeAsset: failed to open '{}' for writing", path);
        return false;
    }
    ofs << root.dump(2);
    return static_cast<bool>(ofs);
}

} // namespace uz
