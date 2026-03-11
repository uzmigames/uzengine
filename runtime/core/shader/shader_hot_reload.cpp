/// ShaderHotReload Implementation
///
/// FileWatcher uses std::filesystem::last_write_time for polling.
/// ShaderHotReload combines the watcher with dependency graph propagation.

#include "shader_hot_reload.h"
#include "shader_dependency_graph.h"
#include "log.h"

#include <algorithm>
#include <cctype>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Path normalization (forward slashes, lowercase on Windows)
// ─────────────────────────────────────────────────────────────────────────────

std::string FileWatcher::NormalizePath(const std::filesystem::path& p) {
    std::string result = p.generic_string();

#ifdef _WIN32
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// FileWatcher
// ─────────────────────────────────────────────────────────────────────────────

bool FileWatcher::IsShaderExtension(const std::string& ext) {
    return ext == ".hlsl" || ext == ".hlsli" || ext == ".h" ||
           ext == ".fx" || ext == ".fxh";
}

void FileWatcher::ScanDirectory(const std::filesystem::path& dir) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;

        const auto ext = entry.path().extension().string();
        if (!IsShaderExtension(ext)) continue;

        auto normalized = NormalizePath(entry.path());
        auto writeTime = entry.last_write_time(ec);
        if (!ec) {
            m_timestamps[normalized] = writeTime;
        }
    }
}

void FileWatcher::Watch(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) {
        UZ_LOG(IO, Warning, "FileWatcher: directory does not exist: '{}'", dir.generic_string());
        return;
    }

    m_watchDirs.push_back(dir);
    ScanDirectory(dir);
    m_watching = true;

    UZ_LOG(IO, Info, "FileWatcher: watching '{}' ({} files)", dir.generic_string(), m_timestamps.size());
}

void FileWatcher::StopAll() {
    m_watchDirs.clear();
    m_timestamps.clear();
    m_watching = false;

    UZ_LOG(IO, Info, "FileWatcher: stopped all watches");
}

std::vector<std::string> FileWatcher::PollChanges() {
    std::vector<std::string> changed;
    if (!m_watching) return changed;

    // Check existing files for modifications and detect new files
    for (const auto& dir : m_watchDirs) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;

            const auto ext = entry.path().extension().string();
            if (!IsShaderExtension(ext)) continue;

            auto normalized = NormalizePath(entry.path());
            auto writeTime = entry.last_write_time(ec);
            if (ec) continue;

            auto it = m_timestamps.find(normalized);
            if (it == m_timestamps.end()) {
                // New file detected
                m_timestamps[normalized] = writeTime;
                changed.push_back(normalized);
            } else if (it->second != writeTime) {
                // Existing file was modified
                it->second = writeTime;
                changed.push_back(normalized);
            }
        }
    }

    return changed;
}

bool FileWatcher::IsWatching() const noexcept {
    return m_watching;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShaderHotReload
// ─────────────────────────────────────────────────────────────────────────────

void ShaderHotReload::Start(const std::vector<std::filesystem::path>& watchDirs) {
    Stop();

    for (const auto& dir : watchDirs) {
        m_watcher.Watch(dir);
    }
    m_running = true;

    UZ_LOG(IO, Info, "ShaderHotReload: started with {} watch directories", watchDirs.size());
}

void ShaderHotReload::Stop() {
    m_watcher.StopAll();
    m_running = false;

    UZ_LOG(IO, Info, "ShaderHotReload: stopped");
}

std::vector<std::string> ShaderHotReload::Poll() {
    if (!m_running) return {};

    auto changedFiles = m_watcher.PollChanges();
    std::vector<std::string> newlyDirty;

    for (const auto& file : changedFiles) {
        // Only report files that weren't already dirty
        if (m_dirtySet.find(file) == m_dirtySet.end()) {
            newlyDirty.push_back(file);
        }
        MarkDirty(file);
    }

    if (!newlyDirty.empty()) {
        UZ_LOG(IO, Info, "ShaderHotReload: {} files changed, {} total dirty",
               newlyDirty.size(), m_dirtySet.size());
    }

    return newlyDirty;
}

bool ShaderHotReload::IsDirty(const std::string& shaderPath) const {
    return m_dirtySet.count(shaderPath) > 0;
}

void ShaderHotReload::MarkDirty(const std::string& shaderPath) {
    m_dirtySet.insert(shaderPath);

    if (m_depGraph != nullptr) {
        auto dependents = m_depGraph->GetDependents(shaderPath);
        for (const auto& dep : dependents) {
            m_dirtySet.insert(dep);
        }

        if (!dependents.empty()) {
            UZ_LOG(IO, Trace, "ShaderHotReload: '{}' marked dirty + {} dependents",
                   shaderPath, dependents.size());
        }
    }
}

void ShaderHotReload::ClearDirty(const std::string& shaderPath) {
    m_dirtySet.erase(shaderPath);
}

void ShaderHotReload::ClearAll() {
    m_dirtySet.clear();
}

std::vector<std::string> ShaderHotReload::GetDirtyShaders() const {
    std::vector<std::string> result(m_dirtySet.begin(), m_dirtySet.end());
    std::sort(result.begin(), result.end());
    return result;
}

size_t ShaderHotReload::GetDirtyCount() const noexcept {
    return m_dirtySet.size();
}

void ShaderHotReload::SetDependencyGraph(ShaderDependencyGraph* graph) noexcept {
    m_depGraph = graph;
}

bool ShaderHotReload::IsRunning() const noexcept {
    return m_running;
}

} // namespace uz
