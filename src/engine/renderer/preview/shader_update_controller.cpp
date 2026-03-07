// shader_update_controller.cpp -- Event-driven shader compilation controller.

#include "engine/renderer/preview/shader_update_controller.h"
#include "engine/renderer/preview/shader_graph_preview.h"

namespace uz::sg {

ShaderUpdateController::ShaderUpdateController(GraphOps& ops, ShaderGraphPreview& preview)
    : ops_(ops), preview_(preview) {}

ShaderUpdateController::~ShaderUpdateController() {
    if (running_) stop();
}

void ShaderUpdateController::start() {
    if (running_) return;
    running_ = true;
    compile_pending_ = false;

    // Register as event listener.
    ops_.set_event_listener([this](const GraphEvent& e) {
        on_graph_event(e);
    });

    // Initial compile.
    compile_and_apply();
}

void ShaderUpdateController::stop() {
    if (!running_) return;
    running_ = false;
    compile_pending_ = false;
    ops_.set_event_listener(nullptr);
}

void ShaderUpdateController::on_graph_event(const GraphEvent& /*event*/) {
    compile_pending_ = true;
    last_event_time_ = Clock::now();
}

void ShaderUpdateController::tick() {
    if (!running_ || !compile_pending_) return;

    // Check debounce window.
    auto now = Clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_event_time_).count();
    if (elapsed < debounce_ms_) return;

    compile_pending_ = false;
    compile_and_apply();
}

CompileResult ShaderUpdateController::compile_and_apply() {
    auto start = Clock::now();

    HlslTranslator translator;
    last_result_ = translator.compile(ops_.graph());

    auto end = Clock::now();
    double compile_ms = std::chrono::duration<double, std::milli>(end - start).count();

    if (last_result_.ok) {
        // Apply to preview.
        bool applied = preview_.apply_compiled_shader(
            last_result_.vertex_hlsl, last_result_.pixel_hlsl);

        if (applied) {
            last_good_vs_ = last_result_.vertex_hlsl;
            last_good_ps_ = last_result_.pixel_hlsl;
        }

        if (success_callback_) {
            success_callback_(compile_ms);
        }

        // Push empty diagnostics (clear previous errors).
        if (diag_callback_) {
            diag_callback_({});
        }
    } else {
        // Compilation failed — retain fallback shader.
        if (!last_good_vs_.empty() && !last_good_ps_.empty()) {
            preview_.apply_compiled_shader(last_good_vs_, last_good_ps_);
        }

        // Push diagnostics.
        if (diag_callback_) {
            std::vector<ShaderDiagnostic> diags;
            for (auto& err : last_result_.errors) {
                diags.push_back({true, err, 0});
            }
            diag_callback_(diags);
        }
    }

    return last_result_;
}

void ShaderUpdateController::set_diagnostics_callback(DiagnosticsCallback cb) {
    diag_callback_ = std::move(cb);
}

void ShaderUpdateController::set_compile_success_callback(CompileSuccessCallback cb) {
    success_callback_ = std::move(cb);
}

} // namespace uz::sg
