/// UzEngine Native Dialogs Implementation (Linux)
///
/// File dialogs and message boxes via `zenity` (primary) or `kdialog`
/// (fallback), invoked as subprocesses via fork/execvp. Follows the same
/// fork/pipe pattern established by clipboard.cpp in this module.
///
/// # Tool Detection
/// Both tools are probed on the first call and the result is cached for the
/// lifetime of the process. zenity is preferred because it is available on
/// both GNOME and non-GNOME systems and produces more consistent output.
///
/// # Output Parsing
/// zenity writes selected paths to stdout separated by '\n'
/// (using `--separator=\n` for multi-select). Exit code 0 = OK, 1 = Cancel.
/// kdialog writes a single path or newline-separated paths to stdout.
///
/// # Filter Translation
/// DialogFilter.extensions uses semicolons ("*.png;*.jpg"). Both tools expect
/// space-separated extensions in their filter strings, so semicolons are
/// replaced with spaces before passing to the subprocess.

#include "native_dialogs.h"

#ifdef __linux__

#include "log.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

UZ_DEFINE_LOG_CATEGORY(NativeDialog, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Subprocess timeout in milliseconds; dialog tools should never hang this
/// long unless the desktop session is dead.
constexpr int kDialogTimeoutMs = 120'000;

/// Poll interval while waiting for the dialog subprocess to exit.
constexpr int kPollIntervalMs = 10;

/// Read buffer chunk size for collecting subprocess stdout.
constexpr size_t kReadChunkSize = 4096;

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Tool Detection
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Available dialog backends, in preference order.
enum class DialogTool { Zenity, KDialog, None };

/// Probes whether `toolName` is on PATH by forking and executing `--version`.
/// Returns true if execvp succeeds (tool found), false if it exits 127.
bool ProbeToolExists(const char* toolName) noexcept {
    const pid_t pid = ::fork();
    if (pid < 0) return false;

    if (pid == 0) {
        // Child: redirect stdout/stderr to /dev/null — we only care about exit code
        const int devNull = ::open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            ::dup2(devNull, STDOUT_FILENO);
            ::dup2(devNull, STDERR_FILENO);
            ::close(devNull);
        }
        char* argv[] = {
            const_cast<char*>(toolName),
            const_cast<char*>("--version"),
            nullptr
        };
        ::execvp(toolName, argv);
        ::_exit(127); // execvp failed — tool not on PATH
    }

    int status = 0;
    ::waitpid(pid, &status, 0);

    // Our sentinel for "execvp failed" is exit code 127
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) return false;
    return true;
}

