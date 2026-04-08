/// UzEngine Material Editor — Type definitions
/// Direct TypeScript port of uz::MatPinType, uz::MatExprType, uz::MatPin, uz::MaterialExpression
/// from runtime/engine/material/material_expression.h

// ─────────────────────────────────────────────────────────────────────────────
// Pin Types
// ─────────────────────────────────────────────────────────────────────────────

export enum MatPinType {
  Float = 'Float',
  Float2 = 'Float2',
  Float3 = 'Float3',
  Float4 = 'Float4',
  Texture = 'Texture',
}

export enum MatPinDir {
  Input = 'Input',
  Output = 'Output',
}

export interface MatPin {
  id: number;
  name: string;
  type: MatPinType;
  direction: MatPinDir;
  defaultValue: [number, number, number, number];
  connectedExprId: number;
  connectedPinId: number;
}

// ─────────────────────────────────────────────────────────────────────────────
// Expression Types — mirrors uz::MatExprType enum values
// ─────────────────────────────────────────────────────────────────────────────

export enum MatExprType {
  // Output
  PBROutput = 0,

  // Constants
  ScalarConstant = 100,
  Vector2Constant = 101,
  Vector3Constant = 102,
  Vector4Constant = 103,
  ColorConstant = 104,

  // Math
  Add = 200,
  Subtract = 201,
  Multiply = 202,
  Divide = 203,
  Lerp = 204,
  Clamp = 205,
  Saturate = 206,
  Abs = 207,
  Pow = 208,
  Sqrt = 209,
  OneMinus = 210,
  Frac = 211,
  Floor = 212,
  Ceil = 213,
  Min = 214,
  Max = 215,
  Dot = 216,
  Cross = 217,
  Normalize = 218,
  Length = 219,
  Step = 220,
  SmoothStep = 221,

  // Texture
  TextureSample = 300,

  // Coordinates
  TexCoord = 400,
  WorldPosition = 401,
  ObjectPosition = 402,
  VertexNormal = 403,
  VertexColor = 404,
  CameraVector = 405,
  ScreenPosition = 406,
  Time = 407,

  // Procedural
  CheckerPattern = 500,
  NoisePerlin = 501,
  GradientLinear = 502,
  GradientRadial = 503,

  // Utility
  TriPlanarMapping = 600,
  Fresnel = 601,
  Panner = 602,
  ComponentMask = 603,
  Append = 604,

  // Parameters
  ScalarParameter = 700,
  VectorParameter = 701,
  TextureParameter = 702,
}

// ─────────────────────────────────────────────────────────────────────────────
// MaterialExpression
// ─────────────────────────────────────────────────────────────────────────────

export interface MaterialExpression {
  id: number;
  type: MatExprType;
  name: string;
  posX: number;
  posY: number;
  inputs: MatPin[];
  outputs: MatPin[];
  scalarParams: Record<string, number>;
  stringParams: Record<string, string>;
  intParams: Record<string, number>;
}

// ─────────────────────────────────────────────────────────────────────────────
// Material Graph Top-Level
// ─────────────────────────────────────────────────────────────────────────────

export type MaterialDomain = 'Surface' | 'PostProcess' | 'Decal' | 'UI';
export type ShadingModel = 'DefaultLit' | 'Unlit' | 'Subsurface';
export type BlendMode = 'Opaque' | 'Masked' | 'Translucent';

export interface MaterialParams {
  baseColorTint?: [number, number, number];
  roughnessMultiplier?: number;
  metallicMultiplier?: number;
  normalStrength?: number;
  emissiveMultiplier?: number;
  specularMultiplier?: number;
  aoMultiplier?: number;
}

export interface UzMatFile {
  version: number;
  name: string;
  domain: MaterialDomain;
  shadingModel: ShadingModel;
  blendMode?: BlendMode;
  twoSided?: boolean;
  expressions: MaterialExpression[];
  params?: MaterialParams;
}

// ─────────────────────────────────────────────────────────────────────────────
// Type string table — mirrors kExprTypeTable in material_expression.cpp
// ─────────────────────────────────────────────────────────────────────────────

