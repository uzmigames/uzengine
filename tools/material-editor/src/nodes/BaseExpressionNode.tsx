/// Base expression node — UE5 Material Editor style.
/// Each pin row contains its own Handle inline, guaranteeing perfect alignment.

import { Handle, Position, type NodeProps } from '@xyflow/react';
import { type MatPin, MatExprType, pinTypeColor, type ExprCategory } from '../model/types';

const CATEGORY_COLORS: Record<ExprCategory, string> = {
  Output: '#7b2022',
  Constants: '#592a7a',
  Math: '#1b6b42',
  Texture: '#1b5e8a',
  Coordinates: '#8a6b1b',
  Procedural: '#1b7a6b',
  Utility: '#4a4a5a',
  Parameters: '#8a4b1b',
};

export interface ExprNodeData extends Record<string, unknown> {
  label: string;
  category: ExprCategory;
  exprId: number;
  exprType: MatExprType;
  inputs: MatPin[];
  outputs: MatPin[];
  scalarParams: Record<string, number>;
  stringParams: Record<string, string>;
  intParams: Record<string, number>;
  onParamChange?: (exprId: number, paramType: 'scalar' | 'string' | 'int', key: string, value: number | string) => void;
}

export type ExprNodeProps = NodeProps & { data: ExprNodeData };

export function BaseExpressionNode({
  data,
  selected,
  children,
}: ExprNodeProps & { children?: React.ReactNode }) {
  const headerColor = CATEGORY_COLORS[data.category];
  const inputPins = data.inputs;
  const outputPins = data.outputs;
  const maxRows = Math.max(inputPins.length, outputPins.length);

  return (
    <div className={`mat-node ${selected ? 'selected' : ''} ${data.category === 'Output' ? 'pbr-output' : ''}`}>
      <div className="mat-node-header" style={{ background: headerColor }}>
        {data.label}
      </div>
      <div className="mat-node-body">
        {Array.from({ length: maxRows }, (_, i) => {
          const inp = inputPins[i];
          const out = outputPins[i];
          return (
            <div key={i} className="mat-node-pin-pair">
              {/* Left: input pin */}
              {inp ? <PinHandle pin={inp} side="input" /> : <div className="mat-node-pin-spacer" />}
              {/* Right: output pin */}
              {out ? <PinHandle pin={out} side="output" /> : <div className="mat-node-pin-spacer" />}
            </div>
          );
        })}
        {children}
      </div>
    </div>
  );
}

function PinHandle({ pin, side }: { pin: MatPin; side: 'input' | 'output' }) {
  const color = pinTypeColor(pin.type);
  const handleId = side === 'input' ? `in-${pin.id}` : `out-${pin.id}`;
  const connected = side === 'input' && pin.connectedExprId !== 0;

  return (
    <div className={`mat-node-pin ${side}`}>
      <Handle
        type={side === 'input' ? 'target' : 'source'}
        position={side === 'input' ? Position.Left : Position.Right}
        id={handleId}
        className="uz-handle"
        style={{
          background: (connected || side === 'output') ? color : '#222',
          borderColor: color,
        }}
      />
      <span className="mat-node-pin-label">{pin.name}</span>
    </div>
  );
}

export default BaseExpressionNode;
