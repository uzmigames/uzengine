/// React hook: converts MaterialGraph model ↔ React Flow nodes/edges.
/// Manages CRUD operations and undo/redo stack.

import { useCallback, useRef, useState } from 'react';
import {
  type Node,
  type Edge,
  type OnNodesChange,
  type OnEdgesChange,
  type OnConnect,
  applyNodeChanges,
  applyEdgeChanges,
} from '@xyflow/react';

import {
  type UzMatFile,
  type MaterialExpression,
  MatExprType,
  getExprCategory,
  pinTypeColor,
} from '../model/types';
import type { ExprNodeData } from '../nodes/BaseExpressionNode';
import {
  addExpression as addExprToModel,
  removeExpression,
  connect as connectModel,
  disconnect as disconnectModel,
} from '../model/material-graph';

// ─────────────────────────────────────────────────────────────────────────────
// Node type mapping: MatExprType → React Flow node type string
// ─────────────────────────────────────────────────────────────────────────────

function nodeTypeForExpr(type: MatExprType): string {
  if (type === MatExprType.PBROutput) return 'pbrOutput';
  if (type === MatExprType.TextureSample) return 'textureSample';
  if (type >= 100 && type < 200) return 'constant';
  if (type >= 200 && type < 300) return 'math';
  if (type >= 400 && type < 500) return 'coordinate';
  if (type >= 500 && type < 600) return 'procedural';
  if (type >= 600 && type < 700) return 'utility';
  if (type >= 700 && type < 800) return 'parameter';
  return 'math';
}

// ─────────────────────────────────────────────────────────────────────────────
// Convert model → React Flow nodes/edges
// ─────────────────────────────────────────────────────────────────────────────

function exprToNode(
  expr: MaterialExpression,
  onParamChange: ExprNodeData['onParamChange'],
  materialName?: string,
): Node {
  const data: ExprNodeData = {
    label: expr.type === MatExprType.PBROutput ? (materialName || expr.name) : expr.name,
    category: getExprCategory(expr.type),
    exprId: expr.id,
    exprType: expr.type,
    inputs: expr.inputs,
    outputs: expr.outputs,
    scalarParams: expr.scalarParams,
    stringParams: expr.stringParams,
    intParams: expr.intParams,
    onParamChange,
  };

  return {
    id: `expr-${expr.id}`,
    type: nodeTypeForExpr(expr.type),
    position: { x: expr.posX, y: expr.posY },
    data,
    deletable: expr.type !== MatExprType.PBROutput,
  };
}

function connectionsToEdges(expressions: MaterialExpression[]): Edge[] {
  const edges: Edge[] = [];

  for (const expr of expressions) {
    for (const pin of expr.inputs) {
      if (pin.connectedExprId === 0) continue;

      const srcExpr = expressions.find((e) => e.id === pin.connectedExprId);
      if (!srcExpr) continue;

      const srcPin = srcExpr.outputs.find((p) => p.id === pin.connectedPinId);
      const color = srcPin ? pinTypeColor(srcPin.type) : 'var(--edge-default)';

      edges.push({
        id: `edge-${pin.connectedExprId}-${pin.connectedPinId}-${expr.id}-${pin.id}`,
        source: `expr-${pin.connectedExprId}`,
        sourceHandle: `out-${pin.connectedPinId}`,
        target: `expr-${expr.id}`,
        targetHandle: `in-${pin.id}`,
        type: 'typed',
        data: { pinColor: color },
      });
    }
  }

  return edges;
}

// ─────────────────────────────────────────────────────────────────────────────
// Simple auto-layout for files without position data
// Places nodes in layers from left to right based on dependency depth.
// ─────────────────────────────────────────────────────────────────────────────

