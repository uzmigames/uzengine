/// UzEngine Memory-Mapped File — Linux Implementation
///
/// Provides zero-copy read access to files by mapping them into the
/// process virtual address space using POSIX mmap. The mapping is
/// released on destruction (RAII).
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

#ifdef __linux__

#include <cstddef>

namespace uz {

/// Memory-mapped file for zero-copy read access.
///
/// Maps an entire file into the process address space using POSIX
/// `open()` + `fstat()` + `mmap(PROT_READ, MAP_PRIVATE)`. The file
/// descriptor is closed immediately after `mmap()` — the mapping
/// keeps the file data accessible until `Close()` or destruction.
///
/// Move-only: ownership of the mapping transfers on move.
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

    /// Move constructor — transfers ownership of the mapping from `other`.
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
    /// Logs `FileSystem::Error` with path and errno description on failure.
    /// Cannot map a zero-byte file (returns false). If already open,
    /// closes the previous mapping first.
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
    void* m_data = nullptr;  ///< Base address from mmap(); nullptr when not mapped.
    size_t m_size = 0;       ///< File size in bytes, used for munmap().
};

} // namespace uz

#endif // __linux__
