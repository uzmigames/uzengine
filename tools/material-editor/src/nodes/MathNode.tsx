/// Math node — generic for binary/unary/ternary math operations.
/// Uses BaseExpressionNode directly since math nodes have no inline editors.

import { BaseExpressionNode, type ExprNodeProps } from './BaseExpressionNode';

export function MathNode(props: ExprNodeProps) {
  return <BaseExpressionNode {...props} />;
}

export default MathNode;
