#include "engine/renderer/resources/material.h"
#include "engine/renderer/resources/texture.h"
#include "engine/core/diag.h"
#include "engine/core/platform.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <mutex>

namespace uz {

// ============================================================================
// Internal Helpers
// ============================================================================

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::vector<float> parse_vector(const std::string& s) {
    std::vector<float> result;
    std::string content = s;
    if (content.front() == '[' && content.back() == ']') {
        content = content.substr(1, content.size() - 2);
    }
    
    std::stringstream ss(content);
    std::string item;
    while (std::getline(ss, item, ',')) {
        try {
            result.push_back(std::stof(trim(item)));
        } catch (...) {}
    }
    return result;
}

// ============================================================================
// TextureManager — Simple texture cache
// ============================================================================

class TextureManager {
public:
    RhiTexture get_or_load(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(path);
        if (it != cache_.end()) {
            return it->second.handle;
        }

        GpuTexture tex = load_texture_from_file(path.c_str(), path.c_str());
        if (tex.handle != RHI_NULL) {
            cache_[path] = tex;
            return tex.handle;
        }
        return RHI_NULL;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [path, tex] : cache_) {
            destroy_texture(tex);
        }
        cache_.clear();
    }

private:
    std::unordered_map<std::string, GpuTexture> cache_;
    std::mutex mutex_;
};

static TextureManager g_texture_manager;

// ============================================================================
// MaterialManager Implementation
// ============================================================================

struct MaterialManager::Impl {
    std::unordered_map<std::string, Material> materials;
    mutable std::mutex mutex;

    bool parse_material_file(const std::string& path, Material& mat) {
        std::ifstream file(path);
        if (!file.is_open()) {
            DIAG_FMT("MaterialManager: failed to open '%s'", path.c_str());
            return false;
        }

        // Derive project base directory from the material file path.
        // Material path is e.g. "E:/UzEngine/assets/materials/gold.uzmat"
        // Go up from the file to find the project root (up 3 levels: file -> materials -> assets -> root).
        std::string base_dir;
        {
            std::string p = path;
            for (auto& c : p) if (c == '\\') c = '/';
            // Strip filename → "E:/UzEngine/assets/materials"
            auto s1 = p.rfind('/');
            if (s1 != std::string::npos) {
                std::string d1 = p.substr(0, s1);
                // Strip "materials" → "E:/UzEngine/assets"
                auto s2 = d1.rfind('/');
                if (s2 != std::string::npos) {
                    std::string d2 = d1.substr(0, s2);
                    // Strip "assets" → "E:/UzEngine"
                    auto s3 = d2.rfind('/');
                    if (s3 != std::string::npos) {
                        base_dir = d2.substr(0, s3 + 1); // "E:/UzEngine/"
                    }
                }
            }
        }

        std::string line;
        while (std::getline(file, line)) {
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line[0] == '[') continue; // Skip section headers for now

            auto eq_pos = line.find('=');
            if (eq_pos == std::string::npos) continue;

            std::string key = trim(line.substr(0, eq_pos));
            std::string value = trim(line.substr(eq_pos + 1));

            if (key == "name") {
                mat.name = value;
            } else if (key == "shader") {
                mat.shader_name = value;
            } else if (value.find('[') != std::string::npos) {
                // Vector parameter
                mat.vec_parameters[key] = parse_vector(value);
            } else if (value.find('.') != std::string::npos && 
                      (value.find(".png") != std::string::npos || 
                       value.find(".jpg") != std::string::npos ||
                       value.find(".dds") != std::string::npos)) {
                // Texture parameter — resolve relative path
                std::string tex_path = value;
                if (!base_dir.empty() && !(tex_path.size() >= 2 && tex_path[1] == ':')) {
                    tex_path = base_dir + value;
                }
                mat.textures[key] = tex_path;
                mat.texture_handles[key] = g_texture_manager.get_or_load(tex_path);
            } else {
                // Scalar parameter
                try {
                    mat.parameters[key] = std::stof(value);
                } catch (...) {
                    // Fallback to treating as string if needed, or just skip
                }
            }
        }
        return true;
    }
};

MaterialManager::MaterialManager() : impl_(new Impl()) {}
MaterialManager::~MaterialManager() { delete impl_; }

Material* MaterialManager::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    auto it = impl_->materials.find(path);
    if (it != impl_->materials.end()) {
        return &it->second;
    }

    Material mat;
    if (impl_->parse_material_file(path, mat)) {
        impl_->materials[path] = std::move(mat);
        return &impl_->materials[path];
    }

    return nullptr;
}

Material* MaterialManager::get(const std::string& path) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->materials.find(path);
    return (it != impl_->materials.end()) ? &it->second : nullptr;
}

bool MaterialManager::reload(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->materials.find(path);
    if (it == impl_->materials.end()) return false;

    Material new_mat;
    if (impl_->parse_material_file(path, new_mat)) {
        it->second = std::move(new_mat);
        return true;
    }
    return false;
}

void MaterialManager::clear() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->materials.clear();
    g_texture_manager.clear();
}

} // namespace uz
