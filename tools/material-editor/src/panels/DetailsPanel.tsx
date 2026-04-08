/// Details panel — UE5 style tabbed panel: Details + Parameters.
/// Details tab: material settings when no node selected, node properties when selected.
/// Parameters tab: material-level parameters.

import { useState } from 'react';
import type { MaterialExpression, UzMatFile, MaterialDomain, ShadingModel, BlendMode } from '../model/types';
import { matExprTypeToString } from '../model/types';

interface Props {
  material: UzMatFile;
  expression: MaterialExpression | null;
  onMaterialChange: (mat: UzMatFile) => void;
  onParamChange: (
    exprId: number,
    paramType: 'scalar' | 'string' | 'int',
    key: string,
    value: number | string,
  ) => void;
}

type TabId = 'details' | 'parameters';

const DOMAINS: MaterialDomain[] = ['Surface', 'PostProcess', 'Decal', 'UI'];
const SHADING_MODELS: ShadingModel[] = ['DefaultLit', 'Unlit', 'Subsurface'];
const BLEND_MODES: BlendMode[] = ['Opaque', 'Masked', 'Translucent'];

// ─────────────────────────────────────────────────────────────────────────────
// Material settings — shown when no node is selected (UE5 Details tab)
// ─────────────────────────────────────────────────────────────────────────────

function MaterialDetails({
  material,
  onMaterialChange,
}: {
  material: UzMatFile;
  onMaterialChange: (mat: UzMatFile) => void;
}) {
  function updateField<K extends keyof UzMatFile>(key: K, value: UzMatFile[K]) {
    onMaterialChange({ ...material, [key]: value });
  }

  return (
    <div className="details-section">
      <div className="details-section-header">
        <span className="details-collapse-icon">&#x25BC;</span>
        Material
      </div>

      <div className="details-row">
        <span className="details-label">Material Domain</span>
        <select
          className="details-select"
          value={material.domain}
          onChange={(e) => updateField('domain', e.target.value as MaterialDomain)}
        >
          {DOMAINS.map((d) => <option key={d} value={d}>{d}</option>)}
        </select>
      </div>

      <div className="details-row">
        <span className="details-label">Blend Mode</span>
        <select
          className="details-select"
          value={material.blendMode ?? 'Opaque'}
          onChange={(e) => updateField('blendMode', e.target.value as BlendMode)}
        >
          {BLEND_MODES.map((b) => <option key={b} value={b}>{b}</option>)}
        </select>
      </div>

      <div className="details-row">
        <span className="details-label">Shading Model</span>
        <select
          className="details-select"
          value={material.shadingModel}
          onChange={(e) => updateField('shadingModel', e.target.value as ShadingModel)}
        >
          {SHADING_MODELS.map((s) => <option key={s} value={s}>{s}</option>)}
        </select>
      </div>

      <div className="details-row">
        <span className="details-label">Two Sided</span>
        <input
          type="checkbox"
          className="details-checkbox"
          checked={material.twoSided ?? false}
          onChange={(e) => updateField('twoSided', e.target.checked)}
        />
      </div>
    </div>
  );
}

// ─────────────────────────────────────────────────────────────────────────────
// Node properties — shown when a node is selected
// ─────────────────────────────────────────────────────────────────────────────

function NodeDetails({
  expression,
  onParamChange,
}: {
  expression: MaterialExpression;
  onParamChange: Props['onParamChange'];
}) {
  const scalarKeys = Object.keys(expression.scalarParams).sort();
  const stringKeys = Object.keys(expression.stringParams).sort();
  const intKeys = Object.keys(expression.intParams).sort();
  const typeName = matExprTypeToString(expression.type);

  return (
    <>
      <div className="details-section">
        <div className="details-section-header">
          <span className="details-collapse-icon">&#x25BC;</span>
          Material Expression {typeName}
        </div>

        {scalarKeys.map((key) => (
          <div className="details-row" key={`s-${key}`}>
            <span className="details-label">{key}</span>
            <input
              className="details-input"
              type="number"
              step={0.01}
              value={expression.scalarParams[key]}
              onChange={(e) =>
                onParamChange(expression.id, 'scalar', key, parseFloat(e.target.value) || 0)
              }
            />
          </div>
        ))}

        {stringKeys.map((key) => (
          <div className="details-row" key={`str-${key}`}>
            <span className="details-label">{key}</span>
            <input
              className="details-input"
              value={expression.stringParams[key]}
              onChange={(e) =>
                onParamChange(expression.id, 'string', key, e.target.value)
              }
            />
          </div>
        ))}

        {intKeys.map((key) => (
          <div className="details-row" key={`i-${key}`}>
            <span className="details-label">{key}</span>
            <input
              className="details-input"
              type="number"
              step={1}
              value={expression.intParams[key]}
              onChange={(e) =>
                onParamChange(expression.id, 'int', key, parseInt(e.target.value) || 0)
              }
            />
          </div>
        ))}
      </div>

      <div className="details-section">
        <div className="details-section-header">
          <span className="details-collapse-icon">&#x25BC;</span>
          Material Expression
        </div>
        <div className="details-row">
          <span className="details-label">Desc</span>
          <input
            className="details-input"
            value={expression.name}
            onChange={(e) =>
              onParamChange(expression.id, 'string', '__name', e.target.value)
            }
          />
        </div>
      </div>
    </>
  );
}

