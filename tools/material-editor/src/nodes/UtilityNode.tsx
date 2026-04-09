/// Utility node — Fresnel, Panner, TriPlanar, ComponentMask, Append.
/// ComponentMask has inline RGBA channel toggles.

import { BaseExpressionNode, type ExprNodeProps } from './BaseExpressionNode';
import { MatExprType } from '../model/types';

function ComponentMaskEditor({
  intParams,
  exprId,
  onParamChange,
}: {
  intParams: Record<string, number>;
  exprId: number;
  onParamChange?: (exprId: number, paramType: 'scalar' | 'string' | 'int', key: string, value: number | string) => void;
}) {
  const channels = ['r', 'g', 'b', 'a'] as const;
  const channelColors = { r: '#ff6b6b', g: '#73d977', b: '#86c8ff', a: '#a1a1a1' };

  return (
    <div className="mat-node-inline-editor" style={{ display: 'flex', gap: 4 }}>
      {channels.map((ch) => (
        <label
          key={ch}
          style={{
            display: 'flex',
            alignItems: 'center',
            gap: 2,
            cursor: 'pointer',
            color: (intParams[ch] ?? 0) ? channelColors[ch] : 'var(--text-muted)',
            fontWeight: 600,
            fontSize: 11,
          }}
        >
          <input
            type="checkbox"
            checked={(intParams[ch] ?? 0) === 1}
            onChange={(e) =>
              onParamChange?.(exprId, 'int', ch, e.target.checked ? 1 : 0)
            }
            style={{ width: 12, height: 12 }}
          />
          {ch.toUpperCase()}
        </label>
      ))}
    </div>
  );
}

export function UtilityNode(props: ExprNodeProps) {
  const { data } = props;

  return (
    <BaseExpressionNode {...props}>
      {data.exprType === MatExprType.ComponentMask && (
        <ComponentMaskEditor
          intParams={data.intParams}
          exprId={data.exprId}
          onParamChange={data.onParamChange}
        />
      )}
    </BaseExpressionNode>
  );
}

export default UtilityNode;
