/// MSVC 14.36 shim for spdlog compiled-lib mode.
///
/// MSVC 14.36's <thread> header has a bug where std::thread constructor template
/// cannot find _beginthreadex. This prevents compiling:
///   - spdlog/src/async.cpp (constructs std::thread in thread_pool)
///   - spdlog/src/color_sinks.cpp (includes <spdlog/async.h>)
///
/// This file provides the symbols those files would have provided, minus the
/// async-specific instantiations that require thread construction.

#ifdef _MSC_VER

#ifndef SPDLOG_COMPILED_LIB
#define SPDLOG_COMPILED_LIB
#endif

// periodic_worker destructor (normally in async.cpp) — only calls join(), no construction
#include <spdlog/common.h>
#include <spdlog/details/periodic_worker-inl.h>

// Color sink template instantiations (normally in color_sinks.cpp)
// We include the inl header and instantiate the synchronous templates only.
#include <mutex>
#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/wincolor_sink-inl.h>
template class SPDLOG_API spdlog::sinks::wincolor_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::wincolor_sink<spdlog::details::console_nullmutex>;
template class SPDLOG_API spdlog::sinks::wincolor_stdout_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::wincolor_stdout_sink<spdlog::details::console_nullmutex>;
template class SPDLOG_API spdlog::sinks::wincolor_stderr_sink<spdlog::details::console_mutex>;
template class SPDLOG_API spdlog::sinks::wincolor_stderr_sink<spdlog::details::console_nullmutex>;

// Synchronous factory methods for color loggers
#include <spdlog/sinks/stdout_color_sinks-inl.h>
template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stdout_color_mt<spdlog::synchronous_factory>(const std::string &logger_name,
                                                     spdlog::color_mode mode);
template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stdout_color_st<spdlog::synchronous_factory>(const std::string &logger_name,
                                                     spdlog::color_mode mode);
template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stderr_color_mt<spdlog::synchronous_factory>(const std::string &logger_name,
                                                     spdlog::color_mode mode);
template SPDLOG_API std::shared_ptr<spdlog::logger>
spdlog::stderr_color_st<spdlog::synchronous_factory>(const std::string &logger_name,
                                                     spdlog::color_mode mode);

// NOTE: async_factory instantiations are intentionally omitted — they require
// thread_pool which constructs std::thread, triggering the MSVC 14.36 bug.

#endif // _MSC_VER
