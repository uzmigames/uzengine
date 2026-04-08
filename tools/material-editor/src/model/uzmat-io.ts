/// UzEngine .uzmat JSON Load/Save
/// Handles both pin connection formats:
///   - Named:  { "dstPin": "BaseColor", "srcExpr": 2, "srcPin": "RGBA" }
///   - Legacy: { "pin": 0, "srcExpr": 2, "srcPin": 0 }
/// Always saves in named-pin format (compatible with C++ ToJson).

import { createExpression } from './expression-registry';
import {
  type MaterialExpression,
  type UzMatFile,
  type MaterialParams,
  type MaterialDomain,
  type ShadingModel,
  type BlendMode,
  matExprTypeFromString,
  matExprTypeToString,
} from './types';

// ─────────────────────────────────────────────────────────────────────────────
// Parse .uzmat JSON → UzMatFile
// ─────────────────────────────────────────────────────────────────────────────

/* eslint-disable @typescript-eslint/no-explicit-any */

export function parseUzmat(jsonString: string): UzMatFile {
  const raw = JSON.parse(jsonString);

  const expressions: MaterialExpression[] = [];
  const rawExprs: any[] = raw.expressions ?? [];

  // First pass: create expressions with factory defaults
  for (const rawExpr of rawExprs) {
    const type = matExprTypeFromString(rawExpr.type);
    const id = rawExpr.id as number;
    const expr = createExpression(type, id);

    // Override name if provided
    if (rawExpr.name) expr.name = rawExpr.name;

    // Override position if provided
    if (rawExpr.posX != null) expr.posX = rawExpr.posX;
    if (rawExpr.posY != null) expr.posY = rawExpr.posY;

    // Merge scalar params
    if (rawExpr.scalarParams) {
      for (const [k, v] of Object.entries(rawExpr.scalarParams)) {
        expr.scalarParams[k] = v as number;
      }
    }

    // Merge string params
    if (rawExpr.stringParams) {
      for (const [k, v] of Object.entries(rawExpr.stringParams)) {
        expr.stringParams[k] = v as string;
      }
    }

    // Merge int params
    if (rawExpr.intParams) {
      for (const [k, v] of Object.entries(rawExpr.intParams)) {
        expr.intParams[k] = v as number;
      }
    }

    expressions.push(expr);
  }

  // Build a lookup map: expression id → expression
  const exprById = new Map<number, MaterialExpression>();
  for (const expr of expressions) {
    exprById.set(expr.id, expr);
  }

  // Second pass: resolve connections
  for (let i = 0; i < rawExprs.length; i++) {
    const rawExpr = rawExprs[i];
    const expr = expressions[i];
    const rawInputs: any[] = rawExpr.inputs ?? [];

    for (const conn of rawInputs) {
      const srcExprId = conn.srcExpr as number;
      const srcExpr = exprById.get(srcExprId);
      if (!srcExpr) continue;

      // Resolve destination pin (on this expression)
      let dstPinIdx: number;
      if (typeof conn.dstPin === 'string') {
        // Named format
        dstPinIdx = expr.inputs.findIndex((p) => p.name === conn.dstPin);
      } else {
        // Legacy numeric format: "pin" field
        dstPinIdx = expr.inputs.findIndex((p) => p.id === (conn.pin ?? conn.dstPin));
      }
      if (dstPinIdx < 0) continue;

      // Resolve source pin (on the source expression)
      let srcPinId: number;
      if (typeof conn.srcPin === 'string') {
        const found = srcExpr.outputs.find((p) => p.name === conn.srcPin);
        srcPinId = found ? found.id : 0;
      } else {
        srcPinId = conn.srcPin as number;
      }

      expr.inputs[dstPinIdx].connectedExprId = srcExprId;
      expr.inputs[dstPinIdx].connectedPinId = srcPinId;
    }
  }

  // Parse material params
  let params: MaterialParams | undefined;
  if (raw.params) {
    params = {
      baseColorTint: raw.params.baseColorTint,
      roughnessMultiplier: raw.params.roughnessMultiplier,
      metallicMultiplier: raw.params.metallicMultiplier,
      normalStrength: raw.params.normalStrength,
      emissiveMultiplier: raw.params.emissiveMultiplier,
      specularMultiplier: raw.params.specularMultiplier,
      aoMultiplier: raw.params.aoMultiplier,
    };
  }

  return {
    version: raw.version ?? 1,
    name: raw.name ?? 'Untitled',
    domain: (raw.domain as MaterialDomain) ?? 'Surface',
    shadingModel: (raw.shadingModel as ShadingModel) ?? 'DefaultLit',
    blendMode: raw.blendMode as BlendMode | undefined,
    twoSided: raw.twoSided as boolean | undefined,
    expressions,
    params,
  };
}

