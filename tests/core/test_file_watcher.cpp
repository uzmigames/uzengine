// test_file_watcher.cpp -- Google Test coverage for FileWatcher.
//
// Tests the inotify backend on Linux. Uses temp directories in /tmp/
// with RAII cleanup. File-change detection tests poll with short timeouts
// to avoid flaky CI, but still exercise the real inotify codepath.

#include <gtest/gtest.h>
#include "engine/core/file_watcher.h"
#include "engine/core/platform.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// RAII temp directory helper
// ============================================================================

namespace {

struct TempDir {
    std::string path;

    TempDir() {
        std::string tmpl = "/tmp/uz_fw_test_XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        char* result = mkdtemp(buf.data());
        if (result) {
            path = result;
        }
    }

    ~TempDir() {
        if (!path.empty()) {
            std::string cmd = "rm -rf \"" + path + "\"";
            (void)system(cmd.c_str());
        }
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

/// Write content to a file, creating or truncating it.
void write_file(const std::string& path, const std::string& content) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
}

/// Wait for an atomic counter to reach at least |target|, polling up to |timeout_ms|.
bool wait_for_count(const std::atomic<int>& counter, int target, int timeout_ms = 3000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (counter.load(std::memory_order_acquire) < target) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

} // anonymous namespace

// ============================================================================
// FileWatcher tests
// ============================================================================

class FileWatcherTest : public ::testing::Test {
protected:
    TempDir tmp_dir_;
};

// -- Constructor / default state --

TEST_F(FileWatcherTest, Constructor_IsNotWatching) {
    uz::FileWatcher watcher;
    EXPECT_FALSE(watcher.is_watching());
}

TEST_F(FileWatcherTest, DestructorWithoutWatch_NoCrash) {
    // Just construct and destroy -- no watch() call.
    uz::FileWatcher watcher;
}

// -- watch() failure cases --

TEST_F(FileWatcherTest, Watch_NullDirectory_ReturnsFalse) {
    uz::FileWatcher watcher;
    bool result = watcher.watch(nullptr, [](const uz::FileChange&) {});
    EXPECT_FALSE(result);
    EXPECT_FALSE(watcher.is_watching());
}

TEST_F(FileWatcherTest, Watch_NullCallback_ReturnsFalse) {
    uz::FileWatcher watcher;
    bool result = watcher.watch(tmp_dir_.path.c_str(), nullptr);
    EXPECT_FALSE(result);
    EXPECT_FALSE(watcher.is_watching());
}

TEST_F(FileWatcherTest, Watch_NonexistentDir_ReturnsFalse) {
    uz::FileWatcher watcher;
    bool result = watcher.watch("/tmp/uz_this_dir_does_not_exist_42", [](const uz::FileChange&) {});
    EXPECT_FALSE(result);
    EXPECT_FALSE(watcher.is_watching());
}

// -- watch() success --

TEST_F(FileWatcherTest, Watch_ValidDir_ReturnsTrue) {
    uz::FileWatcher watcher;
    bool result = watcher.watch(tmp_dir_.path.c_str(), [](const uz::FileChange&) {});
    EXPECT_TRUE(result);
    EXPECT_TRUE(watcher.is_watching());
    watcher.stop();
}

// -- stop() --

TEST_F(FileWatcherTest, Stop_SetsNotWatching) {
    uz::FileWatcher watcher;
    watcher.watch(tmp_dir_.path.c_str(), [](const uz::FileChange&) {});
    ASSERT_TRUE(watcher.is_watching());
    watcher.stop();
    EXPECT_FALSE(watcher.is_watching());
}

TEST_F(FileWatcherTest, Stop_CalledTwice_NoCrash) {
    uz::FileWatcher watcher;
    watcher.watch(tmp_dir_.path.c_str(), [](const uz::FileChange&) {});
    watcher.stop();
    watcher.stop();  // Should not crash
    EXPECT_FALSE(watcher.is_watching());
}

TEST_F(FileWatcherTest, Stop_WithoutWatch_NoCrash) {
    uz::FileWatcher watcher;
    watcher.stop();  // Never called watch()
    EXPECT_FALSE(watcher.is_watching());
}

// -- set_debounce_ms --

TEST_F(FileWatcherTest, SetDebounce_BeforeWatch_NoCrash) {
    uz::FileWatcher watcher;
    watcher.set_debounce_ms(50);
    // No assertion needed -- just verifying it doesn't crash
}

// -- File modification detection --

TEST_F(FileWatcherTest, FileModification_Detected) {
    // Create a file before watching
    const std::string file_path = tmp_dir_.path + "/test.hlsl";
    write_file(file_path, "initial content");

    std::atomic<int> count{0};
    std::string last_path;

    uz::FileWatcher watcher;
    watcher.set_debounce_ms(0);  // Disable debounce for test reliability
    watcher.watch(tmp_dir_.path.c_str(), [&](const uz::FileChange& change) {
        last_path = change.path;
        count.fetch_add(1, std::memory_order_release);
    });

    // Small delay to let the watcher thread start and inotify stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Modify the file
    write_file(file_path, "modified content");

    ASSERT_TRUE(wait_for_count(count, 1)) << "Timeout waiting for modification event";
    EXPECT_NE(last_path.find("test.hlsl"), std::string::npos);
    watcher.stop();
}

// -- File creation detection --

TEST_F(FileWatcherTest, FileCreation_Detected) {
    std::atomic<int> count{0};
    uz::FileWatchEvent last_event = uz::FileWatchEvent::Modified;

    uz::FileWatcher watcher;
    watcher.set_debounce_ms(0);
    watcher.watch(tmp_dir_.path.c_str(), [&](const uz::FileChange& change) {
        last_event = change.event;
        count.fetch_add(1, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Create a new file
    const std::string file_path = tmp_dir_.path + "/new_shader.hlsl";
    write_file(file_path, "new shader source");

    ASSERT_TRUE(wait_for_count(count, 1)) << "Timeout waiting for creation event";
    // inotify may fire IN_CREATE or IN_CLOSE_WRITE depending on timing; both are valid
    EXPECT_TRUE(last_event == uz::FileWatchEvent::Created ||
                last_event == uz::FileWatchEvent::Modified);
    watcher.stop();
}

// -- File deletion detection --

TEST_F(FileWatcherTest, FileDeletion_Detected) {
    const std::string file_path = tmp_dir_.path + "/to_delete.hlsl";
    write_file(file_path, "will be deleted");

    std::atomic<int> delete_count{0};

    uz::FileWatcher watcher;
    watcher.set_debounce_ms(0);
    watcher.watch(tmp_dir_.path.c_str(), [&](const uz::FileChange& change) {
        if (change.event == uz::FileWatchEvent::Deleted) {
            delete_count.fetch_add(1, std::memory_order_release);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Delete the file
    std::remove(file_path.c_str());

    ASSERT_TRUE(wait_for_count(delete_count, 1)) << "Timeout waiting for deletion event";
    watcher.stop();
}

// -- Debounce behavior --

TEST_F(FileWatcherTest, Debounce_RapidWrites_CollapsedToFewer) {
    const std::string file_path = tmp_dir_.path + "/debounce_test.txt";
    write_file(file_path, "initial");

    std::atomic<int> count{0};

    uz::FileWatcher watcher;
    watcher.set_debounce_ms(300);  // 300 ms debounce
    watcher.watch(tmp_dir_.path.c_str(), [&](const uz::FileChange&) {
        count.fetch_add(1, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Rapidly write 10 times with no delay between writes
    for (int i = 0; i < 10; ++i) {
        write_file(file_path, "content " + std::to_string(i));
    }

    // Wait a bit longer than the debounce window for all events to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // With 300ms debounce and rapid writes, we should see far fewer than 10 callbacks.
    // The exact number depends on inotify coalescing + our debounce, but it should
    // be significantly less than the 10 writes.
    const int final_count = count.load(std::memory_order_acquire);
    EXPECT_GT(final_count, 0) << "At least one event should have fired";
    EXPECT_LE(final_count, 5) << "Debounce should collapse most of the 10 rapid writes";

    watcher.stop();
}

// -- Multiple extensions --

TEST_F(FileWatcherTest, MultipleFiles_AllDetected) {
    std::atomic<int> count{0};

    uz::FileWatcher watcher;
    watcher.set_debounce_ms(0);
    watcher.watch(tmp_dir_.path.c_str(), [&](const uz::FileChange&) {
        count.fetch_add(1, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Create files with different extensions
    write_file(tmp_dir_.path + "/shader.hlsl", "hlsl code");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    write_file(tmp_dir_.path + "/config.json", "{}");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    write_file(tmp_dir_.path + "/readme.txt", "text");

    ASSERT_TRUE(wait_for_count(count, 3)) << "All 3 file creations should be detected";

    watcher.stop();
}

// -- Rewatch after stop --

TEST_F(FileWatcherTest, RewatchAfterStop_Works) {
    std::atomic<int> count{0};

    uz::FileWatcher watcher;
    watcher.set_debounce_ms(0);

    // First watch
    watcher.watch(tmp_dir_.path.c_str(), [&](const uz::FileChange&) {
        count.fetch_add(1, std::memory_order_release);
    });
    ASSERT_TRUE(watcher.is_watching());
    watcher.stop();
    ASSERT_FALSE(watcher.is_watching());

    // Second watch (rewatch)
    watcher.watch(tmp_dir_.path.c_str(), [&](const uz::FileChange&) {
        count.fetch_add(1, std::memory_order_release);
    });
    EXPECT_TRUE(watcher.is_watching());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    write_file(tmp_dir_.path + "/rewatch_test.txt", "data");
    ASSERT_TRUE(wait_for_count(count, 1)) << "Events should fire after rewatch";

    watcher.stop();
}

// -- Destructor cleans up --

TEST_F(FileWatcherTest, DestructorWhileWatching_StopsCleanly) {
    {
        uz::FileWatcher watcher;
        watcher.watch(tmp_dir_.path.c_str(), [](const uz::FileChange&) {});
        ASSERT_TRUE(watcher.is_watching());
        // Destructor should call stop() implicitly
    }
    // If we get here without hanging, the test passes
}

// -- Watch while already watching (implicit stop) --

TEST_F(FileWatcherTest, Watch_WhileAlreadyWatching_ImplicitStop) {
    std::atomic<int> count1{0};
    std::atomic<int> count2{0};

    uz::FileWatcher watcher;
    watcher.set_debounce_ms(0);

    // Start watching with first callback
    watcher.watch(tmp_dir_.path.c_str(), [&](const uz::FileChange&) {
        count1.fetch_add(1, std::memory_order_release);
    });
    ASSERT_TRUE(watcher.is_watching());

    // Create a second temp dir for the second watch
    TempDir tmp_dir2;
    ASSERT_FALSE(tmp_dir2.path.empty());

    // Watch second directory -- should implicitly stop the first
    watcher.watch(tmp_dir2.path.c_str(), [&](const uz::FileChange&) {
        count2.fetch_add(1, std::memory_order_release);
    });
    EXPECT_TRUE(watcher.is_watching());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Write to second dir -- should trigger second callback
    write_file(tmp_dir2.path + "/second.txt", "data");
    ASSERT_TRUE(wait_for_count(count2, 1));

    watcher.stop();
}

// -- FileChange path contains directory --

TEST_F(FileWatcherTest, FileChangePath_ContainsDirectoryPrefix) {
    std::string received_path;
    std::atomic<int> count{0};

    uz::FileWatcher watcher;
    watcher.set_debounce_ms(0);
    watcher.watch(tmp_dir_.path.c_str(), [&](const uz::FileChange& change) {
        received_path = change.path;
        count.fetch_add(1, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    write_file(tmp_dir_.path + "/pathcheck.hlsl", "code");

    ASSERT_TRUE(wait_for_count(count, 1));
    // The path should contain the directory and the filename
    EXPECT_NE(received_path.find(tmp_dir_.path), std::string::npos);
    EXPECT_NE(received_path.find("pathcheck.hlsl"), std::string::npos);

    watcher.stop();
}

// -- FileWatchEvent enum values --

TEST(FileWatchEventTest, EnumValues_AreDistinct) {
    EXPECT_EQ(static_cast<uint8_t>(uz::FileWatchEvent::Modified), 0);
    EXPECT_EQ(static_cast<uint8_t>(uz::FileWatchEvent::Created),  1);
    EXPECT_EQ(static_cast<uint8_t>(uz::FileWatchEvent::Deleted),  2);
}

// -- FileChange struct --

TEST(FileChangeTest, DefaultConstruction) {
    uz::FileChange change;
    EXPECT_TRUE(change.path.empty());
    // event is uninitialized but struct should be constructible
}

TEST(FileChangeTest, AssignFields) {
    uz::FileChange change;
    change.path  = "/shaders/test.hlsl";
    change.event = uz::FileWatchEvent::Modified;
    EXPECT_EQ(change.path, "/shaders/test.hlsl");
    EXPECT_EQ(change.event, uz::FileWatchEvent::Modified);
}
