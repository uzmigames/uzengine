/// Coordinate node — output-only nodes (TexCoord, WorldPosition, Time, etc.).
/// TexCoord has an inline UV index selector.

import { BaseExpressionNode, type ExprNodeProps } from './BaseExpressionNode';
import { MatExprType } from '../model/types';

export function CoordinateNode(props: ExprNodeProps) {
  const { data } = props;

  return (
    <BaseExpressionNode {...props}>
      {data.exprType === MatExprType.TexCoord && (
        <div className="mat-node-inline-editor">
          <div style={{ display: 'flex', alignItems: 'center', gap: 4, fontSize: 11 }}>
            <span style={{ color: 'var(--text-secondary)' }}>UV:</span>
            <select
              className="mat-node-inline-input"
              value={data.intParams['uvIndex'] ?? 0}
              onChange={(e) =>
                data.onParamChange?.(data.exprId, 'int', 'uvIndex', parseInt(e.target.value))
              }
            >
              <option value={0}>Channel 0</option>
              <option value={1}>Channel 1</option>
              <option value={2}>Channel 2</option>
              <option value={3}>Channel 3</option>
            </select>
          </div>
        </div>
      )}
    </BaseExpressionNode>
  );
}

export default CoordinateNode;
