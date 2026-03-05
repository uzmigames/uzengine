/*
 * diag.h -- Lightweight crash diagnostic logging
 *
 * Writes timestamped messages to crash_log.txt next to the executable.
 * Works from both C and C++ code.  Each call opens/appends/closes the
 * file so that the log persists even after a crash.
 *
 * Usage:
 *   DIAG("Starting renderer");
 *   DIAG_FMT("HRESULT = 0x%08X", hr);
 *   DIAG_HR("CreateDevice", hr);         // logs OK or hex error
 */

#ifndef UZ_DIAG_H
#define UZ_DIAG_H

#ifdef _WIN32
#include <windows.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void diag_log(const char* msg) {
    static char s_log_path[MAX_PATH] = {0};
    if (s_log_path[0] == 0) {
        GetModuleFileNameA(NULL, s_log_path, MAX_PATH);
        /* Find last slash */
        char* p = s_log_path;
        char* last = p;
        while (*p) { if (*p == '\\' || *p == '/') last = p; p++; }
        /* Replace filename with crash_log.txt */
        last[1] = '\0';
        strcat_s(s_log_path, sizeof(s_log_path), "crash_log.txt");
    }
    FILE* f = NULL;
    if (fopen_s(&f, s_log_path, "a") != 0 || !f) return;
    {
        /* Millisecond timestamp since process start */
        LARGE_INTEGER freq, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&now);
        double ms = (double)now.QuadPart / (double)freq.QuadPart * 1000.0;
        fprintf(f, "[%8.1f ms] %s\n", ms, msg);
        fclose(f);
    }
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

static inline void diag_fmt(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    diag_log(buf);
}

/* Log HRESULT with function name */
static inline void diag_hr(const char* fn, long hr) {
    char buf[256];
    if (hr >= 0)
        snprintf(buf, sizeof(buf), "%s => OK", fn);
    else
        snprintf(buf, sizeof(buf), "%s => FAILED (0x%08X)", fn, (unsigned)hr);
    diag_log(buf);
}

#define DIAG(msg)           diag_log(msg)
#define DIAG_FMT(fmt, ...)  diag_fmt(fmt, __VA_ARGS__)
#define DIAG_HR(fn, hr)     diag_hr(fn, hr)

#ifdef __cplusplus
}
#endif

#else
/* Non-Windows stub */
#define DIAG(msg)           ((void)0)
#define DIAG_FMT(fmt, ...)  ((void)0)
#define DIAG_HR(fn, hr)     ((void)0)
#endif

#endif /* UZ_DIAG_H */
