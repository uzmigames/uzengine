/// UzEngine Material Expression Factory
/// Direct port of MaterialExpression::Create() from material_expression.cpp
/// Creates expressions with the correct pins and default params for each of 42 types.

import {
  type MaterialExpression,
  type MatPin,
  MatPinType,
  MatPinDir,
  MatExprType,
} from './types';

// ─────────────────────────────────────────────────────────────────────────────
// Pin construction helpers (mirrors MakeInput/MakeOutput in C++)
// ─────────────────────────────────────────────────────────────────────────────

function makeInput(
  id: number,
  name: string,
  type: MatPinType,
  d0 = 0,
  d1 = 0,
  d2 = 0,
  d3 = 0,
): MatPin {
  return {
    id,
    name,
    type,
    direction: MatPinDir.Input,
    defaultValue: [d0, d1, d2, d3],
    connectedExprId: 0,
    connectedPinId: 0,
  };
}

function makeOutput(id: number, name: string, type: MatPinType): MatPin {
  return {
    id,
    name,
    type,
    direction: MatPinDir.Output,
    defaultValue: [0, 0, 0, 0],
    connectedExprId: 0,
    connectedPinId: 0,
  };
}

// ─────────────────────────────────────────────────────────────────────────────
// Expression setup helpers
// ─────────────────────────────────────────────────────────────────────────────

function setupBinaryMath(
  expr: MaterialExpression,
  name: string,
  inputA = 'A',
  inputB = 'B',
) {
  expr.name = name;
  expr.inputs = [
    makeInput(0, inputA, MatPinType.Float),
    makeInput(1, inputB, MatPinType.Float),
  ];
  expr.outputs = [makeOutput(0, 'Result', MatPinType.Float)];
}

function setupUnaryMath(expr: MaterialExpression, name: string) {
  expr.name = name;
  expr.inputs = [makeInput(0, 'Value', MatPinType.Float)];
  expr.outputs = [makeOutput(0, 'Result', MatPinType.Float)];
}

// ─────────────────────────────────────────────────────────────────────────────
// Factory
// ─────────────────────────────────────────────────────────────────────────────

