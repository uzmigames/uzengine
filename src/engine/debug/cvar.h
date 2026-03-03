#pragma once

// cvar.h -- Console Variables: runtime-configurable named parameters.
//
// CVars are typed (bool, int, float, string). They can be read and written
// by name. Changes trigger registered callbacks. CVars can be flagged as
// read-only (engine internals) or cheat-protected.

#include <cstdint>
#include <functional>
#include <string>

namespace uz {

enum class CVarType : uint8_t { Bool, Int, Float, String };

enum CVarFlags : uint32_t {
    CVAR_NONE     = 0,
    CVAR_READONLY = 1 << 0, // Cannot be changed at runtime
    CVAR_CHEAT    = 1 << 1, // Only changeable when cheats enabled
    CVAR_PERSIST  = 1 << 2, // Saved to / loaded from config file
    CVAR_DEV      = 1 << 3, // Developer-only, hidden from release builds
};

using CVarChangedFn = std::function<void(const char* name)>;

struct CVarValue {
    CVarType type;
    union {
        bool    b;
        int32_t i;
        float   f;
    };
    char str[256]; // For string type
};

struct CVarInfo {
    char      name[128];
    char      description[256];
    CVarValue value;
    CVarValue default_value;
    uint32_t  flags; // CVarFlags
};

class CVarSystem {
public:
    static constexpr uint32_t kMaxCVars = 512;

    CVarSystem();

    // Register CVars. Returns false if name already registered or max reached.
    bool register_bool(const char* name, bool default_val,
                       const char* desc, uint32_t flags = CVAR_NONE);
    bool register_int(const char* name, int32_t default_val,
                      const char* desc, uint32_t flags = CVAR_NONE);
    bool register_float(const char* name, float default_val,
                        const char* desc, uint32_t flags = CVAR_NONE);
    bool register_string(const char* name, const char* default_val,
                         const char* desc, uint32_t flags = CVAR_NONE);

    // Get current value. Returns false/0/0.0f/"" if not found or wrong type.
    [[nodiscard]] bool        get_bool(const char* name) const;
    [[nodiscard]] int32_t     get_int(const char* name) const;
    [[nodiscard]] float       get_float(const char* name) const;
    [[nodiscard]] const char* get_string(const char* name) const;

    // Set value. Returns false if: not found, wrong type, or readonly flag.
    bool set_bool(const char* name, bool val);
    bool set_int(const char* name, int32_t val);
    bool set_float(const char* name, float val);
    bool set_string(const char* name, const char* val);

    // Set from string (for console/config file input). Parses type automatically.
    bool set_from_string(const char* name, const char* str_val);

    // Get current value as string (for display/serialization).
    [[nodiscard]] std::string get_as_string(const char* name) const;

    // Reset a CVar to its default value.
    bool reset(const char* name);

    // Reset ALL CVars to defaults.
    void reset_all();

    // Query metadata.
    [[nodiscard]] bool            has(const char* name) const;
    [[nodiscard]] const CVarInfo* info(const char* name) const;
    [[nodiscard]] uint32_t        count() const;

    // Enumerate all registered CVars. Calls fn(info) for each.
    using EnumerateFn = std::function<void(const CVarInfo&)>;
    void enumerate(EnumerateFn fn) const;

    // Enumerate CVars matching a prefix (for autocomplete).
    void enumerate_prefix(const char* prefix, EnumerateFn fn) const;

    // Change notification. fn is called after any successful set_*().
    void on_changed(CVarChangedFn fn);

    // Serialize all CVAR_PERSIST CVars to INI-like text.
    [[nodiscard]] std::string serialize() const;

    // Deserialize: parse INI-like text and set matching CVars.
    // Format: "name = value\n" per line. Lines starting with # are comments.
    void deserialize(const char* text);

    // Cheats enable/disable (affects CVAR_CHEAT CVars).
    void set_cheats_enabled(bool enabled);
    [[nodiscard]] bool cheats_enabled() const;

private:
    CVarInfo cvars_[kMaxCVars];
    uint32_t count_ = 0;
    bool     cheats_enabled_ = false;
    CVarChangedFn on_changed_fn_;

    CVarInfo*       find(const char* name);
    const CVarInfo* find(const char* name) const;
    void            notify(const char* name);
};

} // namespace uz
