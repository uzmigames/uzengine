/// UzEngine Asset Importer Registry
///
/// Central registry that maps file extensions to importers.
/// Register importers at startup, then look up the correct importer
/// for any source file by its extension.
///
/// # Thread Safety
/// Registration is NOT thread-safe (call at startup only).
/// Lookup is thread-safe after registration is complete.

#pragma once

#include "asset_importer.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

/// Registry of asset importers, keyed by file extension.
///
/// Usage:
/// ```cpp
/// AssetImporterRegistry registry;
/// registry.Register(std::make_unique<ShaderImporter>());
/// auto* importer = registry.FindByExtension(".hlsl");
/// ```
class AssetImporterRegistry {
public:
    /// Registers an importer and maps all its declared extensions to it.
    ///
    /// Queries `importer->Name()` and `importer->Extensions()` to build
    /// the internal lookup maps. Ownership is transferred to the registry.
    ///
    /// # Arguments
    /// * `importer` - The importer to register. Must not be null.
    ///
    /// # Returns
    /// `true` on success. `false` if:
    /// - `importer` is null
    /// - An importer with the same name is already registered
    /// - Any of its extensions are already claimed by another importer
    ///
    /// # Errors
    /// Logs `Asset::Error` with the conflicting name or extension on failure.
    /// On failure, the importer is NOT registered (all-or-nothing).
    bool Register(std::unique_ptr<IAssetImporter> importer);

    /// Finds the importer that handles the given file extension.
    ///
    /// # Arguments
    /// * `extension` - File extension including the dot (e.g., ".hlsl", ".png").
    ///
    /// # Returns
    /// Raw pointer to the importer (owned by this registry), or `nullptr`
    /// if no importer handles that extension.
    [[nodiscard]] IAssetImporter* FindByExtension(const std::string& extension) const;

    /// Finds an importer by its unique name.
    ///
    /// # Arguments
    /// * `name` - Importer name as returned by `IAssetImporter::Name()`.
    ///
    /// # Returns
    /// Raw pointer to the importer (owned by this registry), or `nullptr`.
    [[nodiscard]] IAssetImporter* FindByName(const std::string& name) const;

    /// Returns a const reference to all registered importers.
    [[nodiscard]] const std::vector<std::unique_ptr<IAssetImporter>>& GetImporters() const {
        return m_importers;
    }

    /// Returns the number of registered importers.
    [[nodiscard]] size_t Count() const { return m_importers.size(); }

    /// Returns a list of all registered file extensions.
    ///
    /// The order is unspecified (hash map iteration order).
    [[nodiscard]] std::vector<std::string> GetExtensions() const;

    /// Removes all registered importers and clears all lookup maps.
    void Clear();

private:
    std::vector<std::unique_ptr<IAssetImporter>> m_importers;
    std::unordered_map<std::string, IAssetImporter*> m_extensionMap;
    std::unordered_map<std::string, IAssetImporter*> m_nameMap;
};

} // namespace uz
