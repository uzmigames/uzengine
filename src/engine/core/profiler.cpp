// ============================================================================
// profiler.cpp — Built-in lightweight CPU profiler implementation.
//
// Lock-free recording for known scopes (atomic add).
// Mutex only taken when a NEW scope name is registered (rare, one-time).
// Fixed-size array — no heap allocations in the hot path.
// ============================================================================

#include "engine/core/profiler.h"

// Skip implementation when using Tracy
#if !(defined(UZ_PROFILING) && UZ_PROFILING && __has_include(<tracy/Tracy.hpp>))

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace uz {

// ---- QPC frequency (lazy-initialized) ----
static int64_t get_qpc_freq() {
    static int64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return freq;
}

int64_t profiler_now() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}

double profiler_ticks_to_ms(int64_t ticks) {
    return static_cast<double>(ticks) / static_cast<double>(get_qpc_freq()) * 1000.0;
}

// ---- Internal entry ----
struct ProfileEntry {
    const char*          name = nullptr;
    std::atomic<int64_t> call_count{0};
    std::atomic<int64_t> total_ticks{0};
    std::atomic<int64_t> min_ticks{INT64_MAX};
    std::atomic<int64_t> max_ticks{0};
    std::atomic<int64_t> frame_ticks{0};    // current-frame accumulator
    int64_t              last_frame_ticks = 0;
    uint32_t             frame_call_count = 0;
    uint32_t             last_frame_calls = 0;
};

static constexpr int     kMaxEntries = 512;
static ProfileEntry      g_entries[kMaxEntries];
static std::atomic<int>  g_entry_count{0};
static std::atomic<bool> g_enabled{true};
static std::mutex        g_create_mutex;
static uint32_t          g_frame_number = 0;

// ---- Find or create entry (lock-free for existing entries) ----
static ProfileEntry* find_or_create(const char* name) {
    const int count = g_entry_count.load(std::memory_order_acquire);

    // Fast path: pointer comparison (same TU, same literal)
    for (int i = 0; i < count; ++i) {
        if (g_entries[i].name == name) return &g_entries[i];
    }
    // Slow path: strcmp (different TU, same string content)
    for (int i = 0; i < count; ++i) {
        if (std::strcmp(g_entries[i].name, name) == 0) return &g_entries[i];
    }

    // Create new entry (mutex protects creation only)
    std::lock_guard<std::mutex> lk(g_create_mutex);
    const int recheck = g_entry_count.load(std::memory_order_acquire);
    for (int i = count; i < recheck; ++i) {
        if (g_entries[i].name == name || std::strcmp(g_entries[i].name, name) == 0)
            return &g_entries[i];
    }
    if (recheck >= kMaxEntries) return nullptr;

    g_entries[recheck].name = name;
    g_entries[recheck].call_count.store(0, std::memory_order_relaxed);
    g_entries[recheck].total_ticks.store(0, std::memory_order_relaxed);
    g_entries[recheck].min_ticks.store(INT64_MAX, std::memory_order_relaxed);
    g_entries[recheck].max_ticks.store(0, std::memory_order_relaxed);
    g_entries[recheck].frame_ticks.store(0, std::memory_order_relaxed);
    g_entries[recheck].last_frame_ticks = 0;
    g_entries[recheck].frame_call_count = 0;
    g_entries[recheck].last_frame_calls = 0;
    g_entry_count.store(recheck + 1, std::memory_order_release);
    return &g_entries[recheck];
}

// ---- Public API ----

void profiler_record(const char* name, int64_t start_ticks) {
    const int64_t end     = profiler_now();
    const int64_t elapsed = end - start_ticks;
    if (elapsed < 0) return; // QPC wraparound guard

    ProfileEntry* e = find_or_create(name);
    if (!e) return;

    e->call_count.fetch_add(1, std::memory_order_relaxed);
    e->total_ticks.fetch_add(elapsed, std::memory_order_relaxed);
    e->frame_ticks.fetch_add(elapsed, std::memory_order_relaxed);

    // CAS loop for min
    int64_t cur = e->min_ticks.load(std::memory_order_relaxed);
    while (elapsed < cur &&
           !e->min_ticks.compare_exchange_weak(cur, elapsed,
               std::memory_order_relaxed, std::memory_order_relaxed)) {}

    // CAS loop for max
    cur = e->max_ticks.load(std::memory_order_relaxed);
    while (elapsed > cur &&
           !e->max_ticks.compare_exchange_weak(cur, elapsed,
               std::memory_order_relaxed, std::memory_order_relaxed)) {}
}

