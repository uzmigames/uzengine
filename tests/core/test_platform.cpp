// test_platform.cpp -- Comprehensive tests for the Platform Abstraction Layer.
//
// Covers: timing, file I/O, threads, mutexes, dynamic library loading.
// All tests run on Linux (dev container). Temp files are created in /tmp/
// and cleaned up after each test via RAII helpers.

#include <gtest/gtest.h>
#include "engine/core/platform.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include "common/safe_string.h"
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace pal = uz::platform;

// ============================================================================
// Helpers -- temp file / dir management
// ============================================================================

namespace {

// RAII temp file: creates a unique file in /tmp, removes it on destruction.
struct TempFile {
    std::string path;

    explicit TempFile(const char* suffix = "") {
#ifdef _WIN32
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string tmpl = std::string(tmp) + "uz_pal_test_XXXXXX";
        if (_mktemp_s(tmpl.data(), tmpl.size() + 1) == 0) {
            // Create the file
            FILE* f = uz::file_open(tmpl.c_str(), "wb");
            if (f) {
                fclose(f);
                if (suffix && suffix[0] != '\0') {
                    std::string newPath = tmpl + suffix;
                    rename(tmpl.c_str(), newPath.c_str());
                    path = newPath;
                } else {
                    path = tmpl;
                }
            }
        }
#else
        std::string tmpl = "/tmp/uz_pal_test_XXXXXX";
        // mkstemp needs a mutable char buffer.
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        int fd = mkstemp(buf.data());
        if (fd >= 0) {
            close(fd);
            path = buf.data();
            if (suffix && suffix[0] != '\0') {
                std::string newPath = path + suffix;
                rename(path.c_str(), newPath.c_str());
                path = newPath;
            }
        }
#endif
    }

