/// Texture Sample node — shows texture path and 5 output pins (RGBA, R, G, B, A).

import { BaseExpressionNode, type ExprNodeProps } from './BaseExpressionNode';

export function TextureSampleNode(props: ExprNodeProps) {
  const { data } = props;
  const texturePath = data.stringParams['texturePath'] ?? '';
  const fileName = texturePath
    ? texturePath.split('/').pop() ?? texturePath
    : 'empty';

  return (
    <BaseExpressionNode {...props}>
      <div className="mat-node-texture-path" title={texturePath || 'No texture assigned'}>
        {fileName}
      </div>
      <div className="mat-node-inline-editor">
        <input
          type="text"
          className="mat-node-inline-input"
          value={texturePath}
          onChange={(e) =>
            data.onParamChange?.(data.exprId, 'string', 'texturePath', e.target.value)
          }
        />
      </div>
    </BaseExpressionNode>
  );
}

export default TextureSampleNode;
