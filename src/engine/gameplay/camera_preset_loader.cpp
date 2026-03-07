// camera_preset_loader.cpp — .uzcam INI-like parser.

#include "camera_preset_loader.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace uz {

// ============================================================================
// Helpers
// ============================================================================

static std::string trim(const std::string& s) {
    const char* begin = s.c_str();
    const char* end   = begin + s.size();
    while (begin < end && (*begin == ' ' || *begin == '\t' || *begin == '\r')) ++begin;
    while (end > begin && (*(end-1) == ' ' || *(end-1) == '\t' || *(end-1) == '\r')) --end;
    return std::string(begin, static_cast<size_t>(end - begin));
}

static float to_float(const std::string& s, float fallback = 0.0f) {
    char* end = nullptr;
    float v = std::strtof(s.c_str(), &end);
    return (end != s.c_str()) ? v : fallback;
}

static bool to_bool(const std::string& s) {
    return s == "true" || s == "1" || s == "yes";
}

// ============================================================================
// Parser
// ============================================================================

bool load_camera_preset(const std::string& path,
                        CameraSystemState& state,
                        std::string& error_out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        error_out = "cannot open file: " + path;
        return false;
    }

    enum class Section { None, Mode, SpringArm, Fov, DoF, Shake };
    Section section = Section::None;
    ShakePreset current_shake{};

    auto flush_shake = [&]() {
        if (current_shake.name[0] != '\0') {
            state.shake_presets.push_back(current_shake);
            current_shake = ShakePreset{};
        }
    };

    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        ++line_num;
        line = trim(line);

        // Skip empty lines and comments.
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        // Section header.
        if (line[0] == '[') {
            flush_shake();

            auto end_bracket = line.find(']');
            if (end_bracket == std::string::npos) {
                error_out = "malformed section at line " + std::to_string(line_num);
                return false;
            }
            std::string header = trim(line.substr(1, end_bracket - 1));

            if (header == "mode") {
                section = Section::Mode;
            } else if (header == "spring_arm") {
                section = Section::SpringArm;
            } else if (header == "fov") {
                section = Section::Fov;
            } else if (header == "dof") {
                section = Section::DoF;
            } else if (header.substr(0, 6) == "shake ") {
                section = Section::Shake;
                // Parse "shake preset=name".
                auto eq = header.find('=');
                if (eq != std::string::npos) {
                    std::string name = trim(header.substr(eq + 1));
                    std::strncpy(current_shake.name, name.c_str(),
                                 sizeof(current_shake.name) - 1);
                    current_shake.name[sizeof(current_shake.name) - 1] = '\0';
                }
            } else {
                // Unknown section — skip silently.
                section = Section::None;
            }
            continue;
        }

        // Key = Value pair.
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key   = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        switch (section) {
        case Section::Mode:
            if (key == "type") {
                if      (value == "third_person") state.mode = CameraMode::ThirdPerson;
                else if (value == "first_person") state.mode = CameraMode::FirstPerson;
                else if (value == "orbit")        state.mode = CameraMode::Orbit;
                else if (value == "cinematic")    state.mode = CameraMode::Cinematic;
            }
            break;

        case Section::SpringArm:
            if      (key == "arm_length")         state.spring_arm_data.arm_length         = to_float(value);
            else if (key == "socket_offset_z")    state.spring_arm_data.socket_offset_z    = to_float(value);
            else if (key == "lag_speed")          state.spring_arm_data.lag_speed          = to_float(value);
            else if (key == "rotation_lag_speed") state.spring_arm_data.rotation_lag_speed = to_float(value);
            else if (key == "collision_test")     state.spring_arm_data.collision_test     = to_bool(value);
            else if (key == "probe_radius")       state.spring_arm_data.probe_radius       = to_float(value);
            break;

        case Section::Fov:
            if      (key == "default")      state.fov_settings.default_fov  = to_float(value);
            else if (key == "ads")          state.fov_settings.ads_fov      = to_float(value);
            else if (key == "sprint")       state.fov_settings.sprint_fov   = to_float(value);
            else if (key == "interp_speed") state.fov_settings.interp_speed = to_float(value);
            break;

        case Section::DoF:
            if      (key == "enabled")      state.dof.enabled      = to_bool(value);
            else if (key == "focus_dist")   state.dof.focus_dist   = to_float(value);
            else if (key == "focus_range")  state.dof.focus_range  = to_float(value);
            else if (key == "near_blur")    state.dof.near_blur    = to_float(value);
            else if (key == "far_blur")     state.dof.far_blur     = to_float(value);
            else if (key == "bokeh_radius") state.dof.bokeh_radius = to_float(value);
            break;

        case Section::Shake:
            if      (key == "magnitude") current_shake.magnitude = to_float(value);
            else if (key == "frequency") current_shake.frequency = to_float(value);
            else if (key == "duration")  current_shake.duration  = to_float(value);
            break;

        case Section::None:
            break;
        }
    }

    flush_shake();
    state.current_fov = state.fov_settings.default_fov;
    return true;
}

} // namespace uz
