#pragma once

/// Per-emitter runtime parameter store for VFX graphs.
///
/// Stores named parameters that can be set from C++ or Lua and are
/// uploaded to the GPU constant buffer each frame. All values are
/// internally stored as Vec4 (float types use .x, Vec3 uses .xyz).
///
/// # Thread Safety
/// Not thread-safe. All access must occur on the main thread or be
/// externally synchronized.
///
/// # GPU Layout
/// `PackToBuffer()` writes parameters as sequential Vec4 values in
/// insertion order, producing a deterministic constant buffer layout.
///
/// # Example
/// ```cpp
/// VFXParameterStore params;
/// params.SetFloat("intensity", 2.5f);
/// params.SetVec3("windDir", {1.0f, 0.0f, 0.0f});
///
/// std::vector<uint8_t> buf(params.GetUploadSize());
/// params.PackToBuffer(buf.data(), static_cast<uint32_t>(buf.size()));
/// ```

#include "vector3.h"
#include "vector4.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VFXParameterStore — named parameter storage for GPU constant buffer upload
// ─────────────────────────────────────────────────────────────────────────────

class VFXParameterStore {
public:
    /// Sets a float parameter by name.
    ///
    /// Stored internally as Vec4{value, 0, 0, 0}. If the parameter
    /// already exists, its value is overwritten without changing
    /// insertion order.
    ///
    /// # Parameters
    /// - `name`: Parameter name (must not be empty).
    /// - `value`: Float value to store.
    void SetFloat(const std::string& name, float value);

    /// Sets a Vec3 parameter by name.
    ///
    /// Stored internally as Vec4{x, y, z, 0}. If the parameter
    /// already exists, its value is overwritten without changing
    /// insertion order.
    ///
    /// # Parameters
    /// - `name`: Parameter name (must not be empty).
    /// - `value`: Vec3 value to store.
    void SetVec3(const std::string& name, const Vec3& value);

    /// Sets a Vec4 parameter by name.
    ///
    /// Stored directly. If the parameter already exists, its value
    /// is overwritten without changing insertion order.
    ///
    /// # Parameters
    /// - `name`: Parameter name (must not be empty).
    /// - `value`: Vec4 value to store.
    void SetVec4(const std::string& name, const Vec4& value);

    /// Gets a float parameter by name.
    ///
    /// Reads the .x component of the stored Vec4.
    ///
    /// # Parameters
    /// - `name`: Parameter name to look up.
    /// - `defaultValue`: Value returned if `name` is not found.
    ///
    /// # Returns
    /// The stored float, or `defaultValue` if the parameter does not exist.
    [[nodiscard]] float GetFloat(const std::string& name,
                                 float defaultValue = 0.0f) const;

    /// Gets a Vec3 parameter by name.
    ///
    /// Reads the .xyz components of the stored Vec4.
    ///
    /// # Parameters
    /// - `name`: Parameter name to look up.
    /// - `defaultValue`: Value returned if `name` is not found.
    ///
    /// # Returns
    /// The stored Vec3, or `defaultValue` if the parameter does not exist.
    [[nodiscard]] Vec3 GetVec3(const std::string& name,
                               const Vec3& defaultValue = {}) const;

    /// Gets a Vec4 parameter by name.
    ///
    /// # Parameters
    /// - `name`: Parameter name to look up.
    /// - `defaultValue`: Value returned if `name` is not found.
    ///
    /// # Returns
    /// The stored Vec4, or `defaultValue` if the parameter does not exist.
    [[nodiscard]] Vec4 GetVec4(const std::string& name,
                               const Vec4& defaultValue = {}) const;

    /// Returns true if a parameter with the given name exists.
    ///
    /// # Parameters
    /// - `name`: Parameter name to check.
    [[nodiscard]] bool Has(const std::string& name) const;

    /// Removes a parameter by name.
    ///
    /// Removes from both the value map and the insertion-order list.
    ///
    /// # Parameters
    /// - `name`: Parameter name to remove.
    ///
    /// # Returns
    /// `true` if the parameter existed and was removed, `false` otherwise.
    bool Remove(const std::string& name);

    /// Clears all stored parameters.
    void Clear();

    /// Returns the number of stored parameters.
    [[nodiscard]] size_t GetCount() const noexcept;

    /// Returns all parameter names in insertion order.
    ///
    /// # Returns
    /// Vector of parameter names. Order matches the GPU buffer layout.
    [[nodiscard]] std::vector<std::string> GetNames() const;

    /// Returns the buffer size in bytes needed for GPU upload.
    ///
    /// Each parameter occupies 16 bytes (one Vec4).
    ///
    /// # Returns
    /// `GetCount() * 16`. Returns 0 if no parameters are stored.
    [[nodiscard]] uint32_t GetUploadSize() const noexcept;

    /// Packs all parameters into a contiguous buffer for GPU upload.
    ///
    /// Writes sequential Vec4 values in insertion order. The caller
    /// must provide a buffer of at least `GetUploadSize()` bytes.
    ///
    /// # Parameters
    /// - `outBuffer`: Destination buffer (must not be null unless size is 0).
    /// - `bufferSize`: Size of `outBuffer` in bytes.
    ///
    /// # Returns
    /// Number of bytes written. Returns 0 if `bufferSize` is insufficient.
    uint32_t PackToBuffer(void* outBuffer, uint32_t bufferSize) const;

private:
    /// Parameter values keyed by name. All types stored as Vec4.
    std::unordered_map<std::string, Vec4> values_;

    /// Insertion-order tracking for deterministic GPU layout.
    std::vector<std::string> insertionOrder_;
};

} // namespace uz
