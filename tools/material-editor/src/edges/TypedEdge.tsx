/// Typed edge — bezier curve color-coded by source pin type.

import { BezierEdge, type EdgeProps } from '@xyflow/react';

export interface TypedEdgeData {
  pinColor: string;
}

export function TypedEdge(props: EdgeProps) {
  const data = props.data as TypedEdgeData | undefined;
  const color = data?.pinColor ?? 'var(--edge-default)';

  return (
    <BezierEdge
      {...props}
      style={{
        stroke: color,
        strokeWidth: 2,
        ...props.style,
      }}
    />
  );
}

export default TypedEdge;
