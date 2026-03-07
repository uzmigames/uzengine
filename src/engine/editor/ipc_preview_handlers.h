#pragma once

// ipc_preview_handlers.h -- Register shader_graph.* IPC handlers for preview.
//
// Handlers:
//   shader_graph.preview        — { width?, height? }       -> { ok, viewportId }
//   shader_graph.stopPreview    — {}                        -> { ok }
//   shader_graph.setPreviewMesh — { mesh: "sphere"|"cube"|"plane" } -> { ok }
//   shader_graph.setPreviewCamera — { yaw?, pitch?, distance?, autoRotate? } -> { ok }
//   shader_graph.setPreviewParam  — { name, value }         -> { ok }
//   shader_graph.compile        — {}                        -> { ok, vertexHlsl, pixelHlsl, errors? }

namespace uz {

class IpcRegistry;

namespace sg {
class ShaderGraphPreview;
class ShaderUpdateController;
class GraphOps;
} // namespace sg

/// Register all shader_graph.* preview handlers.
void preview_register_handlers(IpcRegistry& registry,
                                sg::ShaderGraphPreview* preview,
                                sg::ShaderUpdateController* controller,
                                sg::GraphOps* ops);

} // namespace uz
