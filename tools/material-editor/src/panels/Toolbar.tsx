/// Material editor toolbar — UE5 style: action buttons row + breadcrumb navbar.

import type { UzMatFile } from '../model/types';

interface Props {
  material: UzMatFile;
  onLoad: () => void;
  onSave: () => void;
  onNew: () => void;
  canUndo: boolean;
  canRedo: boolean;
  onUndo: () => void;
  onRedo: () => void;
}

export function Toolbar({
  material,
  onLoad,
  onSave,
  onNew,
  canUndo,
  canRedo,
  onUndo,
  onRedo,
}: Props) {
  return (
    <div className="ue-toolbar-container">
      {/* Action buttons row */}
      <div className="ue-toolbar">
        <button className="ue-toolbar-btn" onClick={onSave} title="Apply changes">
          <span className="ue-toolbar-icon">&#x2714;</span> Apply
        </button>
        <div className="toolbar-separator" />
        <button className="ue-toolbar-btn" onClick={onNew} title="New material">New</button>
        <button className="ue-toolbar-btn" onClick={onLoad} title="Open .uzmat file">Open</button>
        <button className="ue-toolbar-btn" onClick={onSave} title="Save .uzmat file (Ctrl+S)">Save</button>
        <div className="toolbar-separator" />
        <button className="ue-toolbar-btn" onClick={onUndo} disabled={!canUndo} title="Undo (Ctrl+Z)">
          <span className="ue-toolbar-icon">&#x21B6;</span> Undo
        </button>
        <button className="ue-toolbar-btn" onClick={onRedo} disabled={!canRedo} title="Redo (Ctrl+Y)">
          <span className="ue-toolbar-icon">&#x21B7;</span> Redo
        </button>
        <div className="toolbar-separator" />
        <button className="ue-toolbar-btn" title="Clean Graph">Clean Graph</button>
        <button className="ue-toolbar-btn" title="Hide Unrelated">Hide Unrelated</button>
      </div>

      {/* Breadcrumb navbar — UE5 style */}
      <div className="ue-breadcrumb">
        <span className="ue-breadcrumb-icon">&#x2B1A;</span>
        <span className="ue-breadcrumb-item">{material.name || 'NewMaterial'}</span>
        <span className="ue-breadcrumb-sep">&#x276F;</span>
        <span className="ue-breadcrumb-item active">Material Graph</span>
      </div>
    </div>
  );
}

export default Toolbar;