function autoLayout(mat: UzMatFile): void {
  const hasPositions = mat.expressions.some((e) => e.posX !== 0 || e.posY !== 0);
  if (hasPositions) return;

  // Compute depth per expression via BFS from PBROutput
  const depthMap = new Map<number, number>();
  const pbrOutput = mat.expressions.find((e) => e.type === MatExprType.PBROutput);
  if (!pbrOutput) return;

  const queue: [number, number][] = [[pbrOutput.id, 0]];
  const visited = new Set<number>();

  while (queue.length > 0) {
    const [exprId, depth] = queue.shift()!;
    if (visited.has(exprId)) {
      // Keep the maximum depth (leftmost placement for sources)
      const existing = depthMap.get(exprId) ?? 0;
      depthMap.set(exprId, Math.max(existing, depth));
      continue;
    }
    visited.add(exprId);
    depthMap.set(exprId, depth);

    const expr = mat.expressions.find((e) => e.id === exprId);
    if (!expr) continue;

    for (const pin of expr.inputs) {
      if (pin.connectedExprId !== 0) {
        queue.push([pin.connectedExprId, depth + 1]);
      }
    }
  }

  // Assign positions: PBROutput on right, sources on left
  const maxDepth = Math.max(...Array.from(depthMap.values()), 0);
  const layerWidth = 280;
  const nodeHeight = 120;

  // Group by depth
  const layers = new Map<number, MaterialExpression[]>();
  for (const expr of mat.expressions) {
    const depth = depthMap.get(expr.id) ?? 0;
    const layer = maxDepth - depth; // reverse so PBROutput is on the right
    if (!layers.has(layer)) layers.set(layer, []);
    layers.get(layer)!.push(expr);
  }

  for (const [layer, exprs] of layers) {
    for (let i = 0; i < exprs.length; i++) {
      exprs[i].posX = layer * layerWidth + 50;
      exprs[i].posY = i * nodeHeight + 50;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Hook
// ─────────────────────────────────────────────────────────────────────────────

export interface UseMaterialGraphReturn {
  nodes: Node[];
  edges: Edge[];
  material: UzMatFile;
  onNodesChange: OnNodesChange;
  onEdgesChange: OnEdgesChange;
  onConnect: OnConnect;
  setMaterial: (mat: UzMatFile) => void;
  addNode: (type: MatExprType, x: number, y: number) => void;
  deleteNode: (exprId: number) => void;
  updateParam: (exprId: number, paramType: 'scalar' | 'string' | 'int', key: string, value: number | string) => void;
  undo: () => void;
  redo: () => void;
  canUndo: boolean;
  canRedo: boolean;
}

export function useMaterialGraph(initialMaterial: UzMatFile): UseMaterialGraphReturn {
  // Run auto-layout on first load
  autoLayout(initialMaterial);

  const [material, setMaterialState] = useState<UzMatFile>(initialMaterial);
  const [nodes, setNodes] = useState<Node[]>(() =>
    initialMaterial.expressions.map((e) => exprToNode(e, handleParamChange, initialMaterial.name)),
  );
  const [edges, setEdges] = useState<Edge[]>(() =>
    connectionsToEdges(initialMaterial.expressions),
  );

  // Undo/redo stacks store serialized material snapshots
  const undoStack = useRef<string[]>([]);
  const redoStack = useRef<string[]>([]);

  function pushUndo(mat: UzMatFile) {
    undoStack.current.push(JSON.stringify(mat));
    if (undoStack.current.length > 50) undoStack.current.shift();
    redoStack.current = [];
  }

  function rebuildFromModel(mat: UzMatFile) {
    setMaterialState(mat);
    setNodes(mat.expressions.map((e) => exprToNode(e, handleParamChange, material.name)));
    setEdges(connectionsToEdges(mat.expressions));
  }

  // eslint-disable-next-line react-hooks/exhaustive-deps
  function handleParamChange(
    exprId: number,
    paramType: 'scalar' | 'string' | 'int',
    key: string,
    value: number | string,
  ) {
    setMaterialState((prev) => {
      const next = structuredClone(prev);
      const expr = next.expressions.find((e) => e.id === exprId);
      if (!expr) return prev;

      pushUndo(prev);

      if (paramType === 'scalar') expr.scalarParams[key] = value as number;
      else if (paramType === 'string') expr.stringParams[key] = value as string;
      else if (paramType === 'int') expr.intParams[key] = value as number;

      setNodes(next.expressions.map((e) => exprToNode(e, handleParamChange, material.name)));
      return next;
    });
  }

  const onNodesChange: OnNodesChange = useCallback(
    (changes) => {
      let needsRebuild = false;

      setNodes((nds) => {
        const updated = applyNodeChanges(changes, nds);

        // Sync position changes back to the model
        for (const change of changes) {
          if (change.type === 'position' && change.position) {
            const exprId = parseInt(change.id.replace('expr-', ''));
            const expr = material.expressions.find((e) => e.id === exprId);
            if (expr) {
              expr.posX = change.position.x;
              expr.posY = change.position.y;
            }
          }
          if (change.type === 'remove') {
            const exprId = parseInt(change.id.replace('expr-', ''));
            pushUndo(material);
            removeExpression(material, exprId);
            needsRebuild = true;
          }
        }

        return updated;
      });

      if (needsRebuild) {
        rebuildFromModel(structuredClone(material));
      }
    },
    [material],
  );

  const onEdgesChange: OnEdgesChange = useCallback(
    (changes) => {
      let needsRebuild = false;

      setEdges((eds) => {
        const updated = applyEdgeChanges(changes, eds);

        for (const change of changes) {
          if (change.type === 'remove') {
            const edge = eds.find((e) => e.id === change.id);
            if (edge) {
              const dstExprId = parseInt(edge.target.replace('expr-', ''));
              const dstPinId = parseInt(edge.targetHandle?.replace('in-', '') ?? '0');
              pushUndo(material);
              disconnectModel(material, dstExprId, dstPinId);
              needsRebuild = true;
            }
          }
        }

        return updated;
      });

      if (needsRebuild) {
        rebuildFromModel(structuredClone(material));
      }
    },
    [material],
  );

  const onConnect: OnConnect = useCallback(
    (connection) => {
      const srcExprId = parseInt(connection.source.replace('expr-', ''));
      const srcPinId = parseInt(connection.sourceHandle?.replace('out-', '') ?? '0');
      const dstExprId = parseInt(connection.target.replace('expr-', ''));
      const dstPinId = parseInt(connection.targetHandle?.replace('in-', '') ?? '0');

      pushUndo(material);
      const success = connectModel(material, srcExprId, srcPinId, dstExprId, dstPinId);
      if (success) {
        rebuildFromModel(structuredClone(material));
      }
    },
    [material],
  );

  const setMaterial = useCallback((mat: UzMatFile) => {
    autoLayout(mat);
    rebuildFromModel(mat);
    undoStack.current = [];
    redoStack.current = [];
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const addNode = useCallback(
    (type: MatExprType, x: number, y: number) => {
      pushUndo(material);
      addExprToModel(material, type, x, y);
      rebuildFromModel(material);
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [material],
  );

  const deleteNode = useCallback(
    (exprId: number) => {
      pushUndo(material);
      removeExpression(material, exprId);
      rebuildFromModel(material);
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [material],
  );

  const updateParam = handleParamChange;

  const undo = useCallback(() => {
    const prev = undoStack.current.pop();
    if (!prev) return;
    redoStack.current.push(JSON.stringify(material));
    const restored = JSON.parse(prev) as UzMatFile;
    rebuildFromModel(restored);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [material]);

  const redo = useCallback(() => {
    const next = redoStack.current.pop();
    if (!next) return;
    undoStack.current.push(JSON.stringify(material));
    const restored = JSON.parse(next) as UzMatFile;
    rebuildFromModel(restored);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [material]);

  return {
    nodes,
    edges,
    material,
    onNodesChange,
    onEdgesChange,
    onConnect,
    setMaterial,
    addNode,
    deleteNode,
    updateParam,
    undo,
    redo,
    canUndo: undoStack.current.length > 0,
    canRedo: redoStack.current.length > 0,
  };
}
