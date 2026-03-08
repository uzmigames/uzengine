// ---------------------------------------------------------------------------
// ipc_profiler_handlers.cpp -- IPC handler implementations for profiler.*
//
// Handlers:
//   profiler.enable         — {} -> { ok }
//   profiler.disable        — {} -> { ok }
//   profiler.get_frame_data — { frames? } -> { ok, zones: [...] }
// ---------------------------------------------------------------------------

#include "engine/editor/ipc_profiler_handlers.h"

#include "common/ipc_registry.h"
#include "engine/core/profiler.h"
#include "engine/core/profiler_ring_buffer.h"

#include <nlohmann/json.hpp>

namespace uz {

void profiler_register_ipc_handlers(IpcRegistry& registry) {

    // ----------------------------------------------------------------
    // profiler.enable — enable zone collection
    // ----------------------------------------------------------------
    registry.add("profiler.enable", [](const nlohmann::json&) -> nlohmann::json {
        ProfilerCollector::set_enabled(true);
        return {{"ok", true}};
    });

    // ----------------------------------------------------------------
    // profiler.disable — disable zone collection and flush ring buffer
    // ----------------------------------------------------------------
    registry.add("profiler.disable", [](const nlohmann::json&) -> nlohmann::json {
        ProfilerCollector::set_enabled(false);
        // Flush any remaining records so they are not lost.
        ProfilerCollector::flush_frame(0);
        return {{"ok", true}};
    });

    // ----------------------------------------------------------------
    // profiler.get_frame_data — { frames? } -> { ok, zones: [...] }
    // ----------------------------------------------------------------
    registry.add("profiler.get_frame_data",
        [](const nlohmann::json& args) -> nlohmann::json {
            int num_frames = 1;
            if (args.contains("frames")) {
                if (!args["frames"].is_number_integer()) {
                    return {{"ok", false},
                            {"error", "'frames' must be an integer"}};
                }
                num_frames = args["frames"].get<int>();
            }

            if (num_frames < 1 || num_frames > ProfilerCollector::kMaxFrames) {
                return {{"ok", false},
                        {"error", "frames must be 1.." +
                                  std::to_string(ProfilerCollector::kMaxFrames)}};
            }

            // Read zone records.
            constexpr uint32_t kMaxOutput = 4096;
            ProfileZoneRecord records[kMaxOutput];
            const uint32_t count = ProfilerCollector::get_frame_data(
                records, kMaxOutput, num_frames);

            auto zones = nlohmann::json::array();
            for (uint32_t i = 0; i < count; ++i) {
                const auto& r = records[i];
                const int64_t inclusive_us = r.end_us - r.start_us;
                zones.push_back({
                    {"name",         r.name ? r.name : ""},
                    {"thread_id",    r.thread_id},
                    {"start_us",     r.start_us},
                    {"end_us",       r.end_us},
                    {"inclusive_us",  inclusive_us},
                    {"call_count",   r.call_count},
                    {"frame_id",     r.frame_id}
                });
            }

            return {{"ok", true}, {"zones", zones}};
        }
    );
}

} // namespace uz
