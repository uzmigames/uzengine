/// UzEngine Memory-Mapped File — Windows Implementation
///
/// Provides zero-copy read access to files by mapping them into the
/// process virtual address space. The mapping is released on destruction (RAII).
///
/// # Use Cases
/// - Large asset files (textures, meshes) where avoiding a copy matters
/// - Shader source files read frequently during hot-reload
/// - Any file where random access is needed without loading into a vector
///
/// # Example
/// ```cpp
/// uz::MemoryMappedFile mmap;
/// if (mmap.Open("assets/large_texture.uztexture")) {
///     const auto* header = static_cast<const TextureHeader*>(mmap.Data());
///     ProcessTexture(header, mmap.Size());
/// }
/// // File unmapped automatically when mmap goes out of scope
/// ```

#pragma once

#include <cstddef>

namespace uz {

/// Memory-mapped file for zero-copy read access.
///
/// Maps an entire file into the process address space using Win32
/// `CreateFileMappingW` + `MapViewOfFile`. The file remains mapped
/// until `Close()` is called or the object is destroyed.
///
/// Move-only: ownership of the OS handles transfers on move.
/// Not thread-safe: concurrent Open/Close calls require external sync.
/// Data pointer is safe to read from multiple threads after Open.
class MemoryMappedFile {
public:
    /// Constructs an unmapped file. IsOpen() returns false.
    MemoryMappedFile() = default;

    /// Unmaps and closes the file if currently open.
    ~MemoryMappedFile();

    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    /// Move constructor — transfers ownership of handles from `other`.
    /// After move, `other.IsOpen()` returns false.
    MemoryMappedFile(MemoryMappedFile&& other) noexcept;

    /// Move assignment — closes current mapping, takes ownership from `other`.
    /// After move, `other.IsOpen()` returns false.
    MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept;

    /// Opens and maps a file for reading.
    ///
    /// # Arguments
    /// * `path` — UTF-8 file path.
    ///
    /// # Returns
    /// - true if the file was successfully mapped. Data() and Size() are valid.
    /// - false if the file could not be opened or mapped (logged).
    ///
    /// # Errors
    /// Logs `FileSystem::Error` with path and Win32 error on failure.
    /// If already open, closes the previous mapping first.
    [[nodiscard]] bool Open(const char* path);

    /// Unmaps and closes the file.
    ///
    /// Safe to call even if not open (no-op). After this call,
    /// Data() returns nullptr and Size() returns 0.
    void Close();

    /// Returns pointer to the mapped file data.
    ///
    /// # Returns
    /// - Non-null pointer to the first byte of the file if open.
    /// - nullptr if the file is not open.
    [[nodiscard]] const void* Data() const { return m_data; }

    /// Returns the size of the mapped file in bytes.
    [[nodiscard]] size_t Size() const { return m_size; }

    /// Returns true if a file is currently mapped.
    [[nodiscard]] bool IsOpen() const { return m_data != nullptr; }

private:
    void* m_fileHandle = nullptr;    ///< Win32 HANDLE from CreateFileW.
    void* m_mappingHandle = nullptr; ///< Win32 HANDLE from CreateFileMappingW.
    void* m_data = nullptr;          ///< Base address from MapViewOfFile.
    size_t m_size = 0;               ///< File size in bytes.
};

} // namespace uz
