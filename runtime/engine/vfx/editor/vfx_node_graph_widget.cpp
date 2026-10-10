#include "vfx_node_graph_widget.h"

#include <algorithm>
#include <cmath>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// SetGraph
// ─────────────────────────────────────────────────────────────────────────────

void VfxNodeGraphWidget::SetGraph(VfxNodeGraph* graph) noexcept {
    graph_ = graph;
    selectedIds_.clear();
    wireDragActive_ = false;
    wireDragSource_.clear();
    rubberBandActive_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection
// ─────────────────────────────────────────────────────────────────────────────

bool VfxNodeGraphWidget::IsSelected(const std::string& id) const {
    return selectedIds_.count(id) > 0;
}

void VfxNodeGraphWidget::Select(const std::string& id) {
    selectedIds_.clear();
    selectedIds_.insert(id);
    if (onSelectionChanged) onSelectionChanged();
}

void VfxNodeGraphWidget::ToggleSelect(const std::string& id) {
    if (selectedIds_.count(id) > 0) {
        selectedIds_.erase(id);
    } else {
        selectedIds_.insert(id);
    }
    if (onSelectionChanged) onSelectionChanged();
}

void VfxNodeGraphWidget::SelectAll() {
    if (!graph_) return;
    selectedIds_.clear();
    for (const auto& node : graph_->nodes) {
        selectedIds_.insert(node.id);
    }
    if (onSelectionChanged) onSelectionChanged();
}

void VfxNodeGraphWidget::ClearSelection() {
    if (selectedIds_.empty()) return;
    selectedIds_.clear();
    if (onSelectionChanged) onSelectionChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
// Node movement
// ─────────────────────────────────────────────────────────────────────────────

void VfxNodeGraphWidget::MoveSelection(float dx, float dy) {
    if (!graph_ || selectedIds_.empty()) return;
    for (auto& node : graph_->nodes) {
        if (selectedIds_.count(node.id) > 0) {
            node.position.x += dx;
            node.position.y += dy;
        }
    }
    if (onGraphChanged) onGraphChanged();
}

void VfxNodeGraphWidget::NudgeSelection(float dx, float dy) {
    const float unit = (gridUnit > 0.0f) ? gridUnit : 1.0f;
    MoveSelection(dx * unit, dy * unit);
}

// ─────────────────────────────────────────────────────────────────────────────
// Deletion
// ─────────────────────────────────────────────────────────────────────────────

uint32_t VfxNodeGraphWidget::DeleteSelection() {
    if (!graph_ || selectedIds_.empty()) return 0;

    // Remove nodes.
    const auto& ids = selectedIds_;
    const size_t before = graph_->nodes.size();
    graph_->nodes.erase(
        std::remove_if(graph_->nodes.begin(), graph_->nodes.end(),
                        [&ids](const VfxGraphNode& n) {
                            return ids.count(n.id) > 0;
                        }),
        graph_->nodes.end());
    const size_t removed = before - graph_->nodes.size();

    // Remove wires touching the deleted nodes.
    graph_->wires.erase(
        std::remove_if(graph_->wires.begin(), graph_->wires.end(),
                        [&ids](const VfxGraphWire& w) {
                            // Extract node IDs from "nodeId:pinName" endpoints.
                            auto fromColon = w.fromEndpoint.find(':');
                            auto toColon   = w.toEndpoint.find(':');
                            std::string fromId = (fromColon != std::string::npos)
                                ? w.fromEndpoint.substr(0, fromColon)
                                : w.fromEndpoint;
                            std::string toId = (toColon != std::string::npos)
                                ? w.toEndpoint.substr(0, toColon)
                                : w.toEndpoint;
                            return ids.count(fromId) > 0 || ids.count(toId) > 0;
                        }),
        graph_->wires.end());

    selectedIds_.clear();
    if (onSelectionChanged) onSelectionChanged();
    if (onGraphChanged) onGraphChanged();
    return static_cast<uint32_t>(removed);
}

// ─────────────────────────────────────────────────────────────────────────────
// Wire drag
// ─────────────────────────────────────────────────────────────────────────────

void VfxNodeGraphWidget::BeginWireDrag(const std::string& pinEndpoint) {
    wireDragActive_ = true;
    wireDragSource_ = pinEndpoint;
}

bool VfxNodeGraphWidget::EndWireDrag(const std::string& targetPin, VfxPinType type) {
    const bool wasActive = wireDragActive_;
    const std::string source = wireDragSource_;
    wireDragActive_ = false;
    wireDragSource_.clear();

    if (!wasActive || !graph_) return false;
    if (source.empty() || targetPin.empty()) return false;

    // Reject self-loops on the same pin.
    if (source == targetPin) return false;

    // Reject connections on the same node.
    auto sourceColon = source.find(':');
    auto targetColon = targetPin.find(':');
    if (sourceColon == std::string::npos || targetColon == std::string::npos) return false;
    std::string sourceNode = source.substr(0, sourceColon);
    std::string targetNode = targetPin.substr(0, targetColon);
    if (sourceNode == targetNode) return false;

    // Create the wire.
    VfxGraphWire wire;
    wire.fromEndpoint = source;
    wire.toEndpoint   = targetPin;
    wire.type         = type;
    graph_->wires.push_back(std::move(wire));

    if (onGraphChanged) onGraphChanged();
    return true;
}

void VfxNodeGraphWidget::CancelWireDrag() {
    wireDragActive_ = false;
    wireDragSource_.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Rubber-band selection
// ─────────────────────────────────────────────────────────────────────────────

void VfxNodeGraphWidget::BeginRubberBand(float x, float y) {
    rubberBandActive_ = true;
    rbStartX_ = x; rbStartY_ = y;
    rbCurX_   = x; rbCurY_   = y;
}

void VfxNodeGraphWidget::UpdateRubberBand(float x, float y) {
    if (!rubberBandActive_) return;
    rbCurX_ = x;
    rbCurY_ = y;
}

void VfxNodeGraphWidget::EndRubberBand() {
    if (!rubberBandActive_ || !graph_) {
        rubberBandActive_ = false;
        return;
    }

    // Select every node whose position is inside the rect.
    const float xMin = std::min(rbStartX_, rbCurX_);
    const float xMax = std::max(rbStartX_, rbCurX_);
    const float yMin = std::min(rbStartY_, rbCurY_);
    const float yMax = std::max(rbStartY_, rbCurY_);

    selectedIds_.clear();
    for (const auto& node : graph_->nodes) {
        if (node.position.x >= xMin && node.position.x <= xMax &&
            node.position.y >= yMin && node.position.y <= yMax) {
            selectedIds_.insert(node.id);
        }
    }

    rubberBandActive_ = false;
    if (onSelectionChanged) onSelectionChanged();
}

void VfxNodeGraphWidget::GetRubberBandRect(
    float& outX, float& outY, float& outW, float& outH) const {
    outX = std::min(rbStartX_, rbCurX_);
    outY = std::min(rbStartY_, rbCurY_);
    outW = std::abs(rbCurX_ - rbStartX_);
    outH = std::abs(rbCurY_ - rbStartY_);
}

} // namespace uz
