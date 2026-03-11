/// UzEngine Asset Importer Registry — Implementation
///
/// Registers importers by name and extension, with duplicate detection.

#include "asset_registry.h"
#include "log.h"

namespace uz {

bool AssetImporterRegistry::Register(std::unique_ptr<IAssetImporter> importer) {
    if (!importer) {
        UZ_LOG(Asset, Error, "Attempted to register null importer");
        return false;
    }

    const char* name = importer->Name();
    if (m_nameMap.count(name) > 0) {
        UZ_LOG(Asset, Error, "Importer '{}' already registered", name);
        return false;
    }

    auto extensions = importer->Extensions();
    for (const auto& ext : extensions) {
        if (m_extensionMap.count(ext) > 0) {
            UZ_LOG(Asset, Error, "Extension '{}' already claimed by '{}'",
                   ext, m_extensionMap[ext]->Name());
            return false;
        }
    }

    IAssetImporter* raw = importer.get();
    for (const auto& ext : extensions) {
        m_extensionMap[ext] = raw;
    }
    m_nameMap[name] = raw;
    m_importers.push_back(std::move(importer));

    UZ_LOG(Asset, Info, "Registered importer '{}' v{} for {} extension(s)",
           name, raw->Version(), extensions.size());
    return true;
}

IAssetImporter* AssetImporterRegistry::FindByExtension(const std::string& extension) const {
    auto it = m_extensionMap.find(extension);
    return it != m_extensionMap.end() ? it->second : nullptr;
}

IAssetImporter* AssetImporterRegistry::FindByName(const std::string& name) const {
    auto it = m_nameMap.find(name);
    return it != m_nameMap.end() ? it->second : nullptr;
}

std::vector<std::string> AssetImporterRegistry::GetExtensions() const {
    std::vector<std::string> result;
    result.reserve(m_extensionMap.size());
    for (const auto& [ext, _] : m_extensionMap) {
        result.push_back(ext);
    }
    return result;
}

void AssetImporterRegistry::Clear() {
    m_extensionMap.clear();
    m_nameMap.clear();
    m_importers.clear();
}

} // namespace uz
