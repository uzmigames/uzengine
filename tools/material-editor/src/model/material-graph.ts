/// UzEngine Material Graph — In-memory graph model with validation
/// Mirrors uz::MaterialGraph from material_graph.h/cpp

import { createExpression } from './expression-registry';
import {
  type MaterialExpression,
  type UzMatFile,
  MatExprType,
} from './types';

// ─────────────────────────────────────────────────────────────────────────────
// Graph operations
// ─────────────────────────────────────────────────────────────────────────────

export function nextExprId(expressions: MaterialExpression[]): number {
  let maxId = 0;
  for (const e of expressions) {
    if (e.id > maxId) maxId = e.id;
  }
  return maxId + 1;
}

export function addExpression(
  mat: UzMatFile,
  type: MatExprType,
  posX: number,
  posY: number,
): MaterialExpression {
  const id = nextExprId(mat.expressions);
  const expr = createExpression(type, id);
  expr.posX = posX;
  expr.posY = posY;
  mat.expressions.push(expr);
  return expr;
}

export function removeExpression(mat: UzMatFile, exprId: number): void {
  // Don't allow removing the PBR Output node
  const expr = mat.expressions.find((e) => e.id === exprId);
  if (!expr || expr.type === MatExprType.PBROutput) return;

  // Disconnect all inputs on other expressions that reference this one
  for (const other of mat.expressions) {
    for (const pin of other.inputs) {
      if (pin.connectedExprId === exprId) {
        pin.connectedExprId = 0;
        pin.connectedPinId = 0;
      }
    }
  }

  // Remove the expression
  mat.expressions = mat.expressions.filter((e) => e.id !== exprId);
}

export function connect(
  mat: UzMatFile,
  srcExprId: number,
  srcPinId: number,
  dstExprId: number,
  dstPinId: number,
): boolean {
  const dstExpr = mat.expressions.find((e) => e.id === dstExprId);
  if (!dstExpr) return false;

  const dstPin = dstExpr.inputs.find((p) => p.id === dstPinId);
  if (!dstPin) return false;

  const srcExpr = mat.expressions.find((e) => e.id === srcExprId);
  if (!srcExpr) return false;

  const srcPin = srcExpr.outputs.find((p) => p.id === srcPinId);
  if (!srcPin) return false;

  // Check for cycles
  if (wouldCreateCycle(mat, srcExprId, dstExprId)) return false;

  dstPin.connectedExprId = srcExprId;
  dstPin.connectedPinId = srcPinId;
  return true;
}

export function disconnect(
  mat: UzMatFile,
  dstExprId: number,
  dstPinId: number,
): void {
  const dstExpr = mat.expressions.find((e) => e.id === dstExprId);
  if (!dstExpr) return;

  const dstPin = dstExpr.inputs.find((p) => p.id === dstPinId);
  if (!dstPin) return;

  dstPin.connectedExprId = 0;
  dstPin.connectedPinId = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cycle Detection — DFS from dstExprId upstream to see if srcExprId is reachable
// ─────────────────────────────────────────────────────────────────────────────

function wouldCreateCycle(
  mat: UzMatFile,
  srcExprId: number,
  dstExprId: number,
): boolean {
  // If connecting srcExpr -> dstExpr, check if dstExpr can already reach srcExpr
  // (which would mean adding this edge creates a cycle).
  if (srcExprId === dstExprId) return true;

  const visited = new Set<number>();
  const stack = [srcExprId];

  while (stack.length > 0) {
    const current = stack.pop()!;
    if (current === dstExprId) continue; // skip the target itself
    if (visited.has(current)) continue;
    visited.add(current);

    const expr = mat.expressions.find((e) => e.id === current);
    if (!expr) continue;

    for (const pin of expr.inputs) {
      if (pin.connectedExprId === 0) continue;
      if (pin.connectedExprId === dstExprId) return true;
      stack.push(pin.connectedExprId);
    }
  }

  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Topological Sort (for compilation order)
// ─────────────────────────────────────────────────────────────────────────────

export function topologicalSort(mat: UzMatFile): MaterialExpression[] {
  const sorted: MaterialExpression[] = [];
  const visited = new Set<number>();
  const visiting = new Set<number>();

  const exprById = new Map<number, MaterialExpression>();
  for (const e of mat.expressions) exprById.set(e.id, e);

  function visit(exprId: number): boolean {
    if (visited.has(exprId)) return true;
    if (visiting.has(exprId)) return false; // cycle
    visiting.add(exprId);

    const expr = exprById.get(exprId);
    if (!expr) return true;

    for (const pin of expr.inputs) {
      if (pin.connectedExprId !== 0) {
        if (!visit(pin.connectedExprId)) return false;
      }
    }

    visiting.delete(exprId);
    visited.add(exprId);
    sorted.push(expr);
    return true;
  }

  // Start from PBROutput (id=1 by convention)
  const pbrOutput = mat.expressions.find((e) => e.type === MatExprType.PBROutput);
  if (pbrOutput) {
    visit(pbrOutput.id);
  }

  return sorted;
}

// ─────────────────────────────────────────────────────────────────────────────
// Create a default material with PBROutput only
// ─────────────────────────────────────────────────────────────────────────────

export function createDefaultMaterial(): UzMatFile {
  const pbrOutput = createExpression(MatExprType.PBROutput, 1);
  pbrOutput.posX = 400;
  pbrOutput.posY = 200;

  return {
    version: 1,
    name: 'New Material',
    domain: 'Surface',
    shadingModel: 'DefaultLit',
    expressions: [pbrOutput],
  };
}