export function createExpression(
  type: MatExprType,
  id: number,
): MaterialExpression {
  const expr: MaterialExpression = {
    id,
    type,
    name: '',
    posX: 0,
    posY: 0,
    inputs: [],
    outputs: [],
    scalarParams: {},
    stringParams: {},
    intParams: {},
  };

  switch (type) {
    // ── Output ──────────────────────────────────────────────────────────────
    case MatExprType.PBROutput:
      expr.name = 'PBR Output';
      expr.inputs = [
        makeInput(0, 'BaseColor', MatPinType.Float3, 1, 1, 1),
        makeInput(1, 'Metallic', MatPinType.Float, 0),
        makeInput(2, 'Roughness', MatPinType.Float, 0.5),
        makeInput(3, 'Normal', MatPinType.Float3, 0, 0, 1),
        makeInput(4, 'Emissive', MatPinType.Float3, 0, 0, 0),
        makeInput(5, 'Opacity', MatPinType.Float, 1),
        makeInput(6, 'AO', MatPinType.Float, 1),
      ];
      break;

    // ── Constants ───────────────────────────────────────────────────────────
    case MatExprType.ScalarConstant:
      expr.name = 'Scalar Constant';
      expr.outputs = [makeOutput(0, 'Value', MatPinType.Float)];
      expr.scalarParams['value'] = 0;
      break;

    case MatExprType.Vector2Constant:
      expr.name = 'Vector2 Constant';
      expr.outputs = [makeOutput(0, 'Value', MatPinType.Float2)];
      expr.scalarParams['x'] = 0;
      expr.scalarParams['y'] = 0;
      break;

    case MatExprType.Vector3Constant:
      expr.name = 'Vector3 Constant';
      expr.outputs = [makeOutput(0, 'Value', MatPinType.Float3)];
      expr.scalarParams['x'] = 0;
      expr.scalarParams['y'] = 0;
      expr.scalarParams['z'] = 0;
      break;

    case MatExprType.Vector4Constant:
      expr.name = 'Vector4 Constant';
      expr.outputs = [makeOutput(0, 'Value', MatPinType.Float4)];
      expr.scalarParams['x'] = 0;
      expr.scalarParams['y'] = 0;
      expr.scalarParams['z'] = 0;
      expr.scalarParams['w'] = 0;
      break;

    case MatExprType.ColorConstant:
      expr.name = 'Color Constant';
      expr.outputs = [makeOutput(0, 'Color', MatPinType.Float3)];
      expr.scalarParams['r'] = 1;
      expr.scalarParams['g'] = 1;
      expr.scalarParams['b'] = 1;
      break;

    // ── Binary math ─────────────────────────────────────────────────────────
    case MatExprType.Add:
      setupBinaryMath(expr, 'Add');
      break;
    case MatExprType.Subtract:
      setupBinaryMath(expr, 'Subtract');
      break;
    case MatExprType.Multiply:
      setupBinaryMath(expr, 'Multiply');
      break;
    case MatExprType.Divide:
      setupBinaryMath(expr, 'Divide');
      break;
    case MatExprType.Min:
      setupBinaryMath(expr, 'Min');
      break;
    case MatExprType.Max:
      setupBinaryMath(expr, 'Max');
      break;
    case MatExprType.Pow:
      setupBinaryMath(expr, 'Pow', 'Base', 'Exp');
      break;
    case MatExprType.Step:
      setupBinaryMath(expr, 'Step', 'Edge', 'X');
      break;

    // ── Ternary math ────────────────────────────────────────────────────────
    case MatExprType.Lerp:
      expr.name = 'Lerp';
      expr.inputs = [
        makeInput(0, 'A', MatPinType.Float),
        makeInput(1, 'B', MatPinType.Float),
        makeInput(2, 'Alpha', MatPinType.Float),
      ];
      expr.outputs = [makeOutput(0, 'Result', MatPinType.Float)];
      break;

    case MatExprType.Clamp:
      expr.name = 'Clamp';
      expr.inputs = [
        makeInput(0, 'Value', MatPinType.Float),
        makeInput(1, 'Min', MatPinType.Float, 0),
        makeInput(2, 'Max', MatPinType.Float, 1),
      ];
      expr.outputs = [makeOutput(0, 'Result', MatPinType.Float)];
      break;

    case MatExprType.SmoothStep:
      expr.name = 'SmoothStep';
      expr.inputs = [
        makeInput(0, 'Edge0', MatPinType.Float),
        makeInput(1, 'Edge1', MatPinType.Float),
        makeInput(2, 'X', MatPinType.Float),
      ];
      expr.outputs = [makeOutput(0, 'Result', MatPinType.Float)];
      break;

    // ── Unary math ──────────────────────────────────────────────────────────
    case MatExprType.Saturate:
      setupUnaryMath(expr, 'Saturate');
      break;
    case MatExprType.Abs:
      setupUnaryMath(expr, 'Abs');
      break;
    case MatExprType.OneMinus:
      setupUnaryMath(expr, 'OneMinus');
      break;
    case MatExprType.Frac:
      setupUnaryMath(expr, 'Frac');
      break;
    case MatExprType.Floor:
      setupUnaryMath(expr, 'Floor');
      break;
    case MatExprType.Ceil:
      setupUnaryMath(expr, 'Ceil');
      break;
    case MatExprType.Sqrt:
      setupUnaryMath(expr, 'Sqrt');
      break;
    case MatExprType.Length:
      setupUnaryMath(expr, 'Length');
      break;

    case MatExprType.Normalize:
      expr.name = 'Normalize';
      expr.inputs = [makeInput(0, 'Value', MatPinType.Float3)];
      expr.outputs = [makeOutput(0, 'Result', MatPinType.Float3)];
      break;

    // ── Vector math ─────────────────────────────────────────────────────────
    case MatExprType.Dot:
      expr.name = 'Dot';
      expr.inputs = [
        makeInput(0, 'A', MatPinType.Float3),
        makeInput(1, 'B', MatPinType.Float3),
      ];
      expr.outputs = [makeOutput(0, 'Result', MatPinType.Float)];
      break;

    case MatExprType.Cross:
      expr.name = 'Cross';
      expr.inputs = [
        makeInput(0, 'A', MatPinType.Float3),
        makeInput(1, 'B', MatPinType.Float3),
      ];
      expr.outputs = [makeOutput(0, 'Result', MatPinType.Float3)];
      break;

    // ── Texture ─────────────────────────────────────────────────────────────
    case MatExprType.TextureSample:
      expr.name = 'Texture Sample';
      expr.inputs = [makeInput(0, 'UV', MatPinType.Float2)];
      expr.outputs = [
        makeOutput(0, 'RGBA', MatPinType.Float4),
        makeOutput(1, 'R', MatPinType.Float),
        makeOutput(2, 'G', MatPinType.Float),
        makeOutput(3, 'B', MatPinType.Float),
        makeOutput(4, 'A', MatPinType.Float),
      ];
      expr.stringParams['texturePath'] = '';
      break;

    // ── Coordinates ─────────────────────────────────────────────────────────
    case MatExprType.TexCoord:
      expr.name = 'TexCoord';
      expr.outputs = [makeOutput(0, 'UV', MatPinType.Float2)];
      expr.intParams['uvIndex'] = 0;
      break;

    case MatExprType.WorldPosition:
      expr.name = 'World Position';
      expr.outputs = [makeOutput(0, 'Position', MatPinType.Float3)];
      break;

    case MatExprType.ObjectPosition:
      expr.name = 'Object Position';
      expr.outputs = [makeOutput(0, 'Position', MatPinType.Float3)];
      break;

    case MatExprType.VertexNormal:
      expr.name = 'Vertex Normal';
      expr.outputs = [makeOutput(0, 'Normal', MatPinType.Float3)];
      break;

    case MatExprType.VertexColor:
      expr.name = 'Vertex Color';
      expr.outputs = [makeOutput(0, 'Color', MatPinType.Float4)];
      break;

    case MatExprType.CameraVector:
      expr.name = 'Camera Vector';
      expr.outputs = [makeOutput(0, 'Direction', MatPinType.Float3)];
      break;

    case MatExprType.ScreenPosition:
      expr.name = 'Screen Position';
      expr.outputs = [makeOutput(0, 'UV', MatPinType.Float2)];
      break;

    case MatExprType.Time:
      expr.name = 'Time';
      expr.outputs = [makeOutput(0, 'Seconds', MatPinType.Float)];
      break;

    // ── Procedural ──────────────────────────────────────────────────────────
    case MatExprType.CheckerPattern:
      expr.name = 'Checker Pattern';
      expr.inputs = [
        makeInput(0, 'UV', MatPinType.Float2),
        makeInput(1, 'Scale', MatPinType.Float, 10),
      ];
      expr.outputs = [makeOutput(0, 'Mask', MatPinType.Float)];
      break;

    case MatExprType.NoisePerlin:
      expr.name = 'Perlin Noise';
      expr.inputs = [
        makeInput(0, 'UV', MatPinType.Float2),
        makeInput(1, 'Scale', MatPinType.Float, 1),
      ];
      expr.outputs = [makeOutput(0, 'Value', MatPinType.Float)];
      break;

    case MatExprType.GradientLinear:
      expr.name = 'Linear Gradient';
      expr.inputs = [makeInput(0, 'UV', MatPinType.Float2)];
      expr.outputs = [makeOutput(0, 'Value', MatPinType.Float)];
      break;

    case MatExprType.GradientRadial:
      expr.name = 'Radial Gradient';
      expr.inputs = [makeInput(0, 'UV', MatPinType.Float2)];
      expr.outputs = [makeOutput(0, 'Value', MatPinType.Float)];
      break;

    // ── Utility ─────────────────────────────────────────────────────────────
    case MatExprType.TriPlanarMapping:
      expr.name = 'Tri-Planar Mapping';
      expr.inputs = [
        makeInput(0, 'WorldPos', MatPinType.Float3),
        makeInput(1, 'WorldNormal', MatPinType.Float3),
        makeInput(2, 'Sharpness', MatPinType.Float, 4),
      ];
      expr.outputs = [
        makeOutput(0, 'UV', MatPinType.Float2),
        makeOutput(1, 'BlendWeights', MatPinType.Float3),
      ];
      break;

    case MatExprType.Fresnel:
      expr.name = 'Fresnel';
      expr.inputs = [makeInput(0, 'Exponent', MatPinType.Float, 5)];
      expr.outputs = [makeOutput(0, 'Value', MatPinType.Float)];
      break;

    case MatExprType.Panner:
      expr.name = 'Panner';
      expr.inputs = [
        makeInput(0, 'UV', MatPinType.Float2),
        makeInput(1, 'SpeedX', MatPinType.Float, 0),
        makeInput(2, 'SpeedY', MatPinType.Float, 0),
      ];
      expr.outputs = [makeOutput(0, 'UV', MatPinType.Float2)];
      break;

    case MatExprType.ComponentMask:
      expr.name = 'Component Mask';
      expr.inputs = [makeInput(0, 'Input', MatPinType.Float4)];
      expr.outputs = [makeOutput(0, 'Result', MatPinType.Float)];
      expr.intParams['r'] = 1;
      expr.intParams['g'] = 1;
      expr.intParams['b'] = 1;
      expr.intParams['a'] = 0;
      break;

    case MatExprType.Append:
      expr.name = 'Append';
      expr.inputs = [
        makeInput(0, 'A', MatPinType.Float),
        makeInput(1, 'B', MatPinType.Float),
      ];
      expr.outputs = [makeOutput(0, 'Result', MatPinType.Float2)];
      break;

    // ── Parameters ──────────────────────────────────────────────────────────
    case MatExprType.ScalarParameter:
      expr.name = 'Scalar Parameter';
      expr.outputs = [makeOutput(0, 'Value', MatPinType.Float)];
      expr.stringParams['paramName'] = 'Param';
      expr.scalarParams['default'] = 0.5;
      break;

    case MatExprType.VectorParameter:
      expr.name = 'Vector Parameter';
      expr.outputs = [makeOutput(0, 'Value', MatPinType.Float3)];
      expr.stringParams['paramName'] = 'Param';
      expr.scalarParams['defaultR'] = 0;
      expr.scalarParams['defaultG'] = 0;
      expr.scalarParams['defaultB'] = 0;
      break;

    case MatExprType.TextureParameter:
      expr.name = 'Texture Parameter';
      expr.outputs = [makeOutput(0, 'Texture', MatPinType.Texture)];
      expr.stringParams['paramName'] = 'Texture';
      expr.stringParams['texturePath'] = '';
      break;
  }

  return expr;
}

