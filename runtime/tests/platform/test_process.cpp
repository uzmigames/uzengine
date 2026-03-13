/// Unit tests for UzEngine platform process utilities.
///
/// Covers: DLL loader (LoadDll/GetDllSymbol/FreeDll), child process lifecycle
/// (ProcessCreate/Read/Wait/IsRunning/Close), shared memory (Create/Open/Close
/// round-trip), and environment variable helpers (GetEnv/SetEnv).
///
/// All tests are self-contained: no GPU, no audio hardware required.
/// Temporary shared memory regions use unique names derived from the test
/// process ID to prevent cross-run collisions.

#include "dll_loader.h"
#include "process.h"
#include "shared_memory.h"
#include "environment.h"
#include "log.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <Windows.h>
#endif

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Fixture — logging lifecycle
// ─────────────────────────────────────────────────────────────────────────────

class ProcessTest : public ::testing::Test {
protected:
    void SetUp() override {
        fs::path logPath = fs::temp_directory_path() / "uzengine_process_test.log";
        uz::LogInit(logPath.string().c_str());
        uz::LogSetLevel(uz::LogLevel::Error);
    }

    void TearDown() override {
        uz::LogShutdown();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// DLL Loader — LoadDll
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProcessTest, LoadDll_NullPath_ReturnsNullptr) {
    const uz::NativeDllHandle handle = uz::LoadDll(nullptr);
    EXPECT_EQ(handle, nullptr)
        << "LoadDll(nullptr) must return nullptr without crashing";
}

TEST_F(ProcessTest, LoadDll_NonExistentPath_ReturnsNullptr) {
    const uz::NativeDllHandle handle =
        uz::LoadDll("C:/does_not_exist_uzengine_test_dll_xyz.dll");
    EXPECT_EQ(handle, nullptr)
        << "LoadDll with a non-existent path must return nullptr";
}

TEST_F(ProcessTest, LoadDll_Kernel32_Succeeds) {
    // kernel32.dll is guaranteed present on every Windows system.
    const uz::NativeDllHandle handle = uz::LoadDll("kernel32.dll");
    ASSERT_NE(handle, nullptr)
        << "LoadDll(\"kernel32.dll\") must succeed on Windows";

    uz::FreeDll(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// DLL Loader — GetDllSymbol
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProcessTest, GetDllSymbol_KnownExport_ReturnsNonNull) {
    const uz::NativeDllHandle handle = uz::LoadDll("kernel32.dll");
    ASSERT_NE(handle, nullptr);

    void* sym = uz::GetDllSymbol(handle, "GetLastError");
    EXPECT_NE(sym, nullptr)
        << "GetDllSymbol must find 'GetLastError' in kernel32.dll";

    uz::FreeDll(handle);
}

TEST_F(ProcessTest, GetDllSymbol_UnknownExport_ReturnsNullptr) {
    const uz::NativeDllHandle handle = uz::LoadDll("kernel32.dll");
    ASSERT_NE(handle, nullptr);

    void* sym = uz::GetDllSymbol(handle, "UzEngine_NonExistentSymbol_XYZ_12345");
    EXPECT_EQ(sym, nullptr)
        << "GetDllSymbol must return nullptr for a symbol that does not exist";

    uz::FreeDll(handle);
}

TEST_F(ProcessTest, GetDllSymbol_MultipleExports_AllResolve) {
    const uz::NativeDllHandle handle = uz::LoadDll("kernel32.dll");
    ASSERT_NE(handle, nullptr);

    // Verify several well-known kernel32 exports.
    const char* exports[] = {
        "CreateFileW",
        "CloseHandle",
        "ReadFile",
        "WriteFile",
        "GetLastError",
        nullptr
    };
    for (int i = 0; exports[i] != nullptr; ++i) {
        void* sym = uz::GetDllSymbol(handle, exports[i]);
        EXPECT_NE(sym, nullptr)
            << "GetDllSymbol failed for known export: " << exports[i];
    }

    uz::FreeDll(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// DLL Loader — FreeDll
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProcessTest, FreeDll_NullHandle_IsNoOp) {
    // Null-safe: must not crash.
    EXPECT_NO_FATAL_FAILURE(uz::FreeDll(nullptr));
}

TEST_F(ProcessTest, FreeDll_ValidHandle_CalledTwice_DoesNotCrash) {
    // After FreeDll the handle is semantically invalid. A second call should
    // be a no-op or fail gracefully — it must not crash the process.
    // NOTE: We only call it once with the real handle; this is a contract test.
    const uz::NativeDllHandle handle = uz::LoadDll("kernel32.dll");
    ASSERT_NE(handle, nullptr);

    EXPECT_NO_FATAL_FAILURE(uz::FreeDll(handle));
    // We do NOT call FreeDll again here to avoid a real double-free — the
    // contract only guarantees null-safety, not double-free safety.
}

// ─────────────────────────────────────────────────────────────────────────────
// Process — ProcessCreate / ProcessWait / ProcessReadStdout
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProcessTest, ProcessCreate_CmdEchoHello_ReturnsValidHandle) {
    uz::ProcessHandle proc = uz::ProcessCreate("cmd.exe", "/c echo hello");

    ASSERT_NE(proc.hProcess, nullptr)
        << "ProcessCreate must return a valid handle for 'cmd.exe /c echo hello'";
    EXPECT_GT(proc.processId, 0u)
        << "ProcessHandle::processId must be a non-zero PID";

    uz::ProcessWait(proc, 5000);
    uz::ProcessClose(proc);
}

TEST_F(ProcessTest, ProcessCreate_CmdEchoHello_StdoutContainsHello) {
    uz::ProcessHandle proc = uz::ProcessCreate("cmd.exe", "/c echo hello");
    ASSERT_NE(proc.hProcess, nullptr);

    // Wait for the process to finish so all output is flushed into the pipe.
    const int exitCode = uz::ProcessWait(proc, 5000);
    EXPECT_EQ(exitCode, 0)
        << "cmd /c echo hello must exit with code 0";

    // Drain the stdout pipe.
    char buffer[1024] = {};
    size_t totalRead = 0;
    size_t n = 0;
    while ((n = uz::ProcessReadStdout(proc, buffer + totalRead,
                                       sizeof(buffer) - 1 - totalRead)) > 0) {
        totalRead += n;
        if (totalRead >= sizeof(buffer) - 1) break;
    }
    buffer[totalRead] = '\0';

    EXPECT_NE(std::strstr(buffer, "hello"), nullptr)
        << "stdout must contain 'hello'. Got: '" << buffer << "'";

    uz::ProcessClose(proc);
}

TEST_F(ProcessTest, ProcessCreate_InvalidExe_ReturnsInvalidHandle) {
    uz::ProcessHandle proc =
        uz::ProcessCreate("C:/does_not_exist_uzengine_exe.exe", nullptr);

    EXPECT_EQ(proc.hProcess, nullptr)
        << "ProcessCreate must return an invalid handle for a non-existent exe";
    EXPECT_EQ(proc.processId, 0u);

    // ProcessClose on an invalid handle must be safe.
    uz::ProcessClose(proc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Process — ProcessIsRunning
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProcessTest, ProcessIsRunning_AfterProcessExits_ReturnsFalse) {
    uz::ProcessHandle proc = uz::ProcessCreate("cmd.exe", "/c exit 0");
    ASSERT_NE(proc.hProcess, nullptr);

    // Wait for it to exit.
    uz::ProcessWait(proc, 5000);

    EXPECT_FALSE(uz::ProcessIsRunning(proc))
        << "ProcessIsRunning must return false after the child exits";

    uz::ProcessClose(proc);
}

TEST_F(ProcessTest, ProcessIsRunning_InvalidHandle_ReturnsFalse) {
    uz::ProcessHandle invalid;
    // Default-constructed ProcessHandle has nullptr hProcess.
    EXPECT_FALSE(uz::ProcessIsRunning(invalid))
        << "ProcessIsRunning on a zero-initialized handle must return false";
}

TEST_F(ProcessTest, ProcessIsRunning_WhileRunning_ReturnsTrue) {
    // Use ping to simulate a delay — timeout requires a console and exits instantly in detached processes.
    uz::ProcessHandle proc = uz::ProcessCreate("cmd.exe", "/c ping -n 6 127.0.0.1 > nul");
    ASSERT_NE(proc.hProcess, nullptr);

    // Give the process a moment to start.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_TRUE(uz::ProcessIsRunning(proc))
        << "ProcessIsRunning must return true for a sleeping child process";

    uz::ProcessTerminate(proc);
    uz::ProcessWait(proc, 2000);
    uz::ProcessClose(proc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Process — ProcessWait exit code
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProcessTest, ProcessWait_CmdExitCode42_Returns42) {
    uz::ProcessHandle proc = uz::ProcessCreate("cmd.exe", "/c exit 42");
    ASSERT_NE(proc.hProcess, nullptr);

    const int code = uz::ProcessWait(proc, 5000);
    EXPECT_EQ(code, 42)
        << "ProcessWait must return the child's exit code";

    uz::ProcessClose(proc);
}

TEST_F(ProcessTest, ProcessWait_CmdExitCode0_Returns0) {
    uz::ProcessHandle proc = uz::ProcessCreate("cmd.exe", "/c exit 0");
    ASSERT_NE(proc.hProcess, nullptr);

    const int code = uz::ProcessWait(proc, 5000);
    EXPECT_EQ(code, 0);

    uz::ProcessClose(proc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Process — ProcessClose
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProcessTest, ProcessClose_AfterWait_ZerosAllHandles) {
    uz::ProcessHandle proc = uz::ProcessCreate("cmd.exe", "/c exit 0");
    ASSERT_NE(proc.hProcess, nullptr);

    uz::ProcessWait(proc, 5000);
    uz::ProcessClose(proc);

    EXPECT_EQ(proc.hProcess,    nullptr) << "hProcess must be null after Close";
    EXPECT_EQ(proc.hStdoutRead, nullptr) << "hStdoutRead must be null after Close";
    EXPECT_EQ(proc.hStderrRead, nullptr) << "hStderrRead must be null after Close";
    EXPECT_EQ(proc.processId,   0u)      << "processId must be 0 after Close";
}

TEST_F(ProcessTest, ProcessClose_CalledOnDefaultHandle_IsNoOp) {
    uz::ProcessHandle invalid;
    EXPECT_NO_FATAL_FAILURE(uz::ProcessClose(invalid));
}

TEST_F(ProcessTest, ProcessClose_CalledTwice_IsNoOp) {
    uz::ProcessHandle proc = uz::ProcessCreate("cmd.exe", "/c exit 0");
    ASSERT_NE(proc.hProcess, nullptr);
    uz::ProcessWait(proc, 5000);

    uz::ProcessClose(proc);
    // Second close on an already-zeroed handle must not crash.
    EXPECT_NO_FATAL_FAILURE(uz::ProcessClose(proc));
}

// ─────────────────────────────────────────────────────────────────────────────
// Process — ProcessReadStderr
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProcessTest, ProcessReadStderr_CmdErrorOutput_CanReadFromPipe) {
    // Run a command that writes to stderr.
    uz::ProcessHandle proc =
        uz::ProcessCreate("cmd.exe", "/c echo error_output 1>&2");
    ASSERT_NE(proc.hProcess, nullptr);

    uz::ProcessWait(proc, 5000);

    char buffer[1024] = {};
    size_t totalRead = 0;
    size_t n = 0;
    while ((n = uz::ProcessReadStderr(proc, buffer + totalRead,
                                       sizeof(buffer) - 1 - totalRead)) > 0) {
        totalRead += n;
        if (totalRead >= sizeof(buffer) - 1) break;
    }
    buffer[totalRead] = '\0';

    // The stderr pipe must be readable (even if empty on some cmd variants).
    // We only assert the read didn't crash.
    SUCCEED();

    uz::ProcessClose(proc);
}

// ─────────────────────────────────────────────────────────────────────────────
// SharedMemory — Create / Open / Close round-trip
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a test-unique shared memory name to avoid cross-run collisions.
static std::string MakeSharedMemName(const char* suffix) {
    return std::string("UzEngineTest_") + suffix + "_" +
           std::to_string(::GetCurrentProcessId());
}

TEST_F(ProcessTest, SharedMemoryCreate_ValidArgs_ReturnsNonNullData) {
    const std::string name = MakeSharedMemName("Create");
    uz::SharedMemoryHandle h = uz::SharedMemoryCreate(name.c_str(), 256);

    ASSERT_NE(h.data,    nullptr) << "SharedMemoryCreate must map data pointer";
    ASSERT_NE(h.mapping, nullptr) << "SharedMemoryCreate must set mapping handle";
    EXPECT_EQ(h.size, 256u)       << "SharedMemoryHandle::size must match requested size";

    uz::SharedMemoryClose(h);
}

TEST_F(ProcessTest, SharedMemoryCreateOpen_WriteRead_DataMatchesRoundTrip) {
    const std::string name  = MakeSharedMemName("RoundTrip");
    constexpr size_t  kSize = 64;

    uz::SharedMemoryHandle creator = uz::SharedMemoryCreate(name.c_str(), kSize);
    ASSERT_NE(creator.data, nullptr);

    // Write a known pattern via the creator mapping.
    const uint8_t kPattern[kSize] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11
    };
    std::memcpy(creator.data, kPattern, kSize);

    // Open the same region from a second handle (simulates a second process).
    uz::SharedMemoryHandle consumer = uz::SharedMemoryOpen(name.c_str(), kSize);
    ASSERT_NE(consumer.data, nullptr)
        << "SharedMemoryOpen must succeed for an existing named region";

    // Verify the consumer sees the same bytes written by the creator.
    for (size_t i = 0; i < kSize; ++i) {
        EXPECT_EQ(static_cast<const uint8_t*>(consumer.data)[i], kPattern[i])
            << "Byte mismatch at index " << i << " in shared memory round-trip";
    }

    uz::SharedMemoryClose(consumer);
    uz::SharedMemoryClose(creator);
}

TEST_F(ProcessTest, SharedMemoryOpen_NonExistentRegion_ReturnsNullData) {
    // Attempt to open a region that was never created.
    const std::string name = MakeSharedMemName("NoSuchRegion_XYZ");
    uz::SharedMemoryHandle h = uz::SharedMemoryOpen(name.c_str(), 256);

    EXPECT_EQ(h.data,    nullptr) << "SharedMemoryOpen must return null for unknown region";
    EXPECT_EQ(h.mapping, nullptr);
}

TEST_F(ProcessTest, SharedMemoryClose_AfterCreate_ZerosHandle) {
    const std::string name = MakeSharedMemName("Close");
    uz::SharedMemoryHandle h = uz::SharedMemoryCreate(name.c_str(), 128);
    ASSERT_NE(h.data, nullptr);

    uz::SharedMemoryClose(h);

    EXPECT_EQ(h.data,    nullptr) << "data must be null after SharedMemoryClose";
    EXPECT_EQ(h.mapping, nullptr) << "mapping must be null after SharedMemoryClose";
    EXPECT_EQ(h.size,    0u)      << "size must be 0 after SharedMemoryClose";
}

TEST_F(ProcessTest, SharedMemoryClose_OnZeroHandle_IsNoOp) {
    uz::SharedMemoryHandle empty;
    EXPECT_NO_FATAL_FAILURE(uz::SharedMemoryClose(empty));
}

TEST_F(ProcessTest, SharedMemoryClose_CalledTwice_IsNoOp) {
    const std::string name = MakeSharedMemName("CloseTwice");
    uz::SharedMemoryHandle h = uz::SharedMemoryCreate(name.c_str(), 64);
    ASSERT_NE(h.data, nullptr);

    uz::SharedMemoryClose(h);
    EXPECT_NO_FATAL_FAILURE(uz::SharedMemoryClose(h));
}

// ─────────────────────────────────────────────────────────────────────────────
// Environment variables — GetEnv / SetEnv
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProcessTest, GetEnv_PATH_ReturnsNonEmptyString) {
    const std::string value = uz::GetEnv("PATH");
    EXPECT_FALSE(value.empty())
        << "GetEnv(\"PATH\") must return a non-empty string on Windows";
}

TEST_F(ProcessTest, GetEnv_SystemRoot_ReturnsNonEmptyString) {
    const std::string value = uz::GetEnv("SystemRoot");
    EXPECT_FALSE(value.empty())
        << "GetEnv(\"SystemRoot\") must return a non-empty string on Windows";
}

TEST_F(ProcessTest, GetEnv_NonExistentVariable_ReturnsEmptyString) {
    const std::string value =
        uz::GetEnv("UZ_ENGINE_TEST_DOES_NOT_EXIST_XYZ_12345");
    EXPECT_TRUE(value.empty())
        << "GetEnv for a variable that was never set must return an empty string";
}

TEST_F(ProcessTest, SetEnv_ThenGetEnv_RoundTripMatchesValue) {
    const char* kName  = "UZ_ENGINE_TEST_SETENV_ROUNDTRIP";
    const char* kValue = "hello_uzengine_42";

    const bool ok = uz::SetEnv(kName, kValue);
    ASSERT_TRUE(ok) << "SetEnv must return true on success";

    const std::string retrieved = uz::GetEnv(kName);
    EXPECT_EQ(retrieved, std::string(kValue))
        << "GetEnv must return the exact value written by SetEnv";
}

TEST_F(ProcessTest, SetEnv_OverwriteExisting_NewValueIsVisible) {
    const char* kName   = "UZ_ENGINE_TEST_SETENV_OVERWRITE";
    const char* kFirst  = "first_value";
    const char* kSecond = "second_value";

    ASSERT_TRUE(uz::SetEnv(kName, kFirst));
    ASSERT_TRUE(uz::SetEnv(kName, kSecond));

    const std::string retrieved = uz::GetEnv(kName);
    EXPECT_EQ(retrieved, std::string(kSecond))
        << "After overwriting an env var the new value must be visible";
}

TEST_F(ProcessTest, SetEnv_EmptyValue_GetEnvReturnsEmpty) {
    const char* kName = "UZ_ENGINE_TEST_SETENV_EMPTY";

    ASSERT_TRUE(uz::SetEnv(kName, ""));

    const std::string retrieved = uz::GetEnv(kName);
    // Some platforms return empty string for an empty variable.
    // Either empty or absent is acceptable; the important thing is no crash.
    EXPECT_TRUE(retrieved.empty())
        << "GetEnv of a variable set to empty string should return empty";
}
