#include "engine/renderer/resources/material.h"
#include "engine/renderer/resources/texture.h"
#include "engine/renderer/resources/texture_loader.h"
#include "engine/core/diag.h"
#include "engine/core/platform.h"
#include "engine/core/profiler.h"
#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_set>
#include <vector>

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
// TextureManager — Async texture loading with worker thread decode
//
// Architecture (based on Godot 4 + UE5 patterns):
//   1. get_or_queue() creates a unique handle per path (1x1 placeholder)
//   2. tick() polls AsyncTextureLoader workers for completed decodes
//   3. tick() calls texture_replace() to swap resource in same slot
//   4. tick() batch-uploads real pixel data in one GPU submission
//   5. Materials auto-see updated textures (zero rebinding, no refresh)
//   6. Zero stbi_load on main thread — all CPU decode on workers
//
// Placeholder textures (Godot DEFAULT_RD_TEXTURE pattern):
//   - WHITE  (1x1 RGBA 255,255,255,255) — albedo, metallic, roughness, AO
//   - NORMAL (1x1 RGBA 128,128,255,255) — flat normal map
//   - BLACK  (1x1 RGBA 0,0,0,255)       — displacement, emission
// ============================================================================

struct PendingTexture {
    RhiTexture handle;
    uint8_t*   pixels;       // stbi-allocated RGBA8 data
    size_t     pixel_size;   // w * h * 4
};

// Placeholder type heuristic: guess from texture slot name suffix.
enum class PlaceholderType { White, Normal, Black };

static PlaceholderType guess_placeholder(const std::string& path) {
    if (path.find("normal") != std::string::npos) return PlaceholderType::Normal;
    if (path.find("displacement") != std::string::npos) return PlaceholderType::Black;
    if (path.find("emission") != std::string::npos) return PlaceholderType::Black;
    return PlaceholderType::White;
}

class TextureManager {
public:
    TextureManager() : loader_(2) {}

    // -----------------------------------------------------------------------
    // init_placeholders — create 1x1 GPU placeholder textures.
    // Must be called after RHI init. Safe to call multiple times.
    // -----------------------------------------------------------------------
    void init_placeholders() {
        if (placeholders_ready_) return;
        if (!rhi()) return;

        auto make_1x1 = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                            const char* name) -> RhiTexture {
            RhiTextureDesc desc{};
            desc.width      = 1;
            desc.height     = 1;
            desc.depth      = 1;
            desc.mip_levels = 1;
            desc.array_size = 1;
            desc.format     = RHI_FORMAT_RGBA8_UNORM;
            desc.dimension  = RHI_TEXTURE_DIM_2D;
            desc.usage      = RHI_TEXTURE_USAGE_SHADER_READ
                            | RHI_TEXTURE_USAGE_COPY_DST;
            desc.debug_name = name;

            RhiTexture handle = rhi()->texture_create(&desc);
            if (handle == RHI_NULL) return RHI_NULL;

            uint8_t pixel[4] = {r, g, b, a};
            rhi()->texture_upload(handle, pixel, 4);
            return handle;
        };

        placeholder_white_  = make_1x1(255, 255, 255, 255, "placeholder_white");
        placeholder_normal_ = make_1x1(128, 128, 255, 255, "placeholder_normal");
        placeholder_black_  = make_1x1(0,   0,   0,   255, "placeholder_black");
        placeholders_ready_ = (placeholder_white_ != RHI_NULL);