    ~TempFile() {
        if (!path.empty()) {
            remove(path.c_str());
        }
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

// RAII temp directory: creates a unique dir in /tmp, removes it on destruction.
// Only removes the immediate directory (not recursive rm).
struct TempDir {
    std::string path;

    TempDir() {
#ifdef _WIN32
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string tmpl = std::string(tmp) + "uz_pal_test_dir_XXXXXX";
        if (_mktemp_s(tmpl.data(), tmpl.size() + 1) == 0) {
            if (CreateDirectoryA(tmpl.c_str(), nullptr)) {
                path = tmpl;
            }
        }
#else
        std::string tmpl = "/tmp/uz_pal_test_dir_XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        char* result = mkdtemp(buf.data());
        if (result) {
            path = result;
        }
#endif
    }

    ~TempDir() {
        if (!path.empty()) {
#ifdef _WIN32
            std::string cmd = "rmdir /s /q \"" + path + "\"";
#else
            // Best-effort recursive removal via system command.
            std::string cmd = "rm -rf \"" + path + "\"";
#endif
            (void)system(cmd.c_str());
        }
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

} // anonymous namespace

// ============================================================================
// Timer -- get_time_seconds
// ============================================================================

TEST(PlatformTimerSeconds, ReturnsPositiveValue) {
    const double t = pal::get_time_seconds();
    EXPECT_GT(t, 0.0);
}

TEST(PlatformTimerSeconds, IsMonotonic) {
    const double t1 = pal::get_time_seconds();
    // Small busy spin to ensure measurable time passes.
    volatile int dummy = 0;
    for (int i = 0; i < 10000; ++i) dummy = i;
    const double t2 = pal::get_time_seconds();
    EXPECT_GT(t2, t1);
}

TEST(PlatformTimerSeconds, ElapsedSanity) {
    // The elapsed time between two calls should be > 0 and < 1 second.
    const double t1 = pal::get_time_seconds();
    volatile int dummy = 0;
    for (int i = 0; i < 10000; ++i) dummy = i;
    const double t2 = pal::get_time_seconds();
    const double elapsed = t2 - t1;
    EXPECT_GT(elapsed, 0.0);
    EXPECT_LT(elapsed, 1.0);
}

// ============================================================================
// Timer -- get_time_microseconds
// ============================================================================

TEST(PlatformTimerMicro, ReturnsPositiveValue) {
    const uint64_t t = pal::get_time_microseconds();
    EXPECT_GT(t, 0u);
}

TEST(PlatformTimerMicro, IsMonotonic) {
    const uint64_t t1 = pal::get_time_microseconds();
    volatile int dummy = 0;
    for (int i = 0; i < 10000; ++i) dummy = i;
    const uint64_t t2 = pal::get_time_microseconds();
    EXPECT_GE(t2, t1);
}

// ============================================================================
// File I/O -- path_exists
// ============================================================================

TEST(PlatformPathExists, ReturnsFalseForNonexistent) {
    EXPECT_FALSE(pal::path_exists("/tmp/uz_pal_test_nonexistent_path_12345"));
}

TEST(PlatformPathExists, ReturnsTrueForExistingFile) {
    TempFile f;
    ASSERT_FALSE(f.path.empty());
    EXPECT_TRUE(pal::path_exists(f.path.c_str()));
}

TEST(PlatformPathExists, ReturnsTrueForExistingDirectory) {
    // /tmp always exists on Linux.
    EXPECT_TRUE(pal::path_exists("/tmp"));
}

TEST(PlatformPathExists, ReturnsFalseForNullPath) {
    EXPECT_FALSE(pal::path_exists(nullptr));
}

// ============================================================================
// File I/O -- file_size
// ============================================================================

TEST(PlatformFileSize, ReturnsCorrectSizeForKnownFile) {
    TempFile f;
    ASSERT_FALSE(f.path.empty());

    const char* content = "hello world";
    ASSERT_TRUE(pal::file_write_all(f.path.c_str(), content, 11));

    EXPECT_EQ(pal::file_size(f.path.c_str()), 11u);
}

TEST(PlatformFileSize, ReturnsZeroForNonexistentPath) {
    EXPECT_EQ(pal::file_size("/tmp/uz_pal_test_nonexistent_12345"), 0u);
}

TEST(PlatformFileSize, ReturnsZeroForDirectory) {
    // Directories should return 0, not their inode size.
    EXPECT_EQ(pal::file_size("/tmp"), 0u);
}

TEST(PlatformFileSize, ReturnsZeroForNullPath) {
    EXPECT_EQ(pal::file_size(nullptr), 0u);
}

TEST(PlatformFileSize, ReturnsZeroForEmptyFile) {
    TempFile f;
    ASSERT_FALSE(f.path.empty());
    // File was just created by mkstemp -- should be empty.
    EXPECT_EQ(pal::file_size(f.path.c_str()), 0u);
}

// ============================================================================
// File I/O -- file_write_all + file_read_all roundtrip
// ============================================================================

TEST(PlatformFileIO, WriteAndReadRoundtrip) {
    TempFile f;
    ASSERT_FALSE(f.path.empty());

    const char* data = "hello";
    ASSERT_TRUE(pal::file_write_all(f.path.c_str(), data, 5));

    size_t read_size = 0;
    char* buf = pal::file_read_all(f.path.c_str(), &read_size);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(read_size, 5u);
    EXPECT_EQ(std::memcmp(buf, "hello", 5), 0);
    std::free(buf);
}

TEST(PlatformFileIO, ReadAllReturnsNullptrForNonexistent) {
    size_t read_size = 42; // Should be set to 0.
    char* buf = pal::file_read_all("/tmp/uz_pal_nonexistent_xyz_12345", &read_size);
    EXPECT_EQ(buf, nullptr);
    EXPECT_EQ(read_size, 0u);
}

TEST(PlatformFileIO, ReadAllReturnsNullptrForNullPath) {
    size_t read_size = 42;
    char* buf = pal::file_read_all(nullptr, &read_size);
    EXPECT_EQ(buf, nullptr);
    EXPECT_EQ(read_size, 0u);
}

TEST(PlatformFileIO, ReadAllWithNullOutSize) {
    TempFile f;
    ASSERT_FALSE(f.path.empty());

    const char* data = "test";
    ASSERT_TRUE(pal::file_write_all(f.path.c_str(), data, 4));

    // out_size can be nullptr -- should not crash.
    char* buf = pal::file_read_all(f.path.c_str(), nullptr);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(std::memcmp(buf, "test", 4), 0);
    std::free(buf);
}

TEST(PlatformFileIO, WriteAllReturnsFalseForNullPath) {
    EXPECT_FALSE(pal::file_write_all(nullptr, "data", 4));
}

TEST(PlatformFileIO, WriteAllReturnsFalseForNullDataWithNonzeroSize) {
    TempFile f;
    ASSERT_FALSE(f.path.empty());
    EXPECT_FALSE(pal::file_write_all(f.path.c_str(), nullptr, 10));
}

TEST(PlatformFileIO, WriteAllEmptyFile) {
    TempFile f;
    ASSERT_FALSE(f.path.empty());

    // Writing zero bytes should succeed and create/truncate to empty.
    EXPECT_TRUE(pal::file_write_all(f.path.c_str(), nullptr, 0));
    EXPECT_EQ(pal::file_size(f.path.c_str()), 0u);
}

TEST(PlatformFileIO, WriteAllBinaryData) {
    TempFile f;
    ASSERT_FALSE(f.path.empty());

    // Write data with embedded NULs to verify binary correctness.
    const uint8_t binary[] = {0x00, 0xFF, 0x42, 0x00, 0xDE, 0xAD};
    ASSERT_TRUE(pal::file_write_all(f.path.c_str(), binary, sizeof(binary)));

    size_t read_size = 0;
    char* buf = pal::file_read_all(f.path.c_str(), &read_size);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(read_size, sizeof(binary));
    EXPECT_EQ(std::memcmp(buf, binary, sizeof(binary)), 0);
    std::free(buf);
}

TEST(PlatformFileIO, WriteAllToInvalidPath) {
    // Writing to a path where the directory doesn't exist should fail.
    EXPECT_FALSE(pal::file_write_all("/tmp/uz_pal_nonexistent_dir_xyz/file.bin",
                                      "data", 4));
}

// ============================================================================
// File I/O -- mkdir_recursive
// ============================================================================

TEST(PlatformMkdir, CreatesNestedDirectories) {
    TempDir base;
    ASSERT_FALSE(base.path.empty());

    std::string nested = base.path + "/a/b/c";
    EXPECT_TRUE(pal::mkdir_recursive(nested.c_str()));
    EXPECT_TRUE(pal::path_exists(nested.c_str()));
}

TEST(PlatformMkdir, IdempotentOnExistingDir) {
    TempDir base;
    ASSERT_FALSE(base.path.empty());

    std::string nested = base.path + "/x/y";
    EXPECT_TRUE(pal::mkdir_recursive(nested.c_str()));
    // Second call on the same path should also return true.
    EXPECT_TRUE(pal::mkdir_recursive(nested.c_str()));
}

TEST(PlatformMkdir, ReturnsTrueForAlreadyExistingDir) {
    // /tmp always exists.
    EXPECT_TRUE(pal::mkdir_recursive("/tmp"));
}

TEST(PlatformMkdir, ReturnsFalseForNullPath) {
    EXPECT_FALSE(pal::mkdir_recursive(nullptr));
}

TEST(PlatformMkdir, ReturnsFalseForEmptyString) {
    EXPECT_FALSE(pal::mkdir_recursive(""));
}

TEST(PlatformMkdir, CanWriteFileInCreatedDir) {
    TempDir base;
    ASSERT_FALSE(base.path.empty());

    std::string dir = base.path + "/deep/nested";
    ASSERT_TRUE(pal::mkdir_recursive(dir.c_str()));

    std::string filePath = dir + "/test.txt";
    EXPECT_TRUE(pal::file_write_all(filePath.c_str(), "ok", 2));
    EXPECT_TRUE(pal::path_exists(filePath.c_str()));
}

// ============================================================================
// Thread -- basic creation and join
// ============================================================================

TEST(PlatformThread, CreateJoinIncrementsCounter) {
    std::atomic<int> counter{0};

    auto fn = [](void* ud) {
        auto* c = static_cast<std::atomic<int>*>(ud);
        c->fetch_add(1, std::memory_order_relaxed);
    };

    pal::Thread* t = pal::thread_create(fn, &counter, "test_thread");
    ASSERT_NE(t, nullptr);
    pal::thread_join(t);

    EXPECT_EQ(counter.load(), 1);
}

TEST(PlatformThread, FourThreadsIncrementCounter) {
    std::atomic<int> counter{0};

    auto fn = [](void* ud) {
        auto* c = static_cast<std::atomic<int>*>(ud);
        c->fetch_add(1, std::memory_order_relaxed);
    };

    constexpr int kNumThreads = 4;
    pal::Thread* threads[kNumThreads];

    for (int i = 0; i < kNumThreads; ++i) {
        threads[i] = pal::thread_create(fn, &counter, "worker");
        ASSERT_NE(threads[i], nullptr);
    }

    for (int i = 0; i < kNumThreads; ++i) {
        pal::thread_join(threads[i]);
    }

    EXPECT_EQ(counter.load(), kNumThreads);
}

TEST(PlatformThread, CreateReturnsNullptrForNullFunc) {
    pal::Thread* t = pal::thread_create(nullptr, nullptr, "bad");
    EXPECT_EQ(t, nullptr);
}

TEST(PlatformThread, JoinNullptrIsNoop) {
    // Should not crash.
    pal::thread_join(nullptr);
}

TEST(PlatformThread, DetachNullptrIsNoop) {
    // Should not crash.
    pal::thread_detach(nullptr);
}

TEST(PlatformThread, DetachDoesNotBlock) {
    std::atomic<bool> started{false};
    std::atomic<bool> can_exit{false};

    auto fn = [](void* ud) {
        auto* data = static_cast<std::atomic<bool>*>(ud);
        // Signal we started (data points to started, but we use a simpler approach).
        data[0].store(true, std::memory_order_release);
        // Wait until allowed to exit.
        while (!data[1].load(std::memory_order_acquire)) {
            pal::thread_sleep_ms(1);
        }
    };

    // Pack both atomics into an array for the thread to access.
    std::atomic<bool> flags[2] = {false, false};
    pal::Thread* t = pal::thread_create(fn, flags, "detach_test");
    ASSERT_NE(t, nullptr);

    // Detach should return immediately without blocking.
    pal::thread_detach(t);

    // Signal the thread to exit.
    flags[1].store(true, std::memory_order_release);
    // Give it a moment to clean up.
    pal::thread_sleep_ms(50);
}

TEST(PlatformThread, NullNameIsAccepted) {
    std::atomic<int> counter{0};

    auto fn = [](void* ud) {
        auto* c = static_cast<std::atomic<int>*>(ud);
        c->fetch_add(1, std::memory_order_relaxed);
    };

    pal::Thread* t = pal::thread_create(fn, &counter, nullptr);
    ASSERT_NE(t, nullptr);
    pal::thread_join(t);
    EXPECT_EQ(counter.load(), 1);
}

// ============================================================================
// Thread -- thread_current_id
// ============================================================================

TEST(PlatformThreadId, ReturnsNonZero) {
    EXPECT_NE(pal::thread_current_id(), 0u);
}

TEST(PlatformThreadId, SameOnConsecutiveCalls) {
    const uint64_t id1 = pal::thread_current_id();
    const uint64_t id2 = pal::thread_current_id();
    EXPECT_EQ(id1, id2);
}

TEST(PlatformThreadId, DifferentThreadsDifferentIds) {
    std::atomic<uint64_t> child_id{0};

    auto fn = [](void* ud) {
        auto* id = static_cast<std::atomic<uint64_t>*>(ud);
        id->store(pal::thread_current_id(), std::memory_order_release);
    };

    pal::Thread* t = pal::thread_create(fn, &child_id, "id_test");
    ASSERT_NE(t, nullptr);
    pal::thread_join(t);

    const uint64_t main_id = pal::thread_current_id();
    EXPECT_NE(main_id, child_id.load());
}

// ============================================================================
// Thread -- thread_sleep_ms
// ============================================================================

TEST(PlatformThreadSleep, SleepZeroDoesNotCrash) {
    pal::thread_sleep_ms(0);
}

TEST(PlatformThreadSleep, SleepSmallDuration) {
    const double t1 = pal::get_time_seconds();
    pal::thread_sleep_ms(10);
    const double t2 = pal::get_time_seconds();
    // Should have slept at least ~5ms (some slack for scheduling).
    EXPECT_GT(t2 - t1, 0.005);
}

// ============================================================================
// Mutex -- basic operations
// ============================================================================

TEST(PlatformMutex, CreateReturnsNonNull) {
    pal::Mutex* m = pal::mutex_create();
    ASSERT_NE(m, nullptr);
    pal::mutex_destroy(m);
}

TEST(PlatformMutex, LockUnlockNoCrash) {
    pal::Mutex* m = pal::mutex_create();
    ASSERT_NE(m, nullptr);

    pal::mutex_lock(m);
    pal::mutex_unlock(m);
    pal::mutex_destroy(m);
}

TEST(PlatformMutex, TryLockReturnsTrueWhenUnlocked) {
    pal::Mutex* m = pal::mutex_create();
    ASSERT_NE(m, nullptr);

    EXPECT_TRUE(pal::mutex_try_lock(m));
    pal::mutex_unlock(m);
    pal::mutex_destroy(m);
}

TEST(PlatformMutex, TryLockReturnsFalseWhenLockedByAnotherThread) {
    pal::Mutex* m = pal::mutex_create();
    ASSERT_NE(m, nullptr);

    // Lock from main thread.
    pal::mutex_lock(m);

    std::atomic<bool> result{true}; // Default to true; child should set to false.

    auto fn = [](void* ud) {
        // ud points to a struct with {Mutex*, atomic<bool>*}
        auto** ptrs = static_cast<void**>(ud);
        auto* mtx = static_cast<pal::Mutex*>(ptrs[0]);
        auto* flag = static_cast<std::atomic<bool>*>(ptrs[1]);
        flag->store(pal::mutex_try_lock(mtx), std::memory_order_release);
        // If we did acquire it (shouldn't happen), unlock to avoid deadlock.
        if (flag->load(std::memory_order_acquire)) {
            pal::mutex_unlock(mtx);
        }
    };

    void* args[2] = {m, &result};
    pal::Thread* t = pal::thread_create(fn, args, "trylock");
    ASSERT_NE(t, nullptr);
    pal::thread_join(t);

    // The child thread should have failed to acquire the lock.
    EXPECT_FALSE(result.load());

    pal::mutex_unlock(m);
    pal::mutex_destroy(m);
}

TEST(PlatformMutex, LockUnlock1000TimesNoCrash) {
    pal::Mutex* m = pal::mutex_create();
    ASSERT_NE(m, nullptr);

    for (int i = 0; i < 1000; ++i) {
        pal::mutex_lock(m);
        pal::mutex_unlock(m);
    }

    pal::mutex_destroy(m);
}

TEST(PlatformMutex, DestroyNullptrIsNoop) {
    // Should not crash.
    pal::mutex_destroy(nullptr);
}

TEST(PlatformMutex, LockNullptrIsNoop) {
    // Should not crash.
    pal::mutex_lock(nullptr);
}

TEST(PlatformMutex, UnlockNullptrIsNoop) {
    // Should not crash.
    pal::mutex_unlock(nullptr);
}

TEST(PlatformMutex, TryLockNullptrReturnsFalse) {
    EXPECT_FALSE(pal::mutex_try_lock(nullptr));
}

TEST(PlatformMutex, ProtectsSharedData) {
    // Verify mutual exclusion actually works by having threads race on a counter.
    pal::Mutex* m = pal::mutex_create();
    ASSERT_NE(m, nullptr);

    struct SharedData {
        pal::Mutex* mtx;
        int counter;
    };
    SharedData shared{m, 0};

    auto fn = [](void* ud) {
        auto* sd = static_cast<SharedData*>(ud);
        for (int i = 0; i < 10000; ++i) {
            pal::mutex_lock(sd->mtx);
            sd->counter++;
            pal::mutex_unlock(sd->mtx);
        }
    };

    constexpr int kNumThreads = 4;
    pal::Thread* threads[kNumThreads];
    for (int i = 0; i < kNumThreads; ++i) {
        threads[i] = pal::thread_create(fn, &shared, "mutex_race");
        ASSERT_NE(threads[i], nullptr);
    }
    for (int i = 0; i < kNumThreads; ++i) {
        pal::thread_join(threads[i]);
    }

    EXPECT_EQ(shared.counter, kNumThreads * 10000);
    pal::mutex_destroy(m);
}

// ============================================================================
// Dynamic library loading
// ============================================================================

TEST(PlatformDynLib, OpenNullptrReturnsNullptr) {
    pal::DynLib* lib = pal::dynlib_open(nullptr);
    EXPECT_EQ(lib, nullptr);
}

TEST(PlatformDynLib, OpenNonexistentReturnsNullptr) {
    pal::DynLib* lib = pal::dynlib_open("/tmp/uz_nonexistent_lib.so");
    EXPECT_EQ(lib, nullptr);
}

TEST(PlatformDynLib, CloseNullptrIsNoop) {
    // Should not crash.
    pal::dynlib_close(nullptr);
}

TEST(PlatformDynLib, SymbolNullLibReturnsNullptr) {
    EXPECT_EQ(pal::dynlib_symbol(nullptr, "malloc"), nullptr);
}

TEST(PlatformDynLib, SymbolNullNameReturnsNullptr) {
    // Open a real library to test null symbol name.
    pal::DynLib* lib = pal::dynlib_open("libc.so.6");
    if (lib) {
        EXPECT_EQ(pal::dynlib_symbol(lib, nullptr), nullptr);
        pal::dynlib_close(lib);
    }
    // If libc.so.6 is not available (e.g., musl), skip gracefully.
}

TEST(PlatformDynLib, OpenRealLibAndFindSymbol) {
    // libc.so.6 is available on glibc-based Linux systems.
    pal::DynLib* lib = pal::dynlib_open("libc.so.6");
    if (!lib) {
        // Try libm.so.6 as fallback.
        lib = pal::dynlib_open("libm.so.6");
    }
    if (!lib) {
        GTEST_SKIP() << "No suitable shared library found for dynlib test";
    }

    // "malloc" should be a valid symbol in libc.
    void* sym = pal::dynlib_symbol(lib, "malloc");
    EXPECT_NE(sym, nullptr);

    // A nonexistent symbol should return nullptr.
    void* bad = pal::dynlib_symbol(lib, "uz_nonexistent_symbol_xyz_999");
    EXPECT_EQ(bad, nullptr);

    pal::dynlib_close(lib);
}

TEST(PlatformDynLib, OpenLibdlAndFindDlopen) {
    // libdl.so.2 should exist on most Linux distros.
    pal::DynLib* lib = pal::dynlib_open("libdl.so.2");
    if (!lib) {
        GTEST_SKIP() << "libdl.so.2 not available";
    }

    // dlopen itself should be a symbol in libdl.
    void* sym = pal::dynlib_symbol(lib, "dlopen");
    EXPECT_NE(sym, nullptr);

    pal::dynlib_close(lib);
}

// ============================================================================
// Integration: thread + mutex + file I/O
// ============================================================================

TEST(PlatformIntegration, ThreadWritesFile) {
    TempFile f;
    ASSERT_FALSE(f.path.empty());

    struct WriteData {
        const char* path;
        const char* content;
        size_t size;
    };
    WriteData wd{f.path.c_str(), "thread_wrote_this", 17};

    auto fn = [](void* ud) {
        auto* w = static_cast<WriteData*>(ud);
        pal::file_write_all(w->path, w->content, w->size);
    };

    pal::Thread* t = pal::thread_create(fn, &wd, "writer");
    ASSERT_NE(t, nullptr);
    pal::thread_join(t);

    size_t read_size = 0;
    char* buf = pal::file_read_all(f.path.c_str(), &read_size);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(read_size, 17u);
    EXPECT_EQ(std::memcmp(buf, "thread_wrote_this", 17), 0);
    std::free(buf);
}
