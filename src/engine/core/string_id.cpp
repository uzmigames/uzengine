// StringId — runtime interning table implementation.
//
// A simple unordered_map<uint32_t, std::string> protected by a mutex. The
// table grows as new strings are registered and is never shrunk — this is
// intentional because string interning is a permanent operation.

#include "string_id.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace uz {

namespace {

// The interning table and its guard mutex. File-scoped to avoid static init
// order issues — they are constructed on first use (which is fine because
// engine_arenas_init() runs before any string interning).
std::mutex                                s_intern_mutex;
std::unordered_map<uint32_t, std::string> s_intern_table;

} // anonymous namespace

StringId string_id(const char* str) {
    if (str == nullptr) return StringId{0};

    const size_t   len  = std::char_traits<char>::length(str);
    const uint32_t hash = detail::fnv1a_32(str, len);

    {
        std::lock_guard<std::mutex> lock(s_intern_mutex);
        // Only insert if not already present — first registration wins.
        s_intern_table.try_emplace(hash, str, len);
    }

    return StringId{hash};
}

StringId string_id(std::string_view str) {
    const uint32_t hash = detail::fnv1a_32(str.data(), str.size());

    {
        std::lock_guard<std::mutex> lock(s_intern_mutex);
        s_intern_table.try_emplace(hash, str);
    }

    return StringId{hash};
}

const char* string_id_lookup(StringId id) {
    std::lock_guard<std::mutex> lock(s_intern_mutex);
    auto it = s_intern_table.find(id.hash);
    if (it != s_intern_table.end()) {
        return it->second.c_str();
    }
    return "<unknown>";
}

} // namespace uz