        if (placeholders_ready_) {
            DIAG("TextureManager: placeholders created (white/normal/black)");
        }
    }

    RhiTexture placeholder(PlaceholderType type) const {
        switch (type) {
        case PlaceholderType::Normal: return placeholder_normal_;
        case PlaceholderType::Black:  return placeholder_black_;
        default:                      return placeholder_white_;
        }
    }

    // -----------------------------------------------------------------------
    // get_or_prepare — SYNCHRONOUS: CPU decode + GPU create (no upload).
    // Used by tests and single-texture loads where blocking is acceptable.
    // -----------------------------------------------------------------------
    RhiTexture get_or_prepare(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = cache_.find(path);
        if (it != cache_.end()) {
            return it->second.handle;
        }

        DecodedTexture decoded = decode_texture_from_file(path.c_str());
        if (!decoded.pixels) {
            return RHI_NULL;
        }

        RhiTextureDesc desc{};
        desc.width      = decoded.width;
        desc.height     = decoded.height;
        desc.depth      = 1;
        desc.mip_levels = 1;
        desc.array_size = 1;
        desc.format     = RHI_FORMAT_RGBA8_UNORM;
        desc.dimension  = RHI_TEXTURE_DIM_2D;
        desc.usage      = RHI_TEXTURE_USAGE_SHADER_READ | RHI_TEXTURE_USAGE_COPY_DST;
        desc.debug_name = path.c_str();

        RhiTexture handle = rhi()->texture_create(&desc);
        if (handle == RHI_NULL) {
            free_decoded_texture(decoded);
            return RHI_NULL;
        }

        GpuTexture gpu_tex;
        gpu_tex.handle = handle;
        gpu_tex.width  = decoded.width;
        gpu_tex.height = decoded.height;
        gpu_tex.format = RHI_FORMAT_RGBA8_UNORM;
        cache_[path] = gpu_tex;

        pending_.push_back({handle, decoded.pixels, decoded.pixel_size});
        return handle;
    }

    // -----------------------------------------------------------------------
    // get_or_queue — NON-BLOCKING: returns a unique handle per path.
    // If not cached, creates a 1x1 placeholder texture with its own handle,
    // queues path for async decode, and returns that handle. When the real
    // texture loads, tick() calls texture_replace() on the same handle —
    // materials auto-see updated data with zero rebinding.
    // -----------------------------------------------------------------------
    RhiTexture get_or_queue(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Already loaded (or has a pending placeholder handle)?
        auto it = cache_.find(path);
        if (it != cache_.end()) {
            return it->second.handle;
        }

        if (!rhi()) return RHI_NULL;

        // Create a unique 1x1 texture for this path (placeholder content).
        PlaceholderType pt = guess_placeholder(path);
        uint8_t pixel[4];
        switch (pt) {
        case PlaceholderType::Normal: pixel[0]=128; pixel[1]=128; pixel[2]=255; pixel[3]=255; break;
        case PlaceholderType::Black:  pixel[0]=0;   pixel[1]=0;   pixel[2]=0;   pixel[3]=255; break;
        default:                      pixel[0]=255; pixel[1]=255; pixel[2]=255; pixel[3]=255; break;
        }

        RhiTextureDesc desc{};
        desc.width      = 1;
        desc.height     = 1;
        desc.depth      = 1;
        desc.mip_levels = 1;
        desc.array_size = 1;
        desc.format     = RHI_FORMAT_RGBA8_UNORM;
        desc.dimension  = RHI_TEXTURE_DIM_2D;
        desc.usage      = RHI_TEXTURE_USAGE_SHADER_READ | RHI_TEXTURE_USAGE_COPY_DST;
        desc.debug_name = path.c_str();

        RhiTexture handle = rhi()->texture_create(&desc);
        if (handle == RHI_NULL) return RHI_NULL;

        rhi()->texture_upload(handle, pixel, 4);

        GpuTexture gpu_tex;
        gpu_tex.handle = handle;
        gpu_tex.width  = 1;
        gpu_tex.height = 1;
        gpu_tex.format = RHI_FORMAT_RGBA8_UNORM;
        cache_[path] = gpu_tex;

        // Queue for async decode on worker threads.
        queued_set_.insert(path);
        loader_.submit(path);
        return handle;
    }

    // -----------------------------------------------------------------------
    // lookup — return cached handle or RHI_NULL (no queueing).
    // -----------------------------------------------------------------------
    RhiTexture lookup(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(path);
        return (it != cache_.end()) ? it->second.handle : RHI_NULL;
    }

    // -----------------------------------------------------------------------
    // tick — poll async worker results, replace placeholders, batch upload.
    // Returns true if any textures were replaced with real data.
    // Call once per frame from the main render thread.
    //
    // Flow: texture_replace() swaps the D3D12 resource in the existing slot
    // (same RhiTexture handle), then batch-uploads real pixel data. Materials
    // auto-see updated textures — zero rebinding, zero refresh_textures().
    // -----------------------------------------------------------------------
    bool tick(int budget) {
        UZ_PROFILE_ZONE("TextureManager::tick");

        // Poll completed decodes from worker threads.
        auto results = loader_.poll_completed();
        if (results.empty() && deferred_results_.empty()) return false;

        int processed = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto process_result = [&](DecodeResult& r) -> bool {
                if (processed >= budget) {
                    deferred_results_.push_back(std::move(r));
                    return false;
                }

                queued_set_.erase(r.path);

                if (!r.pixels) {
                    DIAG_FMT("TextureManager: async decode failed '%s'",
                             r.path.c_str());
                    return false;
                }

                // Find the placeholder handle (created by get_or_queue).
                auto it = cache_.find(r.path);
                if (it == cache_.end()) {
                    free_decoded_texture_pixels(r.pixels);
                    return false;
                }

                RhiTexture handle = it->second.handle;

                // Replace the 1x1 placeholder resource with real dimensions.
                RhiTextureDesc desc{};
                desc.width      = r.width;
                desc.height     = r.height;
                desc.depth      = 1;
                desc.mip_levels = 1;
                desc.array_size = 1;
                desc.format     = RHI_FORMAT_RGBA8_UNORM;
                desc.dimension  = RHI_TEXTURE_DIM_2D;
                desc.usage      = RHI_TEXTURE_USAGE_SHADER_READ
                                | RHI_TEXTURE_USAGE_COPY_DST;
                desc.debug_name = r.path.c_str();

                if (!rhi()->texture_replace(handle, &desc)) {
                    free_decoded_texture_pixels(r.pixels);
                    return false;
                }

                // Update cache metadata.
                it->second.width  = r.width;
                it->second.height = r.height;

                pending_.push_back({handle, r.pixels, r.pixel_size});
                ++processed;
                return true;
            };

            for (auto& r : results) {
                process_result(r);
            }

            // Also process any deferred results from previous frames.
            if (processed < budget && !deferred_results_.empty()) {
                auto deferred = std::move(deferred_results_);
                deferred_results_.clear();
                for (auto& r : deferred) {
                    process_result(r);
                }
            }

            // Batch-upload all replaced textures in one GPU submission.
            if (!pending_.empty()) {
                flush_pending_locked();
            }
        }

        if (processed > 0) {
            DIAG_FMT("TextureManager: replaced %d textures (async, %zu pending)",
                     processed, loader_.pending_count() + deferred_results_.size());
        }
        return processed > 0;
    }

    // -----------------------------------------------------------------------
    // flush_pending — upload all pending textures (called from tick or sync).
    // -----------------------------------------------------------------------
    void flush_pending() {
        std::lock_guard<std::mutex> lock(mutex_);
        flush_pending_locked();
    }

    bool has_queued() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !queued_set_.empty() || !deferred_results_.empty()
            || loader_.pending_count() > 0;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& p : pending_) {
            free_decoded_texture_pixels(p.pixels);
        }
        pending_.clear();
        queued_set_.clear();

        for (auto& r : deferred_results_) {
            if (r.pixels) free_decoded_texture_pixels(r.pixels);
        }
        deferred_results_.clear();

        for (auto& [path, tex] : cache_) {
            destroy_texture(tex);
        }
        cache_.clear();
    }

