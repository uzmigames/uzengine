// shader_update_controller.h -- Event-driven shader compilation controller.
//
// Listens to GraphOps events, debounces rapid mutations, compiles the shader
// graph, and applies the result to the preview. On failure, retains the last
// good shader and pushes diagnostics.

#pragma once

#include "engine/renderer/shader_graph/graph_ops.h"
#include "engine/renderer/shader_graph/compiler/hlsl_translator.h"

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace uz::sg {

class ShaderGraphPreview;

// Diagnostics callback: receives errors/warnings and node context.
struct ShaderDiagnostic {
    bool is_error = true;
    std::string message;
    uint32_t node_id = 0;  // 0 = graph-level error
};

using DiagnosticsCallback = std::function<void(const std::vector<ShaderDiagnostic>&)>;
using CompileSuccessCallback = std::function<void(double compile_time_ms)>;

class ShaderUpdateController {
public:
    ShaderUpdateController(GraphOps& ops, ShaderGraphPreview& preview);
    ~ShaderUpdateController();

    // Start/stop listening to graph events.
    void start();
    void stop();
    bool is_running() const { return running_; }

    // Manual trigger (bypasses debounce).
    CompileResult compile_and_apply();

    // Tick — called once per frame. Handles debounced compilation.
    void tick();

    // Diagnostics callback.
    void set_diagnostics_callback(DiagnosticsCallback cb);
    void set_compile_success_callback(CompileSuccessCallback cb);

    // Debounce window (default 100ms).
    void set_debounce_ms(int32_t ms) { debounce_ms_ = ms; }
    int32_t debounce_ms() const { return debounce_ms_; }

    // Access last compilation result.
    const CompileResult& last_result() const { return last_result_; }

private:
    void on_graph_event(const GraphEvent& event);

    GraphOps& ops_;
    ShaderGraphPreview& preview_;

    bool running_ = false;
    bool compile_pending_ = false;
    int32_t debounce_ms_ = 100;

    using Clock = std::chrono::steady_clock;
    Clock::time_point last_event_time_;

    // Last good shader for fallback.
    std::string last_good_vs_;
    std::string last_good_ps_;

    CompileResult last_result_;

    DiagnosticsCallback diag_callback_;
    CompileSuccessCallback success_callback_;
};

} // namespace uz::sg
