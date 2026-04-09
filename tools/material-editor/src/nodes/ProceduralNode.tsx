/// Procedural node — CheckerPattern, NoisePerlin, Gradients.
/// Uses BaseExpressionNode directly since procedural nodes control scale via pin connections.

import { BaseExpressionNode, type ExprNodeProps } from './BaseExpressionNode';

export function ProceduralNode(props: ExprNodeProps) {
  return <BaseExpressionNode {...props} />;
}

export default ProceduralNode;
