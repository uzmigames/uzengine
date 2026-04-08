/// PBR Output node — UE5 style: material name as title, inline default values on pins.
/// Shows default values next to unconnected pins (e.g., "Metallic 0.0", "Roughness 0.5").

import { Handle, Position } from '@xyflow/react';
import type { ExprNodeProps } from './BaseExpressionNode';
import { pinTypeColor, type MatPin } from '../model/types';

/// Default values for PBR Output pins (matching UE5 defaults)
const PIN_DEFAULTS: Record<string, string> = {
  BaseColor: '',
  Metallic: '0.0',
  Roughness: '0.5',
  Normal: '',
  Emissive: '',
  Opacity: '',
  AO: '1.0',
};

function PBRPinRow({ pin }: { pin: MatPin }) {
  const color = pinTypeColor(pin.type);
  const connected = pin.connectedExprId !== 0;
  const defaultVal = PIN_DEFAULTS[pin.name] ?? '';

  return (
    <div className="pbr-pin-row">
      <Handle
        type="target"
        position={Position.Left}
        id={`in-${pin.id}`}
        className="uz-handle"
        style={{
          background: connected ? color : '#222',
          borderColor: color,
        }}
      />
      <span className="pbr-pin-label">{pin.name}</span>
      {defaultVal && !connected && (
        <span className="pbr-pin-default">{defaultVal}</span>
      )}
    </div>
  );
}

export function PBROutputNode({ data, selected }: ExprNodeProps) {
  return (
    <div className={`mat-node pbr-output ${selected ? 'selected' : ''}`}>
      <div className="mat-node-header" style={{ background: '#7b2022' }}>
        {data.label}
      </div>
      <div className="mat-node-body">
        {data.inputs.map((pin) => (
          <PBRPinRow key={pin.id} pin={pin} />
        ))}
      </div>
    </div>
  );
}

export default PBROutputNode;
