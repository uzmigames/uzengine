#pragma once

/// UzEngine CPU Profiler
/// RAII scope-based, hierarchical stat groups.
/// Tracy integration for live profiling; standalone CSV fallback.

#include <atomic>
#include <cstdint>
#include <cstring>

// ── Tracy integration ───────────────────────────────────────────────────────
#if defined(UZ_PROFILING_ENABLED) && __has_include(<tracy/Tracy.hpp>)
    #include <tracy/Tracy.hpp>

    #define UZ_PROFILE_SCOPE(Name)        ZoneScopedN(#Name)
    #define UZ_PROFILE_FUNCTION()         ZoneScoped
    #define UZ_PROFILE_FRAME_MARK()       FrameMark
    #define UZ_PROFILE_THREAD(Name)       tracy::SetThreadName(Name)
    #define UZ_PROFILE_VALUE(Name, Val)   TracyPlot(Name, Val)
    #define UZ_PROFILE_TAG(Text)          ZoneText(Text, std::strlen(Text))
#else
    #define UZ_PROFILE_SCOPE(Name)        ((void)0)
    #define UZ_PROFILE_FUNCTION()         ((void)0)
    #define UZ_PROFILE_FRAME_MARK()       ((void)0)
    #define UZ_PROFILE_THREAD(Name)       ((void)0)
    #define UZ_PROFILE_VALUE(Name, Val)   ((void)0)
    #define UZ_PROFILE_TAG(Text)          ((void)0)
#endif

// ── Stat counters (always available, cheap) ─────────────────────────────────

namespace uz {

struct StatGroup {
    const char* name;
};

/// Declare a stat counter. Thread-safe atomic increments.
struct StatCounter {
    const char* name;
    const StatGroup* group;
    std::atomic<int64_t> value{0};

    void Increment(int64_t delta = 1) { value.fetch_add(delta, std::memory_order_relaxed); }
    void Reset() { value.store(0, std::memory_order_relaxed); }
    int64_t Get() const { return value.load(std::memory_order_relaxed); }
};

/// Built-in stat groups.
extern StatGroup StatRendering;
extern StatGroup StatRHI;
extern StatGroup StatPhysics;
extern StatGroup StatScene;
extern StatGroup StatMemory;
extern StatGroup StatIO;

} // namespace uz

#define UZ_DECLARE_STAT(Group, Name) \
    extern ::uz::StatCounter Stat_##Group##_##Name

#define UZ_DEFINE_STAT(Group, Name) \
    ::uz::StatCounter Stat_##Group##_##Name { #Name, &::uz::Stat##Group }

#define UZ_STAT_INCREMENT(Group, Name, ...) \
    Stat_##Group##_##Name.Increment(__VA_ARGS__ + 0)

#define UZ_STAT_RESET(Group, Name) \
    Stat_##Group##_##Name.Reset()

#define UZ_STAT_GET(Group, Name) \
    Stat_##Group##_##Name.Get()
