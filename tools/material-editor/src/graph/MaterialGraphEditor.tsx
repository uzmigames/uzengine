/// Main React Flow graph canvas for the material editor.

import { useCallback, useRef, useState } from 'react';
import {
  ReactFlow,
  Background,
  Controls,
  MiniMap,
  BackgroundVariant,
  type ReactFlowInstance,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';

import PBROutputNode from '../nodes/PBROutputNode';
import ConstantNode from '../nodes/ConstantNode';
import TextureSampleNode from '../nodes/TextureSampleNode';
import MathNode from '../nodes/MathNode';
import CoordinateNode from '../nodes/CoordinateNode';
import ProceduralNode from '../nodes/ProceduralNode';
import UtilityNode from '../nodes/UtilityNode';
import ParameterNode from '../nodes/ParameterNode';
import TypedEdge from '../edges/TypedEdge';
import ContextMenu from '../panels/ContextMenu';
import type { UseMaterialGraphReturn } from './use-material-graph';
import { MatExprType } from '../model/types';

const nodeTypes = {
  pbrOutput: PBROutputNode,
  constant: ConstantNode,
  textureSample: TextureSampleNode,
  math: MathNode,
  coordinate: CoordinateNode,
  procedural: ProceduralNode,
  utility: UtilityNode,
  parameter: ParameterNode,
};

const edgeTypes = {
  typed: TypedEdge,
};

interface Props {
  graph: UseMaterialGraphReturn;
  onSelectNode: (exprId: number | null) => void;
}

export function MaterialGraphEditor({ graph, onSelectNode }: Props) {
  const reactFlowRef = useRef<HTMLDivElement>(null);
  const [rfInstance, setRfInstance] = useState<ReactFlowInstance | null>(null);
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number } | null>(null);

  const onInit = useCallback((instance: ReactFlowInstance) => {
    setRfInstance(instance);
    instance.fitView({ padding: 0.2 });
  }, []);

  const onPaneContextMenu = useCallback(
    (event: MouseEvent | React.MouseEvent) => {
      event.preventDefault();
      const clientX = 'clientX' in event ? event.clientX : 0;
      const clientY = 'clientY' in event ? event.clientY : 0;
      setContextMenu({ x: clientX, y: clientY });
    },
    [],
  );

  const onAddNode = useCallback(
    (type: MatExprType) => {
      if (!rfInstance || !contextMenu) return;
      const position = rfInstance.screenToFlowPosition({
        x: contextMenu.x,
        y: contextMenu.y,
      });
      graph.addNode(type, position.x, position.y);
      setContextMenu(null);
    },
    [rfInstance, contextMenu, graph],
  );

  const onNodeClick = useCallback(
    (_: React.MouseEvent, node: { id: string }) => {
      const exprId = parseInt(node.id.replace('expr-', ''));
      onSelectNode(exprId);
    },
    [onSelectNode],
  );

  const onPaneClick = useCallback(() => {
    onSelectNode(null);
    setContextMenu(null);
  }, [onSelectNode]);

  const onKeyDown = useCallback(
    (event: React.KeyboardEvent) => {
      if (event.ctrlKey && event.key === 'z') {
        event.preventDefault();
        graph.undo();
      } else if (event.ctrlKey && event.key === 'y') {
        event.preventDefault();
        graph.redo();
      }
    },
    [graph],
  );

  return (
    <div className="graph-canvas" ref={reactFlowRef} onKeyDown={onKeyDown} tabIndex={0}>
      <ReactFlow
        nodes={graph.nodes}
        edges={graph.edges}
        onNodesChange={graph.onNodesChange}
        onEdgesChange={graph.onEdgesChange}
        onConnect={graph.onConnect}
        onInit={onInit}
        onNodeClick={onNodeClick}
        onPaneClick={onPaneClick}
        onPaneContextMenu={onPaneContextMenu}
        nodeTypes={nodeTypes}
        edgeTypes={edgeTypes}
        defaultEdgeOptions={{ type: 'typed' }}
        fitView
        snapToGrid
        snapGrid={[10, 10]}
        deleteKeyCode="Delete"
        proOptions={{ hideAttribution: true }}
      >
        <Background variant={BackgroundVariant.Dots} gap={16} size={0.5} color="#2a2a2a" />
        <Background id="grid-large" variant={BackgroundVariant.Lines} gap={160} size={0.3} color="#1e1e1e" />
        <Controls />
        <MiniMap
          nodeColor="#444"
          maskColor="rgba(0, 0, 0, 0.6)"
          style={{ background: '#1a1a1a' }}
        />
      </ReactFlow>

      {contextMenu && (
        <ContextMenu
          x={contextMenu.x}
          y={contextMenu.y}
          onSelect={onAddNode}
          onClose={() => setContextMenu(null)}
        />
      )}
    </div>
  );
}

export default MaterialGraphEditor;
