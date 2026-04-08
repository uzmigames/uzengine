/// Parameter node — ScalarParameter, VectorParameter, TextureParameter.
/// Shows the parameter name and default value editors.

import { BaseExpressionNode, type ExprNodeProps } from './BaseExpressionNode';
import { MatExprType } from '../model/types';

export function ParameterNode(props: ExprNodeProps) {
  const { data } = props;

  return (
    <BaseExpressionNode {...props}>
      <div className="mat-node-inline-editor">
        <div style={{ marginBottom: 4 }}>
          <input
            type="text"
            className="mat-node-inline-input"
            value={data.stringParams['paramName'] ?? ''}
            onChange={(e) =>
              data.onParamChange?.(data.exprId, 'string', 'paramName', e.target.value)
            }
          />
        </div>

        {data.exprType === MatExprType.ScalarParameter && (
          <input
            type="number"
            className="mat-node-inline-input"
            value={data.scalarParams['default'] ?? 0.5}
            step={0.01}
            onChange={(e) =>
              data.onParamChange?.(data.exprId, 'scalar', 'default', parseFloat(e.target.value) || 0)
            }
          />
        )}

        {data.exprType === MatExprType.VectorParameter && (
          <div style={{ display: 'flex', gap: 2 }}>
            {['defaultR', 'defaultG', 'defaultB'].map((k) => (
              <input
                key={k}
                type="number"
                className="mat-node-inline-input"
                value={data.scalarParams[k] ?? 0}
                step={0.01}
                title={k.replace('default', '')}
                onChange={(e) =>
                  data.onParamChange?.(data.exprId, 'scalar', k, parseFloat(e.target.value) || 0)
                }
              />
            ))}
          </div>
        )}

        {data.exprType === MatExprType.TextureParameter && (
          <input
            type="text"
            className="mat-node-inline-input"
            value={data.stringParams['texturePath'] ?? ''}
            onChange={(e) =>
              data.onParamChange?.(data.exprId, 'string', 'texturePath', e.target.value)
            }
          />
        )}
      </div>
    </BaseExpressionNode>
  );
}

export default ParameterNode;