/// All expression types available for creation in the palette (excludes PBROutput).
export const CREATABLE_EXPR_TYPES: MatExprType[] = [
  // Constants
  MatExprType.ScalarConstant,
  MatExprType.Vector2Constant,
  MatExprType.Vector3Constant,
  MatExprType.Vector4Constant,
  MatExprType.ColorConstant,
  // Math
  MatExprType.Add,
  MatExprType.Subtract,
  MatExprType.Multiply,
  MatExprType.Divide,
  MatExprType.Lerp,
  MatExprType.Clamp,
  MatExprType.Saturate,
  MatExprType.Abs,
  MatExprType.Pow,
  MatExprType.Sqrt,
  MatExprType.OneMinus,
  MatExprType.Frac,
  MatExprType.Floor,
  MatExprType.Ceil,
  MatExprType.Min,
  MatExprType.Max,
  MatExprType.Dot,
  MatExprType.Cross,
  MatExprType.Normalize,
  MatExprType.Length,
  MatExprType.Step,
  MatExprType.SmoothStep,
  // Texture
  MatExprType.TextureSample,
  // Coordinates
  MatExprType.TexCoord,
  MatExprType.WorldPosition,
  MatExprType.ObjectPosition,
  MatExprType.VertexNormal,
  MatExprType.VertexColor,
  MatExprType.CameraVector,
  MatExprType.ScreenPosition,
  MatExprType.Time,
  // Procedural
  MatExprType.CheckerPattern,
  MatExprType.NoisePerlin,
  MatExprType.GradientLinear,
  MatExprType.GradientRadial,
  // Utility
  MatExprType.TriPlanarMapping,
  MatExprType.Fresnel,
  MatExprType.Panner,
  MatExprType.ComponentMask,
  MatExprType.Append,
  // Parameters
  MatExprType.ScalarParameter,
  MatExprType.VectorParameter,
  MatExprType.TextureParameter,
];