private:
    void flush_pending_locked() {
        if (pending_.empty()) return;

        std::vector<RhiTextureUploadItem> items;
        items.reserve(pending_.size());
        for (auto& p : pending_) {
            RhiTextureUploadItem item{};
            item.texture     = p.handle;
            item.subresource = 0;
            item.data        = p.pixels;
            item.size        = p.pixel_size;
            items.push_back(item);
        }

        rhi()->texture_upload_batch(items.data(),
                                     static_cast<uint32_t>(items.size()));

        for (auto& p : pending_) {
            free_decoded_texture_pixels(p.pixels);
        }
        pending_.clear();
    }

    AsyncTextureLoader loader_;
    std::unordered_map<std::string, GpuTexture> cache_;
    std::vector<PendingTexture> pending_;
    std::vector<DecodeResult> deferred_results_;  // decoded but GPU create deferred
    std::unordered_set<std::string> queued_set_;
    mutable std::mutex mutex_;

    // 1x1 placeholder textures (Godot DEFAULT_RD_TEXTURE pattern).
    RhiTexture placeholder_white_  = RHI_NULL;
    RhiTexture placeholder_normal_ = RHI_NULL;
    RhiTexture placeholder_black_  = RHI_NULL;
    bool       placeholders_ready_ = false;
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
                mat.texture_handles[key] = g_texture_manager.get_or_queue(tex_path);
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
    UZ_PROFILE_ZONE("MaterialManager::load");
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

bool MaterialManager::refresh_textures() {
    // No-op: texture_replace() swaps resources in-place via the same
    // RhiTexture handle. Materials auto-see updated textures with zero
    // rebinding. Kept for backward compatibility.
    (void)impl_;
    return false;
}

void MaterialManager::clear() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->materials.clear();
    g_texture_manager.clear();
}

// ============================================================================
// Free functions — expose TextureManager to the renderer
// ============================================================================

void texture_init_placeholders() {
    g_texture_manager.init_placeholders();
}

bool texture_tick(int budget) {
    return g_texture_manager.tick(budget);
}

bool texture_has_queued() {
    return g_texture_manager.has_queued();
}

void texture_clear_all() {
    g_texture_manager.clear();
}

RhiTexture texture_get_or_queue(const std::string& path) {
    return g_texture_manager.get_or_queue(path);
}

} // namespace uz
