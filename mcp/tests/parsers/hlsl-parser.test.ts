import { describe, it, expect } from "vitest";
import { parseHlslFile, extractHlslIncludes } from "../../tools/parsers/hlsl-parser.js";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

describe("parseHlslFile — synthetic", () => {
  it("parses shader functions with @ref", () => {
    const source = `/// GGX NDF. @ref Walter 2007 eq.33
float D_GGX(float NdotH, float alpha) {
    float a2 = alpha * alpha;
    return a2 / (PI * a2);
}

/// Schlick Fresnel. @ref Schlick 1994
float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * Pow5(1.0 - cosTheta);
}`;

    const entities = parseHlslFile(source, "shaders/include/pbr.hlsli");

    const dggx = entities.find((e) => e.name === "D_GGX");
    expect(dggx).toBeDefined();
    expect(dggx!.kind).toBe("shader_fn");
    expect(dggx!.type).toBe("float");
    expect(dggx!.refs).toBeDefined();
    expect(dggx!.refs![0].author).toBe("Walter");

    const schlick = entities.find((e) => e.name === "F_Schlick");
    expect(schlick).toBeDefined();
    expect(schlick!.kind).toBe("shader_fn");
    expect(schlick!.params).toHaveLength(2);
    expect(schlick!.params![0].name).toBe("cosTheta");
    expect(schlick!.params![0].type).toBe("float");
    expect(schlick!.params![1].name).toBe("F0");
    expect(schlick!.params![1].type).toBe("float3");
  });

  it("parses cbuffer blocks", () => {
    const source = `/// Per-frame constants for lighting.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
};`;

    const entities = parseHlslFile(source, "shaders/lighting.hlsl");

    const cb = entities.find((e) => e.name === "PerFrameCB");
    expect(cb).toBeDefined();
    expect(cb!.kind).toBe("shader_cb");
    expect(cb!.brief).toContain("Per-frame constants");
  });

  it("parses structs", () => {
    const source = `/// Fullscreen input from vertex shader.
struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};`;

    const entities = parseHlslFile(source, "shaders/common.hlsl");

    const st = entities.find((e) => e.name === "FSInput");
    expect(st).toBeDefined();
    expect(st!.kind).toBe("struct");
  });

  it("parses compute shader with numthreads", () => {
    const source = `/// Generates the transmittance LUT.
///
/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering"
[numthreads(8,8,1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    // compute
}`;

    const entities = parseHlslFile(source, "shaders/atmosphere/transmittance_cs.hlsl");

    const fn = entities.find((e) => e.name === "main");
    expect(fn).toBeDefined();
    expect(fn!.kind).toBe("shader_fn");
    expect(fn!.refs).toBeDefined();
    expect(fn!.refs![0].title).toBe("Precomputed Atmospheric Scattering");
  });

  it("parses file-level doc block", () => {
    const source = `/// Deferred lighting pass.
///
/// @ref Karis 2013 "Real Shading in Unreal Engine 4"

#include "pbr_common.hlsli"

float4 main(FSInput i) : SV_Target {
    return float4(1,1,1,1);
}`;

    const entities = parseHlslFile(source, "shaders/deferred.hlsl");

    const fileDoc = entities.find((e) => e.kind === "file");
    expect(fileDoc).toBeDefined();
    expect(fileDoc!.brief).toContain("Deferred lighting");
    expect(fileDoc!.refs).toBeDefined();
  });
});

describe("parseHlslFile — real files", () => {
  it("parses pbr_common.hlsli", () => {
    const path = resolve("../runtime/shaders/runtime/include/pbr_common.hlsli");
    let source: string;
    try {
      source = readFileSync(path, "utf-8");
    } catch {
      return;
    }

    const entities = parseHlslFile(
      source,
      "runtime/shaders/runtime/include/pbr_common.hlsli",
    );

    // Should have D_GGX, V_SmithGGXCorrelated, F_Schlick, etc.
    const dggx = entities.find((e) => e.name === "D_GGX");
    expect(dggx).toBeDefined();
    expect(dggx!.kind).toBe("shader_fn");

    const fschlick = entities.find((e) => e.name === "F_Schlick");
    expect(fschlick).toBeDefined();

    const burley = entities.find((e) => e.name === "Fd_Burley");
    expect(burley).toBeDefined();

    // File-level doc
    const fileDoc = entities.find((e) => e.kind === "file");
    expect(fileDoc).toBeDefined();
    expect(fileDoc!.brief).toContain("PBR BRDF");
  });
});

describe("extractHlslIncludes", () => {
  it("extracts relative includes", () => {
    const source = `#include "../include/pbr_common.hlsli"
#include "../include/math_util.hlsli"
// #include "commented_out.hlsli"`;

    const includes = extractHlslIncludes(source);
    expect(includes).toEqual([
      "../include/pbr_common.hlsli",
      "../include/math_util.hlsli",
    ]);
  });
});
