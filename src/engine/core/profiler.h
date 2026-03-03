#pragma once

// CPU profiling macros. When UZ_PROFILING=1 these hook into Tracy.
// When UZ_PROFILING=0 (default) they are complete no-ops with zero overhead.
//
// Usage:
//   void Renderer::render() {
//       UZ_PROFILE_FUNCTION();          // profiles entire function scope
//       UZ_PROFILE_FRAME_MARK();        // marks frame boundary for the profiler
//       UZ_PROFILE_VALUE("drawcalls", drawCallCount);
//   }

#if defined(UZ_PROFILING) && UZ_PROFILING
    #include <tracy/Tracy.hpp>
    #define UZ_PROFILE_FUNCTION()           ZoneScoped
    #define UZ_PROFILE_ZONE(name)           ZoneScopedN(name)
    #define UZ_PROFILE_FRAME_MARK()         FrameMark
    #define UZ_PROFILE_VALUE(name, val)     TracyPlot(name, (double)(val))
    #define UZ_PROFILE_MESSAGE(str, len)    TracyMessage(str, len)
#else
    #define UZ_PROFILE_FUNCTION()           ((void)0)
    #define UZ_PROFILE_ZONE(name)           ((void)0)
    #define UZ_PROFILE_FRAME_MARK()         ((void)0)
    #define UZ_PROFILE_VALUE(name, val)     ((void)0)
    #define UZ_PROFILE_MESSAGE(str, len)    ((void)0)
#endif
