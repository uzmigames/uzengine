#pragma once

/// VFX Node-Graph Editor Widget — interaction model for visual graph editing.
///
/// Phase 10.4.0. Provides selection, drag-move, wire creation/deletion,
/// clipboard integration, and keyboard binding state on top of the
/// phase 8.6/8.10/8.15 `VfxNodeGraph` data model. The overlay host renders
/// the widget's state (node cards, wires, selection rects) using the
/// TextRenderer; this class owns the interaction logic only.
///
/// # Rendering responsibilities (overlay host)
///
/// The widget exposes const accessors so the overlay can render:
/// - Node cards at `GetNodePosition(id)` with selection highlight.
/// - Wire curves between `GetWireEndpoints()`.
/// - The rubber-band selection rect when `IsRubberBandActive()`.
/// - The wire-drag preview when `IsWireDragActive()`.
///
/// # Interaction model
///
/// - **Click** on a node → select it (clear previous selection).
/// - **Shift+click** → toggle multi-select.
/// - **Click+drag on canvas** → rubber-band multi-select.
/// - **Click+drag on a selected node** → move selection.
/// - **Click+drag on a pin** → start wire creation; drop on another
///   pin to connect.
/// - **Delete key** → remove selected nodes + their wires.
/// - **Ctrl+A** → select all.
/// - **Ctrl+C / Ctrl+V / Ctrl+D** → clipboard (phase 9.3 consumer).
/// - **Arrow keys** → nudge selection by grid unit.
/// - **Escape** → clear selection.
///
/// @ref UE5 SGraphPanel + SGraphNode
/// @ref engine/vfx/editor/vfx_node_graph.h (data model)

#include "engine/vfx/editor/vfx_node_graph.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Widget
// ─────────────────────────────────────────────────────────────────────────────

class VfxNodeGraphWidget {
public:
    VfxNodeGraphWidget() = default;

    /// Binds the widget to a graph data model. The graph must outlive the
    /// widget. Call again to rebind (e.g. on emitter switch).
    void SetGraph(VfxNodeGraph* graph) noexcept;

    /// Returns the currently bound graph (nullptr when unbound).
    [[nodiscard]] VfxNodeGraph* GetGraph() const noexcept { return graph_; }

    // ── Canvas transform ────────────────────────────────────────────────

    /// Pan offset in screen pixels.
    float panX = 0.0f;
    float panY = 0.0f;

    /// Zoom level (1.0 = 100%).
    float zoom = 1.0f;

    /// Grid snap unit (pixels at zoom=1). 0 = no snap.
    float gridUnit = 16.0f;

    // ── Selection ───────────────────────────────────────────────────────

    /// Returns true when the node with `id` is in the selection set.
    [[nodiscard]] bool IsSelected(const std::string& id) const;

    /// Selects a single node (clears previous selection).
    void Select(const std::string& id);

    /// Toggles a node in the selection set (for Shift+click).
    void ToggleSelect(const std::string& id);

    /// Selects all nodes in the graph.
    void SelectAll();

    /// Clears the selection set.
    void ClearSelection();

    /// Returns the number of selected nodes.
    [[nodiscard]] uint32_t SelectionCount() const noexcept {
        return static_cast<uint32_t>(selectedIds_.size());
    }

    /// Returns the selected node IDs (for clipboard, delete, etc.).
    [[nodiscard]] const std::unordered_set<std::string>& SelectedIds() const noexcept {
        return selectedIds_;
    }

    // ── Node movement ───────────────────────────────────────────────────

    /// Moves all selected nodes by `(dx, dy)` in graph coordinates.
    /// Writes back to `VfxNodeGraph::nodes[].position`.
    void MoveSelection(float dx, float dy);

    /// Nudges all selected nodes by one grid unit in the given direction.
    void NudgeSelection(float dx, float dy);

    // ── Node/wire deletion ──────────────────────────────────────────────

    /// Deletes all selected nodes and any wires touching them.
    /// Returns the number of nodes removed.
    uint32_t DeleteSelection();

    // ── Wire creation ───────────────────────────────────────────────────

    /// Returns true when the user is dragging a wire from a pin.
    [[nodiscard]] bool IsWireDragActive() const noexcept { return wireDragActive_; }

    /// Starts a wire drag from `pinEndpoint` (e.g. "n0:output").
    void BeginWireDrag(const std::string& pinEndpoint);

    /// Completes the wire drag onto `targetPin`. Creates a new wire
    /// if the connection is valid (different nodes, compatible types).
    /// Returns true when a wire was created.
    bool EndWireDrag(const std::string& targetPin, VfxPinType type);

    /// Cancels the wire drag.
    void CancelWireDrag();

    /// Returns the source pin endpoint of the active wire drag.
    [[nodiscard]] const std::string& WireDragSource() const noexcept {
        return wireDragSource_;
    }

    // ── Rubber-band selection ───────────────────────────────────────────

    [[nodiscard]] bool IsRubberBandActive() const noexcept { return rubberBandActive_; }

    void BeginRubberBand(float x, float y);
    void UpdateRubberBand(float x, float y);
    void EndRubberBand();

    /// Returns the rubber-band rect in graph coordinates (for rendering).
    void GetRubberBandRect(float& outX, float& outY, float& outW, float& outH) const;

    // ── Callbacks ───────────────────────────────────────────────────────

    /// Fires when the selection changes (for inspector refresh).
    std::function<void()> onSelectionChanged;

    /// Fires when a wire is created or deleted (for recompile trigger).
    std::function<void()> onGraphChanged;

    /// Fires on Ctrl+C with the selected node IDs. The host wires this
    /// to `VfxNodeGraphClipboard::Copy()` (phase 9.3).
    std::function<void(const std::unordered_set<std::string>& ids)> onCopy;

    /// Fires on Ctrl+V. The host wires this to clipboard Paste.
    std::function<void()> onPaste;

private:
    VfxNodeGraph* graph_ = nullptr;
    std::unordered_set<std::string> selectedIds_;

    // Wire drag state.
    bool        wireDragActive_ = false;
    std::string wireDragSource_;

    // Rubber-band state.
    bool  rubberBandActive_ = false;
    float rbStartX_ = 0, rbStartY_ = 0;
    float rbCurX_   = 0, rbCurY_   = 0;
};

} // namespace uz
