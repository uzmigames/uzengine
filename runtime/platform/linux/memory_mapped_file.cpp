/// UzEngine Memory-Mapped File — Linux Implementation
///
/// Uses open() + fstat() + mmap(PROT_READ, MAP_PRIVATE) for zero-copy reads.
/// The file descriptor is closed immediately after mmap() succeeds — the
/// kernel keeps the file data accessible through the mapping until munmap().

#ifdef __linux__

#include "memory_mapped_file.h"

#include "log.h"
#include "profiler.h"

#include <cerrno>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

UZ_DECLARE_LOG_CATEGORY(FileSystem);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

MemoryMappedFile::~MemoryMappedFile() {
    Close();
}

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept
    : m_data(std::exchange(other.m_data, nullptr))
    , m_size(std::exchange(other.m_size, 0)) {
}

MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& other) noexcept {
    if (this != &other) {
        Close();
        m_data = std::exchange(other.m_data, nullptr);
        m_size = std::exchange(other.m_size, 0);
    }
    return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// Open / Close
// ─────────────────────────────────────────────────────────────────────────────

bool MemoryMappedFile::Open(const char* path) {
    UZ_PROFILE_FUNCTION();

    // Close any existing mapping before opening a new one.
    if (IsOpen()) {
        Close();
    }

    if (!path || path[0] == '\0') {
        UZ_LOG(FileSystem, Error, "MemoryMappedFile::Open: empty or invalid path");
        return false;
    }

    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
        const int err = errno;
        UZ_LOG(FileSystem, Error, "MemoryMappedFile::Open: open failed for '{}', errno={}", path, err);
        return false;
    }

    struct stat st{};
    if (fstat(fd, &st) != 0) {
        const int err = errno;
        UZ_LOG(FileSystem, Error, "MemoryMappedFile::Open: fstat failed for '{}', errno={}", path, err);
        close(fd);
        return false;
    }

    // mmap(2) requires a non-zero length.
    if (st.st_size == 0) {
        UZ_LOG(FileSystem, Warning, "MemoryMappedFile::Open: file '{}' is empty, cannot map", path);
        close(fd);
        return false;
    }

    const size_t mapSize = static_cast<size_t>(st.st_size);
    void* data = mmap(nullptr, mapSize, PROT_READ, MAP_PRIVATE, fd, 0);

    // The file descriptor can be closed immediately — the mapping holds a
    // reference to the underlying file pages independent of the fd.
    close(fd);

    if (data == MAP_FAILED) {
        const int err = errno;
        UZ_LOG(FileSystem, Error, "MemoryMappedFile::Open: mmap failed for '{}', errno={}", path, err);
        return false;
    }

    m_data = data;
    m_size = mapSize;
    return true;
}

void MemoryMappedFile::Close() {
    if (m_data) {
        munmap(m_data, m_size);
        m_data = nullptr;
        m_size = 0;
    }
}

} // namespace uz

#endif // __linux__