// ─────────────────────────────────────────────────────────────────────────────
// Parameters tab — lists all Parameter-type expressions with their defaults
// ─────────────────────────────────────────────────────────────────────────────

function ParametersTab({
  material,
  onParamChange,
}: {
  material: UzMatFile;
  onParamChange: Props['onParamChange'];
}) {
  const paramExprs = material.expressions.filter(
    (e) => matExprTypeToString(e.type).includes('Parameter'),
  );

  if (paramExprs.length === 0) {
    return (
      <div className="details-empty">
        No material parameters defined. Add ScalarParameter, VectorParameter, or
        TextureParameter nodes to expose runtime-adjustable values.
      </div>
    );
  }

  return (
    <>
      {paramExprs.map((expr) => (
        <div className="details-section" key={expr.id}>
          <div className="details-section-header">
            <span className="details-collapse-icon">&#x25BC;</span>
            {expr.name}
          </div>
          {Object.entries(expr.scalarParams).map(([key, val]) => (
            <div className="details-row" key={key}>
              <span className="details-label">{key}</span>
              <input
                className="details-input"
                type="number"
                step={0.01}
                value={val}
                onChange={(e) =>
                  onParamChange(expr.id, 'scalar', key, parseFloat(e.target.value) || 0)
                }
              />
            </div>
          ))}
          {Object.entries(expr.stringParams).map(([key, val]) => (
            <div className="details-row" key={key}>
              <span className="details-label">{key}</span>
              <input
                className="details-input"
                value={val}
                onChange={(e) =>
                  onParamChange(expr.id, 'string', key, e.target.value)
                }
              />
            </div>
          ))}
        </div>
      ))}
    </>
  );
}

// ─────────────────────────────────────────────────────────────────────────────
// Main DetailsPanel with tabs
// ─────────────────────────────────────────────────────────────────────────────

export function DetailsPanel({
  material,
  expression,
  onMaterialChange,
  onParamChange,
}: Props) {
  const [activeTab, setActiveTab] = useState<TabId>('details');
  const [searchText, setSearchText] = useState('');

  return (
    <div className="details-panel">
      {/* Tab bar — UE5 style */}
      <div className="details-tab-bar">
        <button
          className={`details-tab ${activeTab === 'details' ? 'active' : ''}`}
          onClick={() => setActiveTab('details')}
        >
          <span className="details-tab-icon">&#x270E;</span> Details
        </button>
        <span className="details-tab-close">&#x2715;</span>
        <button
          className={`details-tab ${activeTab === 'parameters' ? 'active' : ''}`}
          onClick={() => setActiveTab('parameters')}
        >
          <span className="details-tab-icon">&#x270E;</span> Parameters
        </button>
      </div>

      {/* Search bar — UE5 style */}
      <div className="details-search">
        <input
          type="text"
          value={searchText}
          onChange={(e) => setSearchText(e.target.value)}
          className="details-search-input"
        />
      </div>

      {/* Tab content */}
      <div className="details-content">
        {activeTab === 'details' && (
          expression
            ? <NodeDetails expression={expression} onParamChange={onParamChange} />
            : <MaterialDetails material={material} onMaterialChange={onMaterialChange} />
        )}
        {activeTab === 'parameters' && (
          <ParametersTab material={material} onParamChange={onParamChange} />
        )}
      </div>
    </div>
  );
}

export default DetailsPanel;