const EXPR_TYPE_STRING_TABLE: [MatExprType, string][] = [
  [MatExprType.PBROutput, 'PBROutput'],
  [MatExprType.ScalarConstant, 'ScalarConstant'],
  [MatExprType.Vector2Constant, 'Vector2Constant'],
  [MatExprType.Vector3Constant, 'Vector3Constant'],
  [MatExprType.Vector4Constant, 'Vector4Constant'],
  [MatExprType.ColorConstant, 'ColorConstant'],
  [MatExprType.Add, 'Add'],
  [MatExprType.Subtract, 'Subtract'],
  [MatExprType.Multiply, 'Multiply'],
  [MatExprType.Divide, 'Divide'],
  [MatExprType.Lerp, 'Lerp'],
  [MatExprType.Clamp, 'Clamp'],
  [MatExprType.Saturate, 'Saturate'],
  [MatExprType.Abs, 'Abs'],
  [MatExprType.Pow, 'Pow'],
  [MatExprType.Sqrt, 'Sqrt'],
  [MatExprType.OneMinus, 'OneMinus'],
  [MatExprType.Frac, 'Frac'],
  [MatExprType.Floor, 'Floor'],
  [MatExprType.Ceil, 'Ceil'],
  [MatExprType.Min, 'Min'],
  [MatExprType.Max, 'Max'],
  [MatExprType.Dot, 'Dot'],
  [MatExprType.Cross, 'Cross'],
  [MatExprType.Normalize, 'Normalize'],
  [MatExprType.Length, 'Length'],
  [MatExprType.Step, 'Step'],
  [MatExprType.SmoothStep, 'SmoothStep'],
  [MatExprType.TextureSample, 'TextureSample'],
  [MatExprType.TexCoord, 'TexCoord'],
  [MatExprType.WorldPosition, 'WorldPosition'],
  [MatExprType.ObjectPosition, 'ObjectPosition'],
  [MatExprType.VertexNormal, 'VertexNormal'],
  [MatExprType.VertexColor, 'VertexColor'],
  [MatExprType.CameraVector, 'CameraVector'],
  [MatExprType.ScreenPosition, 'ScreenPosition'],
  [MatExprType.Time, 'Time'],
  [MatExprType.CheckerPattern, 'CheckerPattern'],
  [MatExprType.NoisePerlin, 'NoisePerlin'],
  [MatExprType.GradientLinear, 'GradientLinear'],
  [MatExprType.GradientRadial, 'GradientRadial'],
  [MatExprType.TriPlanarMapping, 'TriPlanarMapping'],
  [MatExprType.Fresnel, 'Fresnel'],
  [MatExprType.Panner, 'Panner'],
  [MatExprType.ComponentMask, 'ComponentMask'],
  [MatExprType.Append, 'Append'],
  [MatExprType.ScalarParameter, 'ScalarParameter'],
  [MatExprType.VectorParameter, 'VectorParameter'],
  [MatExprType.TextureParameter, 'TextureParameter'],
];

const stringToType = new Map<string, MatExprType>(
  EXPR_TYPE_STRING_TABLE.map(([t, s]) => [s, t]),
);
const typeToString = new Map<MatExprType, string>(EXPR_TYPE_STRING_TABLE);

export function matExprTypeToString(type: MatExprType): string {
  return typeToString.get(type) ?? 'Unknown';
}

export function matExprTypeFromString(str: string): MatExprType {
  return stringToType.get(str) ?? MatExprType.PBROutput;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pin Type Utilities
// ─────────────────────────────────────────────────────────────────────────────

export function matPinTypeComponents(t: MatPinType): number {
  switch (t) {
    case MatPinType.Float: return 1;
    case MatPinType.Float2: return 2;
    case MatPinType.Float3: return 3;
    case MatPinType.Float4: return 4;
    case MatPinType.Texture: return 0;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Expression Categories for UI grouping
// ─────────────────────────────────────────────────────────────────────────────

export type ExprCategory = 'Output' | 'Constants' | 'Math' | 'Texture' | 'Coordinates' | 'Procedural' | 'Utility' | 'Parameters';

export function getExprCategory(type: MatExprType): ExprCategory {
  if (type === MatExprType.PBROutput) return 'Output';
  if (type >= 100 && type < 200) return 'Constants';
  if (type >= 200 && type < 300) return 'Math';
  if (type >= 300 && type < 400) return 'Texture';
  if (type >= 400 && type < 500) return 'Coordinates';
  if (type >= 500 && type < 600) return 'Procedural';
  if (type >= 600 && type < 700) return 'Utility';
  if (type >= 700 && type < 800) return 'Parameters';
  return 'Output';
}

/// Pin type color for the graph editor.
/// Colors match UE5 FEdGraphUtilities::GetTypeColor exactly:
/// Float=green, Float2=light-green, Float3=yellow, Float4=salmon, Texture=red
export function pinTypeColor(t: MatPinType): string {
  switch (t) {
    case MatPinType.Float: return '#36c736';
    case MatPinType.Float2: return '#9fdb9f';
    case MatPinType.Float3: return '#f9e770';
    case MatPinType.Float4: return '#fc6f6f';
    case MatPinType.Texture: return '#ff7070';
  }
}