void profiler_frame_mark() {
    const int count = g_entry_count.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) {
        g_entries[i].last_frame_ticks = g_entries[i].frame_ticks.exchange(
            0, std::memory_order_relaxed);
    }
    ++g_frame_number;
}

void profiler_set_enabled(bool enabled) {
    g_enabled.store(enabled, std::memory_order_relaxed);
}

bool profiler_enabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

int profiler_scope_count() {
    return g_entry_count.load(std::memory_order_acquire);
}

void profiler_reset() {
    const int count = g_entry_count.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) {
        g_entries[i].call_count.store(0, std::memory_order_relaxed);
        g_entries[i].total_ticks.store(0, std::memory_order_relaxed);
        g_entries[i].min_ticks.store(INT64_MAX, std::memory_order_relaxed);
        g_entries[i].max_ticks.store(0, std::memory_order_relaxed);
        g_entries[i].frame_ticks.store(0, std::memory_order_relaxed);
        g_entries[i].last_frame_ticks = 0;
    }
    g_frame_number = 0;
}

int profiler_get_stats(ProfileStats* out, int max_count) {
    const int count = g_entry_count.load(std::memory_order_acquire);
    const int n = (count < max_count) ? count : max_count;

    for (int i = 0; i < n; ++i) {
        out[i].name       = g_entries[i].name;
        out[i].call_count = static_cast<uint32_t>(
            g_entries[i].call_count.load(std::memory_order_relaxed));
        out[i].total_ms   = profiler_ticks_to_ms(
            g_entries[i].total_ticks.load(std::memory_order_relaxed));
        out[i].min_ms     = profiler_ticks_to_ms(
            g_entries[i].min_ticks.load(std::memory_order_relaxed));
        out[i].max_ms     = profiler_ticks_to_ms(
            g_entries[i].max_ticks.load(std::memory_order_relaxed));
        out[i].avg_ms     = (out[i].call_count > 0)
            ? out[i].total_ms / out[i].call_count : 0.0;
        out[i].last_frame_ms = profiler_ticks_to_ms(
            g_entries[i].last_frame_ticks);
    }
    return n;
}

// ---- Dump report ----

void profiler_dump() {
    const int count = g_entry_count.load(std::memory_order_acquire);
    if (count == 0) {
        fprintf(stderr, "[PROFILE] No scopes recorded.\n");
        return;
    }

    // Snapshot stats
    ProfileStats stats[kMaxEntries];
    const int n = profiler_get_stats(stats, count);

    // Sort by total_ms descending
    std::sort(stats, stats + n, [](const ProfileStats& a, const ProfileStats& b) {
        return a.total_ms > b.total_ms;
    });

    fprintf(stderr,
        "\n[PROFILE] %d scopes | %u frames\n"
        "%-40s %8s %11s %9s %9s %9s %10s\n"
        "%-40s %8s %11s %9s %9s %9s %10s\n",
        n, g_frame_number,
        "Scope", "Calls", "Total(ms)", "Avg(ms)", "Min(ms)", "Max(ms)", "Last(ms)",
        "-----", "-----", "---------", "-------", "-------", "-------", "--------");

    for (int i = 0; i < n; ++i) {
        const auto& s = stats[i];
        if (s.min_ms > 1e8) {
            // No valid min recorded
            fprintf(stderr, "%-40s %8u %11.2f %9.3f %9s %9.3f %10.3f\n",
                    s.name, s.call_count, s.total_ms, s.avg_ms,
                    "-", s.max_ms, s.last_frame_ms);
        } else {
            fprintf(stderr, "%-40s %8u %11.2f %9.3f %9.3f %9.3f %10.3f\n",
                    s.name, s.call_count, s.total_ms, s.avg_ms,
                    s.min_ms, s.max_ms, s.last_frame_ms);
        }
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

bool profiler_dump_if_slow(double threshold_ms) {
    const int count = g_entry_count.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) {
        double last = profiler_ticks_to_ms(g_entries[i].last_frame_ticks);
        if (last > threshold_ms) {
            fprintf(stderr, "[PROFILE] Slow frame detected (%.1f ms in '%s'):\n",
                    last, g_entries[i].name);
            profiler_dump();
            return true;
        }
    }
    return false;
}

} // namespace uz

#endif // !Tracy
