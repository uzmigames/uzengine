/// UzEngine Shared Memory Implementation (macOS)
///
/// POSIX shm_open + ftruncate + mmap for named IPC regions managed by the
/// macOS kernel. The API is identical to the Linux implementation; only the
/// compile guard differs (`__APPLE__` vs `__linux__`).

#include "shared_memory.h"

#ifdef __APPLE__

#include "log.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Encodes an int fd as the void* mapping field.
[[nodiscard]] void* FdToMapping(int fd) noexcept {
    return reinterpret_cast<void*>(static_cast<intptr_t>(fd));
}

/// Decodes the void* mapping field back to an int fd. Returns -1 if null.
[[nodiscard]] int MappingToFd(void* mapping) noexcept {
    if (!mapping) return -1;
    return static_cast<int>(reinterpret_cast<intptr_t>(mapping));
}

/// Maps an already-open shm fd into the process address space.
/// Returns MAP_FAILED on error.
void* MapShm(int fd, size_t size) noexcept {
    return ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

SharedMemoryHandle SharedMemoryCreate(const char* name, size_t size) {
    SharedMemoryHandle result{};

    if (!name || size == 0) {
        UZ_LOG(Platform, Error, "SharedMemoryCreate: invalid arguments (name={}, size={})",
               name ? name : "null", size);
        return result;
    }

    // O_CREAT | O_RDWR: create if absent, open if exists.
    // macOS requires the name to start with '/' (EINVAL otherwise).
    const int fd = ::shm_open(name, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        UZ_LOG(Platform, Error, "SharedMemoryCreate: shm_open failed for '{}': {}",
               name, ::strerror(errno));
        return result;
    }

    // Set the size of the shared memory object
    if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
        UZ_LOG(Platform, Error, "SharedMemoryCreate: ftruncate failed for '{}': {}",
               name, ::strerror(errno));
        ::close(fd);
        return result;
    }

    void* data = MapShm(fd, size);
    if (data == MAP_FAILED) {
        UZ_LOG(Platform, Error, "SharedMemoryCreate: mmap failed for '{}': {}",
               name, ::strerror(errno));
        ::close(fd);
        return result;
    }

    result.mapping = FdToMapping(fd);
    result.data    = data;
    result.size    = size;

    UZ_LOG(Platform, Info, "Shared memory created: '{}' ({} bytes)", name, size);
    return result;
}

SharedMemoryHandle SharedMemoryOpen(const char* name, size_t size) {
    SharedMemoryHandle result{};

    if (!name || size == 0) {
        UZ_LOG(Platform, Error, "SharedMemoryOpen: invalid arguments");
        return result;
    }

    // No O_CREAT: fail if the region doesn't already exist
    const int fd = ::shm_open(name, O_RDWR, 0);
    if (fd < 0) {
        UZ_LOG(Platform, Error, "SharedMemoryOpen: shm_open failed for '{}': {}",
               name, ::strerror(errno));
        return result;
    }

    void* data = MapShm(fd, size);
    if (data == MAP_FAILED) {
        UZ_LOG(Platform, Error, "SharedMemoryOpen: mmap failed for '{}': {}",
               name, ::strerror(errno));
        ::close(fd);
        return result;
    }

    result.mapping = FdToMapping(fd);
    result.data    = data;
    result.size    = size;

    UZ_LOG(Platform, Info, "Shared memory opened: '{}' ({} bytes)", name, size);
    return result;
}

void SharedMemoryClose(SharedMemoryHandle& handle) {
    if (handle.data) {
        ::munmap(handle.data, handle.size);
    }

    const int fd = MappingToFd(handle.mapping);
    if (fd >= 0) {
        ::close(fd);
    }

    handle = {};
}

} // namespace uz

#endif // __APPLE__