/// Returns the available dialog tool for this session, cached after first call.
DialogTool DetectDialogTool() noexcept {
    static DialogTool s_cached = DialogTool::None;
    static bool s_probed = false;

    if (s_probed) return s_cached;
    s_probed = true;

    if (ProbeToolExists("zenity")) {
        s_cached = DialogTool::Zenity;
    } else if (ProbeToolExists("kdialog")) {
        s_cached = DialogTool::KDialog;
    } else {
        s_cached = DialogTool::None;
        UZ_LOG(NativeDialog, Warning,
            "DetectDialogTool: neither zenity nor kdialog found on PATH — "
            "native dialogs will not function");
    }
    return s_cached;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Subprocess Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Forks a subprocess with `argv`, captures its stdout, and waits for it to
/// exit. Returns the raw stdout bytes.
///
/// On completion, `*exitCodeOut` is set to the subprocess exit code:
///   - 0  = user confirmed / OK
///   - 1  = user cancelled / first alternative button (zenity/kdialog convention)
///   - 2  = second alternative button (kdialog --yesnocancel "Cancel")
///   - 127 = execvp failed (tool not on PATH)
///
/// `argv` must be null-terminated. The caller retains ownership.
std::vector<char> RunDialogCommand(const char* const* argv, int* exitCodeOut) {
    *exitCodeOut = -1;

    int stdoutPipe[2] = { -1, -1 };
    if (::pipe(stdoutPipe) != 0) {
        UZ_LOG(NativeDialog, Error, "RunDialogCommand: pipe() failed: {}", ::strerror(errno));
        return {};
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        UZ_LOG(NativeDialog, Error, "RunDialogCommand: fork() failed: {}", ::strerror(errno));
        ::close(stdoutPipe[0]);
        ::close(stdoutPipe[1]);
        return {};
    }

    if (pid == 0) {
        // Child: wire stdout → pipe write end
        ::close(stdoutPipe[0]);
        ::dup2(stdoutPipe[1], STDOUT_FILENO);
        ::close(stdoutPipe[1]);

        // Silence stderr so zenity/kdialog warnings don't pollute the engine console
        const int devNull = ::open("/dev/null", O_WRONLY);
        if (devNull >= 0) {
            ::dup2(devNull, STDERR_FILENO);
            ::close(devNull);
        }

        ::execvp(argv[0], const_cast<char* const*>(argv));
        ::_exit(127);
    }

    // Parent: close write end, read stdout until EOF
    ::close(stdoutPipe[1]);

    std::vector<char> output;
    char buf[kReadChunkSize];
    ssize_t n = 0;
    while ((n = ::read(stdoutPipe[0], buf, sizeof(buf))) > 0) {
        output.insert(output.end(), buf, buf + n);
    }
    ::close(stdoutPipe[0]);

    // Wait with timeout — dialog tools should not hang, but be defensive
    int status = 0;
    int elapsed = 0;
    bool timedOut = false;

    while (elapsed < kDialogTimeoutMs) {
        const pid_t ret = ::waitpid(pid, &status, WNOHANG);
        if (ret == pid) break;
        if (ret < 0) break;

        struct timespec ts { 0, static_cast<long>(kPollIntervalMs) * 1'000'000L };
        ::nanosleep(&ts, nullptr);
        elapsed += kPollIntervalMs;
    }

    if (elapsed >= kDialogTimeoutMs) {
        UZ_LOG(NativeDialog, Error,
            "RunDialogCommand: subprocess '{}' timed out after {}ms — killing",
            argv[0], kDialogTimeoutMs);
        ::kill(pid, SIGKILL);
        ::waitpid(pid, &status, 0);
        timedOut = true;
    }

    if (timedOut) {
        *exitCodeOut = -1;
        return {};
    }

    const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    *exitCodeOut = exitCode;

    if (exitCode == 127) {
        // execvp failed — tool vanished between probe and exec
        UZ_LOG(NativeDialog, Error,
            "RunDialogCommand: execvp failed for '{}' (tool not found)", argv[0]);
        return {};
    }

    // Exit codes 0, 1, 2 are all valid dialog responses (OK / No / Cancel).
    // Any other non-zero code is unexpected — log it but still return output
    // so the caller can decide how to handle it.
    if (exitCode != 0 && exitCode != 1 && exitCode != 2) {
        UZ_LOG(NativeDialog, Warning,
            "RunDialogCommand: '{}' exited with unexpected code={}", argv[0], exitCode);
    }

    return output;
}

/// Splits `output` on '\n' and returns non-empty lines as strings.
/// Strips a trailing newline that zenity appends after the last path.
std::vector<std::string> SplitLines(const std::vector<char>& output) {
    std::vector<std::string> lines;
    if (output.empty()) return lines;

    const char* begin = output.data();
    const char* end   = output.data() + output.size();
    const char* cur   = begin;

    while (cur < end) {
        const char* nl = static_cast<const char*>(::memchr(cur, '\n', static_cast<size_t>(end - cur)));
        const char* lineEnd = nl ? nl : end;
        if (lineEnd > cur) {
            lines.emplace_back(cur, lineEnd);
        }
        cur = nl ? (nl + 1) : end;
    }

    return lines;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Filter String Builders
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Converts a single DialogFilter to a zenity --file-filter argument string.
///
/// zenity expects: "Description | *.ext1 *.ext2"
/// Our input has semicolons as separators, so we replace them with spaces.
std::string BuildZenityFilter(const DialogFilter& filter) {
    std::string result;
    if (filter.description) {
        result += filter.description;
    }
    result += " | ";
    if (filter.extensions) {
        // Replace semicolons with spaces: "*.png;*.jpg" → "*.png *.jpg"
        for (const char* p = filter.extensions; *p != '\0'; ++p) {
            result += (*p == ';') ? ' ' : *p;
        }
    }
    return result;
}

/// Converts a DialogFilter array to a single kdialog filter string.
///
/// kdialog expects: "Description (*.ext1 *.ext2);;Description2 (*.ext3)"
/// Each filter group is separated by ";;".
std::string BuildKDialogFilter(const DialogFilter* filters, int count) {
    if (!filters || count <= 0) return {};
    std::string result;
    for (int i = 0; i < count; ++i) {
        if (i > 0) result += ";;";
        if (filters[i].description) result += filters[i].description;
        result += " (";
        if (filters[i].extensions) {
            for (const char* p = filters[i].extensions; *p != '\0'; ++p) {
                result += (*p == ';') ? ' ' : *p;
            }
        }
        result += ')';
    }
    return result;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ShowOpenFileDialog
// ─────────────────────────────────────────────────────────────────────────────

FileDialogResult ShowOpenFileDialog(
    const DialogFilter* filters, int filterCount,
    bool multiSelect,
    const char* defaultDir,
    void* /*parentHwnd*/)
{
    FileDialogResult result{};
    const DialogTool tool = DetectDialogTool();

    if (tool == DialogTool::None) {
        UZ_LOG(NativeDialog, Error, "ShowOpenFileDialog: no dialog tool available");
        return result;
    }

    if (tool == DialogTool::Zenity) {
        // Build argv — zenity allows multiple --file-filter flags (one per filter)
        std::vector<std::string> filterArgs;
        filterArgs.reserve(static_cast<size_t>(filterCount) * 2);
        for (int i = 0; i < filterCount; ++i) {
            filterArgs.push_back("--file-filter=" + BuildZenityFilter(filters[i]));
        }

        // Initial directory hint: append a trailing slash so zenity treats it as a dir
        std::string filenameArg;
        if (defaultDir) {
            filenameArg = "--filename=";
            filenameArg += defaultDir;
            if (!filenameArg.empty() && filenameArg.back() != '/') filenameArg += '/';
        }

        std::vector<const char*> argv;
        argv.push_back("zenity");
        argv.push_back("--file-selection");
        if (multiSelect) {
            argv.push_back("--multiple");
            argv.push_back("--separator=\n");
        }
        for (const auto& f : filterArgs) argv.push_back(f.c_str());
        if (!filenameArg.empty()) argv.push_back(filenameArg.c_str());
        argv.push_back(nullptr);

        int exitCode = -1;
        const auto output = RunDialogCommand(argv.data(), &exitCode);
        if (exitCode != 0 || output.empty()) return result; // 1 = cancelled, else error

        result.paths = SplitLines(output);
        result.ok    = !result.paths.empty();

    } else {
        // kdialog --getopenfilename [startDir] [filter]
        const std::string filterStr = BuildKDialogFilter(filters, filterCount);
        const char* startDir = defaultDir ? defaultDir : "/";

        std::vector<const char*> argv;
        argv.push_back("kdialog");
        argv.push_back("--getopenfilename");
        argv.push_back(startDir);
        if (!filterStr.empty()) argv.push_back(filterStr.c_str());
        argv.push_back(nullptr);

        int exitCode = -1;
        const auto output = RunDialogCommand(argv.data(), &exitCode);
        if (exitCode != 0 || output.empty()) return result;

        result.paths = SplitLines(output);
        result.ok    = !result.paths.empty();
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShowSaveFileDialog
// ─────────────────────────────────────────────────────────────────────────────

FileDialogResult ShowSaveFileDialog(
    const DialogFilter* filters, int filterCount,
    const char* defaultName,
    const char* defaultDir,
    void* /*parentHwnd*/)
{
    FileDialogResult result{};
    const DialogTool tool = DetectDialogTool();

    if (tool == DialogTool::None) {
        UZ_LOG(NativeDialog, Error, "ShowSaveFileDialog: no dialog tool available");
        return result;
    }

    if (tool == DialogTool::Zenity) {
        std::vector<std::string> filterArgs;
        filterArgs.reserve(static_cast<size_t>(filterCount) * 2);
        for (int i = 0; i < filterCount; ++i) {
            filterArgs.push_back("--file-filter=" + BuildZenityFilter(filters[i]));
        }

        // Build the --filename hint: prefer "dir/name", fall back to just dir or name
        std::string filenameArg;
        if (defaultDir || defaultName) {
            filenameArg = "--filename=";
            if (defaultDir) {
                filenameArg += defaultDir;
                if (filenameArg.back() != '/') filenameArg += '/';
            }
            if (defaultName) {
                filenameArg += defaultName;
            }
        }

        std::vector<const char*> argv;
        argv.push_back("zenity");
        argv.push_back("--file-selection");
        argv.push_back("--save");
        argv.push_back("--confirm-overwrite");
        for (const auto& f : filterArgs) argv.push_back(f.c_str());
        if (!filenameArg.empty()) argv.push_back(filenameArg.c_str());
        argv.push_back(nullptr);

        int exitCode = -1;
        const auto output = RunDialogCommand(argv.data(), &exitCode);
        if (exitCode != 0 || output.empty()) return result;

        const auto lines = SplitLines(output);
        if (!lines.empty()) {
            result.paths.push_back(lines.front());
            result.ok = true;
        }

    } else {
        // kdialog --getsavefilename [startDir/defaultName] [filter]
        std::string startPath;
        if (defaultDir) {
            startPath = defaultDir;
            if (!startPath.empty() && startPath.back() != '/') startPath += '/';
        }
        if (defaultName) startPath += defaultName;
        if (startPath.empty()) startPath = "/";

        const std::string filterStr = BuildKDialogFilter(filters, filterCount);

        std::vector<const char*> argv;
        argv.push_back("kdialog");
        argv.push_back("--getsavefilename");
        argv.push_back(startPath.c_str());
        if (!filterStr.empty()) argv.push_back(filterStr.c_str());
        argv.push_back(nullptr);

        int exitCode = -1;
        const auto output = RunDialogCommand(argv.data(), &exitCode);
        if (exitCode != 0 || output.empty()) return result;

        const auto lines = SplitLines(output);
        if (!lines.empty()) {
            result.paths.push_back(lines.front());
            result.ok = true;
        }
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShowMessageBox
// ─────────────────────────────────────────────────────────────────────────────

MessageBoxResult ShowMessageBox(
    const char* title, const char* message,
    MessageBoxButtons buttons,
    MessageBoxIcon icon,
    void* /*parentHwnd*/)
{
    const DialogTool tool = DetectDialogTool();

    if (tool == DialogTool::None) {
        UZ_LOG(NativeDialog, Error, "ShowMessageBox: no dialog tool available");
        return MessageBoxResult::Cancel;
    }

    const char* safeTitle   = title   ? title   : "";
    const char* safeMessage = message ? message : "";

    if (tool == DialogTool::Zenity) {
        // Select the correct zenity dialog type based on icon + button combination.
        // zenity --question maps to yes/no. --yesnocancel is not a native zenity flag;
        // we approximate it with --question and a custom extra-button for Cancel.
        const char* dialogType = nullptr;
        switch (icon) {
            case MessageBoxIcon::Info:     dialogType = "--info";     break;
            case MessageBoxIcon::Warning:  dialogType = "--warning";  break;
            case MessageBoxIcon::Error:    dialogType = "--error";    break;
            case MessageBoxIcon::Question: dialogType = "--question"; break;
        }

        std::string titleArg   = std::string("--title=")  + safeTitle;
        std::string messageArg = std::string("--text=")   + safeMessage;

        if (buttons == MessageBoxButtons::Ok) {
            // --info/--warning/--error show a single OK button by default.
            // Closing the dialog (exit 1) is still treated as OK — there's no other choice.
            const char* argv[] = {
                "zenity", dialogType,
                titleArg.c_str(), messageArg.c_str(),
                nullptr
            };
            int exitCode = -1;
            RunDialogCommand(argv, &exitCode);
            return MessageBoxResult::Ok;
        }

        if (buttons == MessageBoxButtons::OkCancel) {
            // --question with relabelled buttons; exit 0=OK, exit 1=Cancel
            const char* argv[] = {
                "zenity", "--question",
                titleArg.c_str(), messageArg.c_str(),
                "--ok-label=OK", "--cancel-label=Cancel",
                nullptr
            };
            int exitCode = -1;
            RunDialogCommand(argv, &exitCode);
            return (exitCode == 0) ? MessageBoxResult::Ok : MessageBoxResult::Cancel;
        }

        if (buttons == MessageBoxButtons::YesNo) {
            // exit 0=Yes (first button), exit 1=No (second/cancel button)
            const char* argv[] = {
                "zenity", "--question",
                titleArg.c_str(), messageArg.c_str(),
                "--ok-label=Yes", "--cancel-label=No",
                nullptr
            };
            int exitCode = -1;
            RunDialogCommand(argv, &exitCode);
            return (exitCode == 0) ? MessageBoxResult::Yes : MessageBoxResult::No;
        }

        if (buttons == MessageBoxButtons::YesNoCancel) {
            // zenity lacks a native three-button flag. Use --extra-button=Cancel;
            // when the extra button is pressed, zenity prints its label to stdout
            // and exits with a non-zero code. exit 0=Yes, exit 1=No, stdout="Cancel"=Cancel.
            const char* argv[] = {
                "zenity", "--question",
                titleArg.c_str(), messageArg.c_str(),
                "--ok-label=Yes", "--cancel-label=No",
                "--extra-button=Cancel",
                nullptr
            };
            int exitCode = -1;
            const auto output = RunDialogCommand(argv, &exitCode);

            // Extra-button output is the button label followed by a newline
            if (!output.empty()) {
                const auto lines = SplitLines(output);
                if (!lines.empty() && lines.front() == "Cancel") {
                    return MessageBoxResult::Cancel;
                }
            }
            return (exitCode == 0) ? MessageBoxResult::Yes : MessageBoxResult::No;
        }

    } else {
        // kdialog backend — map (buttons, icon) to the appropriate kdialog verb
        std::string titleArg = std::string("--title=") + safeTitle;

        if (buttons == MessageBoxButtons::Ok) {
            const char* verb = nullptr;
            switch (icon) {
                case MessageBoxIcon::Info:     verb = "--msgbox"; break;
                case MessageBoxIcon::Warning:  verb = "--sorry";  break;
                case MessageBoxIcon::Error:    verb = "--error";  break;
                case MessageBoxIcon::Question: verb = "--msgbox"; break;
            }
            const char* argv[] = {
                "kdialog", verb, safeMessage,
                titleArg.c_str(),
                nullptr
            };
            int exitCode = -1;
            RunDialogCommand(argv, &exitCode);
            return MessageBoxResult::Ok;
        }

        if (buttons == MessageBoxButtons::OkCancel) {
            // --warningcontinuecancel: exit 0=Continue (OK), exit 1=Cancel
            const char* argv[] = {
                "kdialog", "--warningcontinuecancel", safeMessage,
                titleArg.c_str(),
                nullptr
            };
            int exitCode = -1;
            RunDialogCommand(argv, &exitCode);
            return (exitCode == 0) ? MessageBoxResult::Ok : MessageBoxResult::Cancel;
        }

        if (buttons == MessageBoxButtons::YesNo) {
            // kdialog --yesno: exit 0=Yes, exit 1=No
            const char* argv[] = {
                "kdialog", "--yesno", safeMessage,
                titleArg.c_str(),
                nullptr
            };
            int exitCode = -1;
            RunDialogCommand(argv, &exitCode);
            return (exitCode == 0) ? MessageBoxResult::Yes : MessageBoxResult::No;
        }

        if (buttons == MessageBoxButtons::YesNoCancel) {
            // kdialog --yesnocancel: exit 0=Yes, exit 1=No, exit 2=Cancel
            const char* argv[] = {
                "kdialog", "--yesnocancel", safeMessage,
                titleArg.c_str(),
                nullptr
            };
            int exitCode = -1;
            RunDialogCommand(argv, &exitCode);
            if (exitCode == 0) return MessageBoxResult::Yes;
            if (exitCode == 2) return MessageBoxResult::Cancel;
            return MessageBoxResult::No; // exit 1
        }
    }

    return MessageBoxResult::Cancel;
}

} // namespace uz

#endif // __linux__