// ─────────────────────────────────────────────────────────────────────────────
// Serialize UzMatFile → JSON string
// ─────────────────────────────────────────────────────────────────────────────

export function serializeUzmat(mat: UzMatFile): string {
  const raw: any = {
    version: mat.version,
    name: mat.name,
    domain: mat.domain,
    shadingModel: mat.shadingModel,
  };

  if (mat.blendMode) raw.blendMode = mat.blendMode;
  if (mat.twoSided != null) raw.twoSided = mat.twoSided;

  raw.expressions = mat.expressions.map((expr) => {
    const rawExpr: any = {
      id: expr.id,
      type: matExprTypeToString(expr.type),
    };

    // Only write name if user-customized (different from factory default)
    const factory = createExpression(expr.type, expr.id);
    if (expr.name !== factory.name) {
      rawExpr.name = expr.name;
    }

    // Write position if non-zero
    if (expr.posX !== 0 || expr.posY !== 0) {
      rawExpr.posX = expr.posX;
      rawExpr.posY = expr.posY;
    }

    // Write connections using named-pin format
    const inputs: any[] = [];
    for (const pin of expr.inputs) {
      if (pin.connectedExprId === 0) continue;

      // Resolve source pin name
      const srcExpr = mat.expressions.find((e) => e.id === pin.connectedExprId);
      const srcPinObj = srcExpr?.outputs.find((p) => p.id === pin.connectedPinId);
      const srcPinName = srcPinObj?.name ?? pin.connectedPinId;

      inputs.push({
        dstPin: pin.name,
        srcExpr: pin.connectedExprId,
        srcPin: srcPinName,
      });
    }
    if (inputs.length > 0) rawExpr.inputs = inputs;

    // Write params (only non-empty)
    if (Object.keys(expr.scalarParams).length > 0) {
      rawExpr.scalarParams = { ...expr.scalarParams };
    }
    if (Object.keys(expr.stringParams).length > 0) {
      rawExpr.stringParams = { ...expr.stringParams };
    }
    if (Object.keys(expr.intParams).length > 0) {
      rawExpr.intParams = { ...expr.intParams };
    }

    return rawExpr;
  });

  if (mat.params) {
    const p: any = {};
    if (mat.params.baseColorTint) p.baseColorTint = mat.params.baseColorTint;
    if (mat.params.roughnessMultiplier != null) p.roughnessMultiplier = mat.params.roughnessMultiplier;
    if (mat.params.metallicMultiplier != null) p.metallicMultiplier = mat.params.metallicMultiplier;
    if (mat.params.normalStrength != null) p.normalStrength = mat.params.normalStrength;
    if (mat.params.emissiveMultiplier != null) p.emissiveMultiplier = mat.params.emissiveMultiplier;
    if (mat.params.specularMultiplier != null) p.specularMultiplier = mat.params.specularMultiplier;
    if (mat.params.aoMultiplier != null) p.aoMultiplier = mat.params.aoMultiplier;
    if (Object.keys(p).length > 0) raw.params = p;
  }

  return JSON.stringify(raw, null, 4);
}
