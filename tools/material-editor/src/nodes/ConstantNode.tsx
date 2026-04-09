/// Constant node — UE5-style: X Y Z fields at top, large color preview below.

import { BaseExpressionNode, type ExprNodeProps } from './BaseExpressionNode';
import { MatExprType } from '../model/types';

function ScalarEditor({ value, onChange }: { value: number; onChange: (v: number) => void }) {
  return (
    <input
      type="number"
      className="mat-node-inline-input"
      value={value}
      step={0.01}
      onChange={(e) => onChange(parseFloat(e.target.value) || 0)}
    />
  );
}

/// UE5-style vector constant: X Y Z input row at top + large color preview rectangle.
function VectorColorEditor({
  params,
  keys,
  labels,
  onChange,
}: {
  params: Record<string, number>;
  keys: string[];
  labels: string[];
  onChange: (key: string, value: number) => void;
}) {
  const r = Math.round(Math.min(1, Math.max(0, params[keys[0]] ?? 0)) * 255);
  const g = Math.round(Math.min(1, Math.max(0, params[keys[1]] ?? 0)) * 255);
  const b = Math.round(Math.min(1, Math.max(0, params[keys[2]] ?? 0)) * 255);
  const hex = `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`;

  return (
    <>
      {/* X Y Z input row — UE5 shows these at the top of the node */}
      <div className="mat-node-vector-fields">
        {keys.map((k, i) => (
          <div key={k} className="mat-node-vector-field">
            <span className="mat-node-vector-label">{labels[i]}</span>
            <input
              type="number"
              className="mat-node-vector-input"
              value={params[k] ?? 0}
              step={0.01}
              onChange={(e) => onChange(k, parseFloat(e.target.value) || 0)}
            />
          </div>
        ))}
      </div>
      {/* Large color preview — UE5's main visual element on constant nodes */}
      <div className="mat-node-color-preview" style={{ background: hex }}>
        <input
          type="color"
          value={hex}
          className="mat-node-color-input"
          onChange={(e) => {
            const v = e.target.value;
            onChange(keys[0], parseInt(v.slice(1, 3), 16) / 255);
            onChange(keys[1], parseInt(v.slice(3, 5), 16) / 255);
            onChange(keys[2], parseInt(v.slice(5, 7), 16) / 255);
          }}
        />
      </div>
    </>
  );
}

export function ConstantNode(props: ExprNodeProps) {
  const { data } = props;
  const handleScalarChange = (key: string, value: number) => {
    data.onParamChange?.(data.exprId, 'scalar', key, value);
  };

  return (
    <BaseExpressionNode {...props}>
      <div className="mat-node-inline-editor">
        {data.exprType === MatExprType.ScalarConstant && (
          <ScalarEditor
            value={data.scalarParams['value'] ?? 0}
            onChange={(v) => handleScalarChange('value', v)}
          />
        )}
        {data.exprType === MatExprType.Vector2Constant && (
          <div className="mat-node-vector-fields">
            {['x', 'y'].map((k) => (
              <div key={k} className="mat-node-vector-field">
                <span className="mat-node-vector-label">{k.toUpperCase()}</span>
                <input
                  type="number"
                  className="mat-node-vector-input"
                  value={data.scalarParams[k] ?? 0}
                  step={0.01}
                  onChange={(e) => handleScalarChange(k, parseFloat(e.target.value) || 0)}
                />
              </div>
            ))}
          </div>
        )}
        {data.exprType === MatExprType.Vector3Constant && (
          <VectorColorEditor
            params={data.scalarParams}
            keys={['x', 'y', 'z']}
            labels={['X', 'Y', 'Z']}
            onChange={handleScalarChange}
          />
        )}
        {data.exprType === MatExprType.Vector4Constant && (
          <VectorColorEditor
            params={data.scalarParams}
            keys={['x', 'y', 'z']}
            labels={['X', 'Y', 'Z']}
            onChange={handleScalarChange}
          />
        )}
        {data.exprType === MatExprType.ColorConstant && (
          <VectorColorEditor
            params={data.scalarParams}
            keys={['r', 'g', 'b']}
            labels={['R', 'G', 'B']}
            onChange={handleScalarChange}
          />
        )}
      </div>
    </BaseExpressionNode>
  );
}

export default ConstantNode;
