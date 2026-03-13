/// UzEngine Environment Variables Implementation (Linux)
///
/// POSIX getenv/setenv — direct UTF-8 byte-string environment access.

#include "environment.h"

#ifdef __linux__

#include "log.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace uz {

std::string GetEnv(const char* name) {
    if (!name) {
        UZ_LOG(Platform, Warning, "GetEnv called with nullptr name");
        return {};
    }

    const char* value = ::getenv(name);
    if (!value) {
        // Variable not set — not an error, callers handle empty string
        return {};
    }

    return std::string(value);
}

bool SetEnv(const char* name, const char* value) {
    if (!name || !value) {
        UZ_LOG(Platform, Error, "SetEnv called with nullptr argument");
        return false;
    }

    // overwrite=1: replace any existing value
    if (::setenv(name, value, 1) != 0) {
        UZ_LOG(Platform, Error, "SetEnv: setenv failed for '{}': {}", name, ::strerror(errno));
        return false;
    }

    return true;
}

} // namespace uz

#endif // __linux__
