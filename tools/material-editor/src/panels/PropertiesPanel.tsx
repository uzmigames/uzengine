/// Properties panel — shows editable parameters for the selected expression node.

import type { MaterialExpression } from '../model/types';
import { matExprTypeToString } from '../model/types';

interface Props {
  expression: MaterialExpression | null;
  onParamChange: (
    exprId: number,
    paramType: 'scalar' | 'string' | 'int',
    key: string,
    value: number | string,
  ) => void;
}

export function PropertiesPanel({ expression, onParamChange }: Props) {
  if (!expression) {
    return (
      <div className="panel-section">
        <div className="panel-section-title">Properties</div>
        <div style={{ color: 'var(--text-muted)', fontSize: 12, padding: '8px 0' }}>
          Select a node to view properties
        </div>
      </div>
    );
  }

  const scalarKeys = Object.keys(expression.scalarParams).sort();
  const stringKeys = Object.keys(expression.stringParams).sort();
  const intKeys = Object.keys(expression.intParams).sort();

  return (
    <div className="panel-section">
      <div className="panel-section-title">Properties</div>

      <div className="prop-row">
        <span className="prop-label">Type</span>
        <span style={{ fontSize: 12, color: 'var(--text-primary)' }}>
          {matExprTypeToString(expression.type)}
        </span>
      </div>

      <div className="prop-row">
        <span className="prop-label">ID</span>
        <span style={{ fontSize: 12, color: 'var(--text-secondary)' }}>
          {expression.id}
        </span>
      </div>

      <div className="prop-row">
        <span className="prop-label">Name</span>
        <input
          className="prop-input"
          value={expression.name}
          onChange={(e) =>
            onParamChange(expression.id, 'string', '__name', e.target.value)
          }
        />
      </div>

      {scalarKeys.length > 0 && (
        <>
          <div className="panel-section-title" style={{ marginTop: 8 }}>Scalar Params</div>
          {scalarKeys.map((key) => (
            <div className="prop-row" key={key}>
              <span className="prop-label">{key}</span>
              <input
                className="prop-input"
                type="number"
                step={0.01}
                value={expression.scalarParams[key]}
                onChange={(e) =>
                  onParamChange(expression.id, 'scalar', key, parseFloat(e.target.value) || 0)
                }
              />
            </div>
          ))}
        </>
      )}

      {stringKeys.length > 0 && (
        <>
          <div className="panel-section-title" style={{ marginTop: 8 }}>String Params</div>
          {stringKeys.map((key) => (
            <div className="prop-row" key={key}>
              <span className="prop-label">{key}</span>
              <input
                className="prop-input"
                value={expression.stringParams[key]}
                onChange={(e) =>
                  onParamChange(expression.id, 'string', key, e.target.value)
                }
              />
            </div>
          ))}
        </>
      )}

      {intKeys.length > 0 && (
        <>
          <div className="panel-section-title" style={{ marginTop: 8 }}>Int Params</div>
          {intKeys.map((key) => (
            <div className="prop-row" key={key}>
              <span className="prop-label">{key}</span>
              <input
                className="prop-input"
                type="number"
                step={1}
                value={expression.intParams[key]}
                onChange={(e) =>
                  onParamChange(expression.id, 'int', key, parseInt(e.target.value) || 0)
                }
              />
            </div>
          ))}
        </>
      )}

      <div className="panel-section-title" style={{ marginTop: 8 }}>Connections</div>
      {expression.inputs.map((pin) => (
        <div className="prop-row" key={pin.id}>
          <span className="prop-label">{pin.name}</span>
          <span style={{ fontSize: 11, color: pin.connectedExprId ? 'var(--text-primary)' : 'var(--text-muted)' }}>
            {pin.connectedExprId
              ? `expr #${pin.connectedExprId} pin ${pin.connectedPinId}`
              : 'unconnected'}
          </span>
        </div>
      ))}
    </div>
  );
}

export default PropertiesPanel;
